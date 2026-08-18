/****************************************************************************
 * BS-X GX
 *
 * persist.h
 *
 * Persistence of the BS-X cartridge state: the 8M memory pack (flash),
 * the 512 KB PSRAM and the 32 KB SRAM. See persist.cpp for the model.
 ***************************************************************************/

#ifndef _PERSIST_H_
#define _PERSIST_H_

#include <gctypes.h>

#define BSX_MEMPACK_SIZE	0x100000	// 8 Mbit memory pack (flash)
#define BSX_PSRAM_SIZE		0x80000		// BS-X cartridge PSRAM
#define BSX_SRAM_SIZE		0x8000		// BS-X cartridge SRAM (battery-backed)

void PersistInit();					// once at boot (main thread, after SystemInit)
bool PersistLoadAll(bool silent);	// after the BIOS is loaded: restore pack + PSRAM (+ SRAM)
void PersistResume();				// let the background writer run (call before emulation)
void PersistTick();					// once per emulated frame, from the emulation loop
void PersistFlushSync();			// pause the writer and write everything dirty right now
bool PersistDirty();				// anything not yet on the card?

#endif
