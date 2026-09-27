/****************************************************************************
 * BS-X GX
 *
 * persist.h
 *
 * Keeps the memory pack, PSRAM and SRAM on the card
 ***************************************************************************/

#ifndef _PERSIST_H_
#define _PERSIST_H_

#include <gctypes.h>

#define BSX_MEMPACK_SIZE	0x100000
#define BSX_PSRAM_SIZE		0x80000
#define BSX_SRAM_SIZE		0x8000

void PersistInit();
bool PersistLoadAll(bool silent);
void PersistResume();
void PersistTick();
void PersistFlushSync();			// leaves the writer paused until PersistResume

#endif
