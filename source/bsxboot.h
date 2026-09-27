/****************************************************************************
 * BS-X GX
 *
 * bsxboot.h
 *
 * Finds and loads the BS-X BIOS
 ***************************************************************************/

#ifndef _BSXBOOT_H_
#define _BSXBOOT_H_

#include <stddef.h>

extern unsigned long SNESROMSize;

bool BSXLocateBIOS(char *outPath, size_t outLen);

bool BSXLoadBIOS(const char *path);

#endif
