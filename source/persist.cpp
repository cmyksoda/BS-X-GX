/****************************************************************************
 * BS-X GX
 *
 * persist.cpp
 *
 * Keeps the emulated BS-X cartridge state on the SD/USB card the way real
 * hardware keeps it: the memory pack is flash (survives everything), the
 * SRAM and PSRAM are battery-backed.
 *
 * Model
 *  - Three "blobs" (pack / PSRAM / SRAM) each map a region of emulator
 *    memory to a file in the saves folder (BS-X.mempack / BS-X.psram /
 *    BS-X.srm).
 *  - The emulation thread never does SD I/O while playing. PersistTick()
 *    (called once per frame) only looks for changes: the core bumps
 *    BSXFlashWriteSeq on flash writes/erases (bsx.cpp); SRAM/PSRAM have no
 *    write hook, so they are checksummed every few seconds. Once a region
 *    has been quiet for a moment it is copied into a staging buffer and
 *    handed to a low-priority writer thread.
 *  - The writer thread owns all background SD writes (its own FILE*, never
 *    fileop.cpp's shared handle/savebuffer), writes in 32 KB pieces with
 *    yields, and replaces files atomically (tmp + rename).
 *  - PersistFlushSync() (menu / exit / power-off) parks the writer and
 *    writes anything still dirty synchronously.
 *
 * Threading rules honoured here: no extmem_malloc off the main thread
 * (the MEM2 mspace is unlocked); the device hot-plug thread is halted
 * around main-thread I/O just like fileop.cpp does.
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
	const char *filename;	// inside the saves folder
	uint8 *src;				// live emulator memory
	uint32 size;
	uint8 *staging;			// copy handed to the writer (MEM2, allocated at init)
	char path[MAXPATHLEN];

	// main-thread bookkeeping
	uint32 savedAdler;		// checksum of what is on the card
	uint32 lastAdler;		// checksum seen at the previous poll (stability check)

	// shared with the writer (protected by mutex)
	bool pending;			// staging holds data that still has to be written
	bool writing;			// writer is currently reading staging
};

static Blob blobs[BLOB_COUNT];

static lwp_t   writerThread = LWP_THREAD_NULL;
static mutex_t persistMutex = LWP_MUTEX_NULL;
static cond_t  wakeCond     = LWP_COND_NULL;	// main -> writer: work / state change
static cond_t  idleCond     = LWP_COND_NULL;	// writer -> main: went idle
static bool    paused       = true;				// writer must not touch the card
static bool    busy         = false;			// writer is inside a write
static bool    initialized  = false;
static bool    loaded       = false;			// PersistLoadAll() ran; ticks may save

// flash debounce (main thread only)
static uint32 lastFlashSeq   = 0;
static uint32 flashQuiet     = 0;
static uint32 pollCountdown  = 0;

#define FLASH_QUIET_FRAMES	120		// ~2 s after the last flash write
#define POLL_FRAMES			300		// checksum SRAM/PSRAM every ~5 s
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

// Writes tmp, then swaps it in. libfat's rename() refuses to overwrite,
// so the old file is unlinked first; on load we fall back to the .tmp
// if a power cut lands in that tiny window.
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
		// finish the interrupted swap
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
			b.pending = true;	// keep it queued; the sync flush at exit gets another go
		busy = false;
		LWP_CondBroadcast(idleCond);
		LWP_MutexUnlock(persistMutex);

		if(!ok)
			usleep(2000000);	// card trouble: don't spin
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
		// writer is still reading the previous copy; try again next tick
		LWP_MutexUnlock(persistMutex);
		return;
	}
	memcpy(b.staging, b.src, b.size);
	b.pending = true;
	b.savedAdler = adler;	// what will be on the card once the writer is done
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
	blobs[BLOB_MEMPACK].src = Memory.ROM;		// FlashROM == Memory.ROM for the BS-X BIOS
	blobs[BLOB_PSRAM].src   = Memory.BSRAM;
	blobs[BLOB_SRAM].src    = Memory.SRAM;

	for(int i = 0; i < BLOB_COUNT; i++)
		snprintf(blobs[i].path, MAXPATHLEN, "%s%s/%s",
			pathPrefix[GCSettings.SaveMethod], GCSettings.SaveFolder, blobs[i].filename);
}

bool PersistLoadAll(bool silent)
{
	if(!initialized || GCSettings.SaveMethod == DEVICE_AUTO)
		return false;

	BindMemory();

	HaltDeviceThread();

	// memory pack: a fresh 8M pack is erased flash (all 0xFF)
	if(!LoadBlobFile(blobs[BLOB_MEMPACK]))
		memset(blobs[BLOB_MEMPACK].src, 0xFF, BSX_MEMPACK_SIZE);

	if(!LoadBlobFile(blobs[BLOB_PSRAM]))
		memset(blobs[BLOB_PSRAM].src, 0x00, BSX_PSRAM_SIZE);

	// SRAM: sram.cpp already loaded it (and soft-reset) if present; nothing to do

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

	// memory pack: debounce on the core's write counter
	if(BSXFlashWriteSeq != lastFlashSeq)
	{
		lastFlashSeq = BSXFlashWriteSeq;
		flashQuiet = 0;
	}
	else if(BSXFlashDirty && ++flashQuiet >= FLASH_QUIET_FRAMES)
	{
		BSXFlashDirty = false;	// a later write sets it again
		flashQuiet = 0;
		Stage(blobs[BLOB_MEMPACK], Checksum(blobs[BLOB_MEMPACK]));
	}

	// SRAM / PSRAM: no write hooks, so poll a checksum now and then
	if(--pollCountdown > 0)
		return;
	pollCountdown = POLL_FRAMES;

	for(int i = BLOB_PSRAM; i <= BLOB_SRAM; i++)
	{
		Blob &b = blobs[i];
		uint32 a = Checksum(b);
		if(a != b.savedAdler && a == b.lastAdler)	// changed, and stable since last poll
			Stage(b, a);
		b.lastAdler = a;
	}
}

bool PersistDirty()
{
	if(!loaded)
		return false;
	if(BSXFlashDirty)
		return true;
	for(int i = 0; i < BLOB_COUNT; i++)
	{
		if(blobs[i].pending)
			return true;
		if(Checksum(blobs[i]) != blobs[i].savedAdler)
			return true;
	}
	return false;
}

void PersistFlushSync()
{
	if(!initialized)
		return;

	// park the writer
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
