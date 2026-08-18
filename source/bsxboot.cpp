/****************************************************************************
 * BS-X GX
 *
 * bsxboot.cpp
 *
 * Booting straight into the BS-X (Satellaview) BIOS. The BIOS is a 1 MB
 * LoROM cartridge image; the snes9x core recognises it (S9xInitBSX,
 * Settings.BSXItself), keeps it in Memory.BIOSROM and treats
 * Memory.ROM[0..1MB) as the inserted memory pack.
 ***************************************************************************/

#include <stdio.h>
#include <string.h>

#include "snes9xgx.h"
#include "fileop.h"
#include "filebrowser.h"	// SNESROMSize
#include "sram.h"
#include "persist.h"
#include "bsxboot.h"

#include "snes9x/snes9x.h"
#include "snes9x/memmap.h"

#define BSX_BIOS_SIZE	0x100000

static const char *biosNames[] = { "BS-X.bin", "BS-X.bios", "bs-x.bin", NULL };

static bool ProbeDevice(int device, char *outPath, size_t outLen)
{
	if(!ChangeInterface(device, SILENT))
		return false;

	for(int i = 0; biosNames[i]; i++)
	{
		snprintf(outPath, outLen, "%s%s/%s", pathPrefix[device], APPFOLDER, biosNames[i]);
		FILE *f = fopen(outPath, "rb");
		if(f)
		{
			fclose(f);
			return true;
		}
	}
	return false;
}

bool BSXLocateBIOS(char *outPath, size_t outLen)
{
	int order[3];
	int n = 0;

	if(GCSettings.LoadMethod == DEVICE_SD || GCSettings.LoadMethod == DEVICE_USB)
		order[n++] = GCSettings.LoadMethod;
	if(GCSettings.LoadMethod != DEVICE_SD)
		order[n++] = DEVICE_SD;
	if(GCSettings.LoadMethod != DEVICE_USB)
		order[n++] = DEVICE_USB;

	HaltDeviceThread();
	bool found = false;
	for(int i = 0; i < n && !found; i++)
		found = ProbeDevice(order[i], outPath, outLen);
	ResumeDeviceThread();

	if(!found)
		outPath[0] = 0;
	return found;
}

bool BSXLoadBIOS(const char *path)
{
	// LoadFile() reads into savebuffer (2 MB) and transparently unzips.
	// A 512-byte copier header is tolerated.
	AllocSaveBuffer();
	size_t size = LoadFile((char *)savebuffer, (char *)path, 0, BSX_BIOS_SIZE + 512, SILENT);
	uint8 *buf = savebuffer;

	if(size == BSX_BIOS_SIZE + 512)
	{
		buf += 512;
		size = BSX_BIOS_SIZE;
	}

	bool ok = (size == BSX_BIOS_SIZE) &&
	          memcmp(buf + 0x7FC0, "Satellaview BS-X", 16) == 0;

	if(ok)
	{
		SNESROMSize = 0;
		ok = Memory.LoadROMMem(buf, size);	// memcpy into Memory.ROM + LoadROMInt + S9xReset
	}
	FreeSaveBuffer();

	if(!ok)
		return false;

	strcpy(Memory.ROMFilename, "BS-X");
	Memory.ROMFilePath[0] = 0;
	SNESROMSize = BSX_BIOS_SIZE;

	// Battery-backed SRAM (town save). sram.cpp soft-resets after loading so
	// the BIOS boots with it in place.
	if(GCSettings.SaveMethod != DEVICE_AUTO)
	{
		char srm[MAXPATHLEN];
		snprintf(srm, sizeof(srm), "%s%s/BS-X.srm", pathPrefix[GCSettings.SaveMethod], GCSettings.SaveFolder);
		LoadSRAM(srm, SILENT);
	}

	// Memory pack (flash) and PSRAM
	PersistLoadAll(SILENT);
	return true;
}
