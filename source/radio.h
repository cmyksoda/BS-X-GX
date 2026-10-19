/****************************************************************************
 * BS-X GX
 *
 * radio.h
 *
 * SoundLink: the broadcast's radio, played from SD in step with its clock
 ***************************************************************************/

#ifndef _RADIO_H_
#define _RADIO_H_

#include <gctypes.h>

struct RadioStatus
{
	bool playing;		// a track is open and the broadcast is inside it
	bool heard;			// and the receiver's audio is on
	s32 msIn;			// where in the track the radio has reached
	s32 broadcastMs;	// where the broadcast is, by its clock
};

// station side: an Ogg Vorbis file whose second 0 is the broadcast's start (ms since 1970, Japan time), "" for none
void RadioSet(const char *path, s64 startMs);

// audio side, after S9xMixSamples: adds the radio to a buffer of 16-bit stereo frames at 48 kHz
void RadioMix(s16 *buffer, int frames);

void RadioGetStatus(RadioStatus *out);

#endif
