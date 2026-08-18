/****************************************************************************
 * BS-X GX
 *
 * bsxboot.h
 *
 * Booting straight into the BS-X (Satellaview) BIOS.
 ***************************************************************************/

#ifndef _BSXBOOT_H_
#define _BSXBOOT_H_

#include <stddef.h>

// Finds BS-X.bin on the load device / SD / USB. Fills outPath on success.
bool BSXLocateBIOS(char *outPath, size_t outLen);

// Loads the BIOS as the cartridge, restores SRAM / PSRAM / memory pack.
// Emulation is ready to run afterwards (SNESROMSize > 0).
bool BSXLoadBIOS(const char *path);

#endif
