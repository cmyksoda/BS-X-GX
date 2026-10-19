/****************************************************************************
 * BS-X GX
 *
 * bsxstream.h
 *
 * The satellite's channel files, held in memory for the BS-X stream registers
 ***************************************************************************/

#ifndef _BSXSTREAM_H_
#define _BSXSTREAM_H_

#include <gctypes.h>
#include <time.h>

#define BSX_STREAM_MAX_FILES	320

struct BSXStreamEntry
{
	u16 lci;
	u16 index;
	u32 offset;
	u32 size;
};

struct BSXStreamTable
{
	u32 count;
	u32 used;
	u32 capacity;
	u8 *data;
	BSXStreamEntry entries[BSX_STREAM_MAX_FILES];
};

void BSXStreamInit(u32 arenaSize);

// station side: build the next broadcast in the spare table, then swap it in
BSXStreamTable *BSXStreamBeginBuild(int waitMs);
u8 *BSXStreamAdd(BSXStreamTable *t, u16 lci, u16 index, u32 size);
void BSXStreamPublish(BSXStreamTable *t);
void BSXStreamClear();
u32 BSXStreamActivity();
bool BSXStreamReceiving(u32 ms);

// the satellite's clock: the broadcast time the station gave, running on from the tick it was true at
void BSXStreamSetClock(s64 ms, u64 at);
void BSXStreamClearClock();
bool BSXStreamClock(struct tm *out);
bool BSXStreamClockMs(s64 *ms);

// core side, emulation thread only
bool BSXStreamOpen(int which, u16 lci, u16 index, u32 *size);
int BSXStreamGet(int which);
void BSXStreamClose(int which);

#endif
