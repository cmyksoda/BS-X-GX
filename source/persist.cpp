/****************************************************************************
 * BS-X GX
 *
 * persist.cpp
 *
 * Keeps the memory pack, PSRAM and SRAM on the card, written in the background
 ***************************************************************************/

#include <gccore.h>
#include <ogcsys.h>
#include <ogc/lwp.h>
#include <ogc/mutex.h>
#include <ogc/cond.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include "snes9xgx.h"
#include "memmanager.h"
#include "fileop.h"
#include "persist.h"

#include "snes9x/snes9x.h"
#include "snes9x/memmap.h"
#include "snes9x/bsx.h"

enum { BLOB_MEMPACK = 0, BLOB_PSRAM, BLOB_SRAM, BLOB_COUNT };

struct Blob
{
	const char *filename;
	uint8 *src;
	uint32 size;
	uint8 *staging;			// MEM2, allocated at init: the mspace isn't thread-safe
	char path[MAXPATHLEN];

	uint32 savedAdler;
	uint32 lastAdler;

	// shared with the writer, under persistMutex
	bool pending;
	bool writing;
};

static Blob blobs[BLOB_COUNT];

static lwp_t   writerThread = LWP_THREAD_NULL;
static mutex_t persistMutex = LWP_MUTEX_NULL;
static cond_t  wakeCond     = LWP_COND_NULL;
static cond_t  idleCond     = LWP_COND_NULL;
static bool    paused       = true;
static bool    busy         = false;
static bool    initialized  = false;
static bool    loaded       = false;

static uint32 lastFlashSeq   = 0;
static uint32 flashQuiet     = 0;
static uint32 pollCountdown  = 0;

#define FLASH_QUIET_FRAMES	120
#define POLL_FRAMES			300
#define WRITE_CHUNK			32768

/****************************************************************************
 * File helpers (own handles; never fileop.cpp's)
 ***************************************************************************/
static bool ReadExact(const char *path, uint8 *dst, uint32 size)
{
	FILE *f = fopen(path, "rb");
	if(!f)
		return false;

	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);

	bool ok = false;
	if(len == (long)size)
		ok = (fread(dst, 1, size, f) == size);
	fclose(f);
	return ok;
}

// libfat's rename() won't overwrite, so a power cut between unlink and rename
// leaves only the .tmp; LoadBlobFile falls back to it.
static bool WriteAtomic(const char *path, const uint8 *data, uint32 size, bool yield)
{
	char tmp[MAXPATHLEN];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);

	FILE *f = fopen(tmp, "wb");
	if(!f)
		return false;

	bool ok = true;
	uint32 off = 0;
	while(off < size)
	{
		uint32 n = size - off;
		if(n > WRITE_CHUNK) n = WRITE_CHUNK;
		if(fwrite(data + off, 1, n, f) != n) { ok = false; break; }
		off += n;
		if(yield)
			LWP_YieldThread();
	}
	if(ok)
	{
		fflush(f);
		fsync(fileno(f));
	}
	fclose(f);

	if(!ok)
	{
		unlink(tmp);
		return false;
	}
	unlink(path);
	return rename(tmp, path) == 0;
}

static bool LoadBlobFile(Blob &b)
{
	if(ReadExact(b.path, b.src, b.size))
		return true;

	char tmp[MAXPATHLEN];
	snprintf(tmp, sizeof(tmp), "%s.tmp", b.path);
	if(ReadExact(tmp, b.src, b.size))
	{
		unlink(b.path);
		rename(tmp, b.path);
		return true;
	}
	return false;
}

/****************************************************************************
 * Writer thread
 ***************************************************************************/
static void * WriterThread(void *arg)
{
	while(1)
	{
		LWP_MutexLock(persistMutex);
		int which = -1;
		while(1)
		{
			if(!paused)
			{
				for(int i = 0; i < BLOB_COUNT; i++)
					if(blobs[i].pending) { which = i; break; }
			}
			if(which >= 0)
				break;
			LWP_CondWait(wakeCond, persistMutex);
		}
		Blob &b = blobs[which];
		b.pending = false;
		b.writing = true;
		busy = true;
		LWP_MutexUnlock(persistMutex);

		bool ok = WriteAtomic(b.path, b.staging, b.size, true);

		LWP_MutexLock(persistMutex);
		b.writing = false;
		if(!ok && !b.pending)
			b.pending = true;
		busy = false;
		LWP_CondBroadcast(idleCond);
		LWP_MutexUnlock(persistMutex);

		if(!ok)
			usleep(2000000);
	}
	return NULL;
}

/****************************************************************************
 * Staging (main thread)
 ***************************************************************************/
static void Stage(Blob &b, uint32 adler)
{
	LWP_MutexLock(persistMutex);
	if(b.writing)
	{
		LWP_MutexUnlock(persistMutex);
		return;
	}
	memcpy(b.staging, b.src, b.size);
	b.pending = true;
	b.savedAdler = adler;
	LWP_CondSignal(wakeCond);
	LWP_MutexUnlock(persistMutex);
}

static uint32 Checksum(const Blob &b)
{
	return adler32(adler32(0L, Z_NULL, 0), b.src, b.size);
}

/****************************************************************************
 * Public API
 ***************************************************************************/
void PersistInit()
{
	if(initialized)
		return;

	memset(blobs, 0, sizeof(blobs));
	blobs[BLOB_MEMPACK].filename = "BS-X.mempack";
	blobs[BLOB_MEMPACK].size = BSX_MEMPACK_SIZE;
	blobs[BLOB_PSRAM].filename = "BS-X.psram";
	blobs[BLOB_PSRAM].size = BSX_PSRAM_SIZE;
	blobs[BLOB_SRAM].filename = "BS-X.srm";
	blobs[BLOB_SRAM].size = BSX_SRAM_SIZE;

	for(int i = 0; i < BLOB_COUNT; i++)
		blobs[i].staging = (uint8 *)extmem_malloc(blobs[i].size);

	LWP_MutexInit(&persistMutex, false);
	LWP_CondInit(&wakeCond);
	LWP_CondInit(&idleCond);
	LWP_CreateThread(&writerThread, WriterThread, NULL, NULL, 16384, 40);
	initialized = true;
}

static void BindMemory()
{
	blobs[BLOB_MEMPACK].src = Memory.ROM;		// the core's FlashROM for the BS-X BIOS
	blobs[BLOB_PSRAM].src   = Memory.BSRAM;
	blobs[BLOB_SRAM].src    = Memory.SRAM;

	for(int i = 0; i < BLOB_COUNT; i++)
		snprintf(blobs[i].path, MAXPATHLEN, "%s%s/%s",
			pathPrefix[GCSettings.SaveMethod], SAVEFOLDER, blobs[i].filename);
}

bool PersistLoadAll(bool silent)
{
	if(!initialized || GCSettings.SaveMethod == DEVICE_AUTO)
		return false;

	BindMemory();

	HaltDeviceThread();

	Memory.ClearSRAM();
	LoadBlobFile(blobs[BLOB_SRAM]);

	// a new pack is erased flash
	if(!LoadBlobFile(blobs[BLOB_MEMPACK]))
		memset(blobs[BLOB_MEMPACK].src, 0xFF, BSX_MEMPACK_SIZE);

	if(!LoadBlobFile(blobs[BLOB_PSRAM]))
		memset(blobs[BLOB_PSRAM].src, 0x00, BSX_PSRAM_SIZE);

	ResumeDeviceThread();

	for(int i = 0; i < BLOB_COUNT; i++)
	{
		blobs[i].savedAdler = Checksum(blobs[i]);
		blobs[i].lastAdler = blobs[i].savedAdler;
	}
	BSXFlashDirty = false;
	lastFlashSeq = BSXFlashWriteSeq;
	flashQuiet = 0;
	pollCountdown = POLL_FRAMES;
	loaded = true;
	return true;
}

void PersistResume()
{
	if(!initialized)
		return;
	LWP_MutexLock(persistMutex);
	paused = false;
	LWP_CondSignal(wakeCond);
	LWP_MutexUnlock(persistMutex);
}

void PersistTick()
{
	if(!loaded)
		return;

	if(BSXFlashWriteSeq != lastFlashSeq)
	{
		lastFlashSeq = BSXFlashWriteSeq;
		flashQuiet = 0;
	}
	else if(BSXFlashDirty && ++flashQuiet >= FLASH_QUIET_FRAMES)
	{
		BSXFlashDirty = false;
		flashQuiet = 0;
		Stage(blobs[BLOB_MEMPACK], Checksum(blobs[BLOB_MEMPACK]));
	}

	// SRAM and PSRAM have no write hooks
	if(--pollCountdown > 0)
		return;
	pollCountdown = POLL_FRAMES;

	for(int i = BLOB_PSRAM; i <= BLOB_SRAM; i++)
	{
		Blob &b = blobs[i];
		uint32 a = Checksum(b);
		if(a != b.savedAdler && a == b.lastAdler)	// wait until it stops changing
			Stage(b, a);
		b.lastAdler = a;
	}
}

void PersistFlushSync()
{
	if(!initialized)
		return;

	LWP_MutexLock(persistMutex);
	paused = true;
	while(busy)
		LWP_CondWait(idleCond, persistMutex);
	LWP_MutexUnlock(persistMutex);

	if(!loaded)
		return;

	HaltDeviceThread();
	for(int i = 0; i < BLOB_COUNT; i++)
	{
		Blob &b = blobs[i];
		uint32 a = Checksum(b);
		bool dirty = b.pending || a != b.savedAdler || (i == BLOB_MEMPACK && BSXFlashDirty);
		if(!dirty)
			continue;
		if(WriteAtomic(b.path, b.src, b.size, false))
		{
			b.savedAdler = a;
			b.lastAdler = a;
			b.pending = false;
		}
	}
	BSXFlashDirty = false;
	lastFlashSeq = BSXFlashWriteSeq;
	flashQuiet = 0;
	ResumeDeviceThread();
}
