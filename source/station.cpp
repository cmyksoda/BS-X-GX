/****************************************************************************
 * BS-X GX
 *
 * station.cpp
 *
 * Tunes in to a station and keeps the satellite fed with what's on air
 ***************************************************************************/

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/mutex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <zlib.h>

#include "bsxstream.h"
#include "station.h"
#include "utils/http.h"

#define MANIFEST_MAX	16384
#define RETRY_SECONDS	30

struct ManifestFile
{
	u16 lci;
	u16 index;
	u32 size;
	u32 crc;
	char path[96];
};

static lwp_t stationThread = LWP_THREAD_NULL;
static mutex_t statusMutex = LWP_MUTEX_NULL;
static StationStatus status;
static u64 onAirSince;
static int onAirLength;
static volatile bool refresh = false;
static char pendingUrl[128];
static volatile bool urlChanged = false;

static char host[96];
static u16 port = 80;
static char manifest[MANIFEST_MAX + 1];
static ManifestFile files[BSX_STREAM_MAX_FILES];
static char currentSlot[32];

static void SetStatus(int state, const char *error)
{
	LWP_MutexLock(statusMutex);
	status.state = state;
	snprintf(status.error, sizeof(status.error), "%s", error ? error : "");
	LWP_MutexUnlock(statusMutex);
}

static bool ParseUrl(const char *url)
{
	if(strncasecmp(url, "http://", 7) == 0)
		url += 7;
	snprintf(host, sizeof(host), "%s", url);
	char *slash = strchr(host, '/');
	if(slash)
		*slash = 0;
	char *colon = strchr(host, ':');
	port = 80;
	if(colon)
	{
		*colon = 0;
		port = atoi(colon + 1);
	}
	return host[0] && port;
}

// plain text, one field per line; see bsx-station/satdata/slot.py
static int ParseManifest(char *text, char *slot, char *title, char *next, int *endsIn)
{
	int count = 0;
	bool header = false, ended = false;
	*endsIn = 0;
	for(char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n"))
	{
		int version;
		if(sscanf(line, "bsxgx %d", &version) == 1)
			header = version == 1;
		else if(!strncmp(line, "slot ", 5))
			snprintf(slot, 32, "%s", line + 5);
		else if(!strncmp(line, "title ", 6))
			snprintf(title, 64, "%s", line + 6);
		else if(!strncmp(line, "next ", 5))
			snprintf(next, 64, "%s", line + 5);
		else if(!strncmp(line, "ends_in ", 8))
			*endsIn = atoi(line + 8);
		else if(!strncmp(line, "f ", 2) && count < BSX_STREAM_MAX_FILES)
		{
			ManifestFile &f = files[count];
			unsigned lci, index, size, crc;
			if(sscanf(line, "f %x %u %u %x %95s", &lci, &index, &size, &crc, f.path) == 5)
			{
				f.lci = lci;
				f.index = index;
				f.size = size;
				f.crc = crc;
				count++;
			}
		}
		else if(!strcmp(line, "end"))
			ended = true;
	}
	return header && ended && slot[0] ? count : -1;
}

static const char *Receive(int count)
{
	BSXStreamTable *t = BSXStreamBeginBuild(60000);
	if(!t)
		return "no memory for the broadcast";

	for(int i = 0; i < count; i++)
	{
		ManifestFile &f = files[i];
		u8 *dst = BSXStreamAdd(t, f.lci, f.index, f.size);
		if(!dst)
			return "broadcast too big";
		int n = HttpGet(host, port, f.path, dst, f.size);
		if(n < 0)
			return HttpErrorString(n);
		if((u32)n != f.size || crc32(crc32(0L, Z_NULL, 0), dst, f.size) != f.crc)
			return "broadcast arrived damaged";
	}
	// swapping mid-download makes the BIOS announce the new program and drop the game
	for(int i = 0; i < 1800 && BSXStreamReceiving(3000); i++)
		usleep(100000);
	BSXStreamPublish(t);
	return NULL;
}

static void Nap(int seconds)
{
	for(int i = 0; i < seconds * 10 && !refresh; i++)
		usleep(100000);
	refresh = false;
}

static void *StationThread(void *arg)
{
	while(1)
	{
		if(urlChanged)
		{
			urlChanged = false;
			currentSlot[0] = 0;
			if(!pendingUrl[0])
			{
				BSXStreamClear();
				SetStatus(STATION_OFF, NULL);
				while(!urlChanged)
					Nap(3600);
				continue;
			}
			if(!ParseUrl(pendingUrl))
			{
				SetStatus(STATION_ERROR, "the station address looks wrong");
				Nap(RETRY_SECONDS);
				continue;
			}
			SetStatus(STATION_TUNING, NULL);
		}

		int n = HttpGet(host, port, "/wii/now.txt", (u8 *)manifest, MANIFEST_MAX);
		u64 fetched = gettime();
		if(n < 0)
		{
			SetStatus(STATION_ERROR, HttpErrorString(n));
			Nap(RETRY_SECONDS);
			continue;
		}
		manifest[n] = 0;

		char slot[32] = "", title[64] = "", next[64] = "";
		int endsIn;
		int count = ParseManifest(manifest, slot, title, next, &endsIn);
		if(count <= 0)
		{
			SetStatus(STATION_ERROR, "the station sent something odd");
			Nap(RETRY_SECONDS);
			continue;
		}

		if(strcmp(slot, currentSlot) != 0)
		{
			const char *err = Receive(count);
			if(err)
			{
				SetStatus(STATION_ERROR, err);
				Nap(RETRY_SECONDS);
				continue;
			}
			snprintf(currentSlot, sizeof(currentSlot), "%s", slot);
		}

		LWP_MutexLock(statusMutex);
		status.state = STATION_ONAIR;
		status.error[0] = 0;
		snprintf(status.title, sizeof(status.title), "%s", title);
		snprintf(status.next, sizeof(status.next), "%s", next);
		onAirSince = fetched;	// ends_in counts from the fetch, not from a swap held back by a download
		onAirLength = endsIn;
		LWP_MutexUnlock(statusMutex);

		// a little jitter so a room full of Wiis doesn't hit the station in the same second
		int wait = endsIn - (int)(ticks_to_millisecs(diff_ticks(fetched, gettime())) / 1000) + 5 + (rand() % 10);
		Nap(wait < 20 ? 20 : wait > 600 ? 600 : wait);
	}
	return NULL;
}

void StationStart(const char *url)
{
	if(statusMutex == LWP_MUTEX_NULL)
		LWP_MutexInit(&statusMutex, false);
	if(!url[0] && stationThread == LWP_THREAD_NULL)
		return;
	snprintf(pendingUrl, sizeof(pendingUrl), "%s", url);
	urlChanged = true;
	SetStatus(STATION_TUNING, NULL);
	if(stationThread != LWP_THREAD_NULL)
	{
		StationRefresh();
		return;
	}
	LWP_CreateThread(&stationThread, StationThread, NULL, NULL, 32768, 45);
}

void StationRefresh()
{
	refresh = true;
}

void StationGetStatus(StationStatus *out)
{
	if(statusMutex == LWP_MUTEX_NULL)
	{
		memset(out, 0, sizeof(*out));
		return;
	}
	LWP_MutexLock(statusMutex);
	*out = status;
	if(status.state == STATION_ONAIR)
	{
		int left = onAirLength - (int)(ticks_to_millisecs(diff_ticks(onAirSince, gettime())) / 1000);
		out->secondsLeft = left > 0 ? left : 0;
	}
	LWP_MutexUnlock(statusMutex);
}
