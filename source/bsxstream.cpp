/****************************************************************************
 * BS-X GX
 *
 * bsxstream.cpp
 *
 * The satellite's channel files, held in memory for the BS-X stream registers
 ***************************************************************************/

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/mutex.h>
#include <string.h>
#include <unistd.h>

#include "memmanager.h"
#include "bsxstream.h"

struct Stream
{
	BSXStreamTable *table;
	const u8 *data;
	u32 size;
	u32 pos;
};

static BSXStreamTable tables[2];
static BSXStreamTable *current = NULL;
static Stream streams[2];
static mutex_t streamMutex = LWP_MUTEX_NULL;
static volatile u32 activity = 0;
static volatile u64 lastDownloadActivity = 0;

// only program data comes in 32 KB data groups; the BIOS's own channels are tiny
#define DOWNLOAD_ENTRY_MIN	20000

void BSXStreamInit(u32 arenaSize)
{
	LWP_MutexInit(&streamMutex, false);
	for(int i = 0; i < 2; i++)
	{
		memset(&tables[i], 0, sizeof(BSXStreamTable));
		// main thread only: the mspace isn't locked
		tables[i].data = (u8 *)extmem_malloc(arenaSize);
		tables[i].capacity = tables[i].data ? arenaSize : 0;
	}
}

// the table the BIOS isn't reading from; streams still on it after waitMs are cut off
BSXStreamTable *BSXStreamBeginBuild(int waitMs)
{
	for(int waited = 0; ; waited += 100)
	{
		LWP_MutexLock(streamMutex);
		BSXStreamTable *t = (current == &tables[0]) ? &tables[1] : &tables[0];
		bool busy = false;
		// a fully read stream only ever returns 0xFF again, so it can let go
		for(int i = 0; i < 2; i++)
			busy |= streams[i].table == t && streams[i].pos < streams[i].size;
		if(!busy || waited >= waitMs)
		{
			for(int i = 0; i < 2; i++)
			{
				if(streams[i].table == t)
				{
					streams[i].table = NULL;
					streams[i].data = NULL;
				}
			}
			t->count = 0;
			t->used = 0;
			LWP_MutexUnlock(streamMutex);
			return t->capacity ? t : NULL;
		}
		LWP_MutexUnlock(streamMutex);
		usleep(100000);
	}
}

u8 *BSXStreamAdd(BSXStreamTable *t, u16 lci, u16 index, u32 size)
{
	u32 span = (size + 31) & ~31;
	if(t->count >= BSX_STREAM_MAX_FILES || t->used + span > t->capacity)
		return NULL;

	BSXStreamEntry &e = t->entries[t->count++];
	e.lci = lci;
	e.index = index;
	e.offset = t->used;
	e.size = size;
	t->used += span;
	return t->data + e.offset;
}

void BSXStreamPublish(BSXStreamTable *t)
{
	LWP_MutexLock(streamMutex);
	current = t;
	LWP_MutexUnlock(streamMutex);
}

void BSXStreamClear()
{
	LWP_MutexLock(streamMutex);
	current = NULL;
	LWP_MutexUnlock(streamMutex);
}

u32 BSXStreamActivity()
{
	return activity;
}

bool BSXStreamReceiving(u32 ms)
{
	for(int i = 0; i < 2; i++)
		if(streams[i].data && streams[i].size >= DOWNLOAD_ENTRY_MIN &&
		   streams[i].pos < streams[i].size)
			return true;
	u64 t = lastDownloadActivity;
	return t && ticks_to_millisecs(diff_ticks(t, gettime())) < ms;
}

bool BSXStreamOpen(int which, u16 lci, u16 index, u32 *size)
{
	bool found = false;
	LWP_MutexLock(streamMutex);
	Stream &s = streams[which];
	s.table = NULL;
	s.data = NULL;
	for(u32 i = 0; current && i < current->count; i++)
	{
		BSXStreamEntry &e = current->entries[i];
		if(e.lci == lci && e.index == index)
		{
			s.table = current;
			s.data = current->data + e.offset;
			s.size = e.size;
			s.pos = 0;
			*size = e.size;
			activity++;
			found = true;
			break;
		}
	}
	LWP_MutexUnlock(streamMutex);
	return found;
}

int BSXStreamGet(int which)
{
	Stream &s = streams[which];
	const u8 *d = s.data;
	if(!d || s.pos >= s.size)
		return -1;
	int c = d[s.pos++];
	if(s.pos == s.size && s.size >= DOWNLOAD_ENTRY_MIN)
		lastDownloadActivity = gettime();	// bridges the gap before the next data group opens
	return c;
}

void BSXStreamClose(int which)
{
	LWP_MutexLock(streamMutex);
	streams[which].table = NULL;
	streams[which].data = NULL;
	LWP_MutexUnlock(streamMutex);
}
