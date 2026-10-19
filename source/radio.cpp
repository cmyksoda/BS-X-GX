/****************************************************************************
 * BS-X GX
 *
 * radio.cpp
 *
 * SoundLink: the broadcast's radio, played from SD in step with its clock
 ***************************************************************************/

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/mutex.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>
#include <tremor/ivorbisfile.h>

#include "snes9xgx.h"
#include "bsxstream.h"
#include "radio.h"
#include "snes9x/snes9x.h"
#include "snes9x/bsx.h"

#define RING_FRAMES		65536		// two seconds at 32 kHz
#define OUTPUT_RATE		48000
#define LATENCY_MS		64			// what the DMA queue holds ahead of the speaker
#define RECEIVER_OFF	0x80		// $2197 bit 7: the receiver's audio is powered down
#define REG_2197		(0x2197 - 0x2180)
#define PAUSED_MS		300			// no mixing for this long: the game is in a menu

static lwp_t radioThread = LWP_THREAD_NULL;
static mutex_t wantMutex = LWP_MUTEX_NULL, ringMutex = LWP_MUTEX_NULL;
static char wantedPath[MAXPATHLEN];
static s64 wantedStart;

static s16 ring[RING_FRAMES * 2] ATTRIBUTE_ALIGN(32);
static volatile u32 writePos, readPos, readFrac;	// track frames; readFrac is 16-bit fraction
static volatile u32 step;							// track frames per output frame, 16.16
static volatile u32 rate = 32000;
static volatile bool playing = false;
static volatile u32 lastMixMs;

static u32 NowMs()
{
	return (u32)ticks_to_millisecs(gettime());
}

static void Fill(OggVorbis_File *vf, int channels)
{
	static char pcm[4096];
	while(writePos - readPos < RING_FRAMES - sizeof(pcm) / 2)
	{
		int section;
		long n = ov_read(vf, pcm, sizeof(pcm), &section);
		if(n <= 0)
			return;
		const s16 *src = (const s16 *)pcm;
		u32 w = writePos, frames = n / (2 * channels);
		for(u32 i = 0; i < frames; i++)
		{
			s16 *d = &ring[((w + i) & (RING_FRAMES - 1)) * 2];
			d[0] = src[i * channels];
			d[1] = src[i * channels + channels - 1];
		}
		__sync_synchronize();
		writePos = w + frames;
	}
}

static void SetPlaying(bool on)
{
	LWP_MutexLock(ringMutex);
	playing = on;
	LWP_MutexUnlock(ringMutex);
}

static void *RadioThread(void *arg)
{
	OggVorbis_File vf;
	bool open = false;
	char current[MAXPATHLEN] = "";
	s64 startMs = 0;
	int channels = 2;
	s64 total = 0;

	while(1)
	{
		char path[MAXPATHLEN];
		LWP_MutexLock(wantMutex);
		strcpy(path, wantedPath);
		s64 start = wantedStart;
		LWP_MutexUnlock(wantMutex);

		if(strcmp(path, current) != 0 || start != startMs)
		{
			SetPlaying(false);
			if(open)
				ov_clear(&vf);		// closes the file too
			open = false;
			strcpy(current, path);
			startMs = start;
			FILE *f = path[0] ? fopen(path, "rb") : NULL;
			if(f && ov_open(f, &vf, NULL, 0) == 0)
			{
				vorbis_info *vi = ov_info(&vf, -1);
				rate = vi->rate;
				channels = vi->channels;
				total = ov_pcm_total(&vf, -1);
				open = channels == 1 || channels == 2;
				if(!open)
					ov_clear(&vf);
			}
			else if(f)
				fclose(f);
		}

		s64 now;
		s64 target = open && BSXStreamClockMs(&now) ? (now - startMs + LATENCY_MS) * rate / 1000 : -1;
		if(target < 0 || target >= total)
		{
			SetPlaying(false);
			usleep(100000);
			continue;
		}

		// while the game is paused nothing is mixed; the jump comes when it plays again
		if(playing && NowMs() - lastMixMs > PAUSED_MS)
		{
			usleep(20000);
			continue;
		}

		s64 error = target - (s64)readPos;
		if(!playing || error > rate / 2 || error < -(s64)(rate / 2))
		{
			// tuning in, or half a second out (back from a menu): jump to where the broadcast is
			SetPlaying(false);
			ov_pcm_seek(&vf, target);
			LWP_MutexLock(ringMutex);
			readPos = writePos = (u32)target;
			readFrac = 0;
			LWP_MutexUnlock(ringMutex);
			step = (u32)(rate * 65536.0 / OUTPUT_RATE);
			Fill(&vf, channels);
			// decoding the first two seconds took a while: start from where the broadcast is now
			if(BSXStreamClockMs(&now))
			{
				s64 late = (now - startMs + LATENCY_MS) * rate / 1000;
				if(late > (s64)readPos && late < (s64)writePos)
					readPos = (u32)late;
			}
			SetPlaying(true);
		}
		else
		{
			// drift is pulled in by playing at most 0.5% fast or slow, too little to hear
			double c = (double)error / (rate * 2.0);
			c = c > 0.005 ? 0.005 : c < -0.005 ? -0.005 : c;
			step = (u32)(rate * 65536.0 / OUTPUT_RATE * (1.0 + c));
			Fill(&vf, channels);
		}
		usleep(20000);
	}
	return NULL;
}

void RadioSet(const char *path, s64 startMs)
{
	if(wantMutex == LWP_MUTEX_NULL)
	{
		LWP_MutexInit(&wantMutex, false);
		LWP_MutexInit(&ringMutex, false);
	}
	LWP_MutexLock(wantMutex);
	snprintf(wantedPath, sizeof(wantedPath), "%s", path);
	wantedStart = startMs;
	LWP_MutexUnlock(wantMutex);
	if(radioThread == LWP_THREAD_NULL && path[0])
		LWP_CreateThread(&radioThread, RadioThread, NULL, NULL, 65536, 50);
}

void RadioMix(s16 *buffer, int frames)
{
	if(ringMutex == LWP_MUTEX_NULL)
		return;
	u32 now = NowMs();
	bool resumed = now - lastMixMs > PAUSED_MS;
	lastMixMs = now;
	LWP_MutexLock(ringMutex);
	if(!playing || resumed)
	{
		// back from a pause: silent until the decoder has caught up with the broadcast
		playing = false;
		LWP_MutexUnlock(ringMutex);
		return;
	}
	bool heard = GCSettings.RadioVolume > 0 && !(BSX.PPU[REG_2197] & RECEIVER_OFF);
	int volume = GCSettings.RadioVolume * 256 / 100;
	u32 pos = readPos, frac = readFrac, inc = step, end = writePos;

	// the broadcast keeps moving while the receiver is off, so the position always advances
	for(int i = 0; i < frames && pos + 1 < end; i++)
	{
		if(heard)
		{
			const s16 *a = &ring[(pos & (RING_FRAMES - 1)) * 2];
			const s16 *b = &ring[((pos + 1) & (RING_FRAMES - 1)) * 2];
			int w = frac >> 1;
			int l = a[0] + (((b[0] - a[0]) * w) >> 15);
			int r = a[1] + (((b[1] - a[1]) * w) >> 15);
			// the core hands the Wii right, then left
			int o0 = buffer[i * 2] + ((r * volume) >> 8);
			int o1 = buffer[i * 2 + 1] + ((l * volume) >> 8);
			buffer[i * 2] = o0 > 32767 ? 32767 : o0 < -32768 ? -32768 : o0;
			buffer[i * 2 + 1] = o1 > 32767 ? 32767 : o1 < -32768 ? -32768 : o1;
		}
		frac += inc;
		pos += frac >> 16;
		frac &= 0xFFFF;
	}
	readPos = pos;
	readFrac = frac;
	LWP_MutexUnlock(ringMutex);
}

void RadioGetStatus(RadioStatus *out)
{
	s64 now;
	out->playing = playing;
	out->heard = playing && GCSettings.RadioVolume > 0 && !(BSX.PPU[REG_2197] & RECEIVER_OFF);
	out->msIn = rate ? (s32)((u64)readPos * 1000 / rate) : 0;
	out->broadcastMs = wantedPath[0] && BSXStreamClockMs(&now) ? (s32)(now - wantedStart) : -1;
}
