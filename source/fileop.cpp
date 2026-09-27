/****************************************************************************
 * Snes9x GX
 *
 * Daryl Borth 2008-2026
 *
 * fileop.cpp
 *
 * File operations
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ogcsys.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fat.h>
#include <sdcard/wiisd_io.h>
#include <ogc/usbstorage.h>
#include <ogc/cond.h>

#include "snes9xgx.h"
#include "fileop.h"
#include "memmanager.h"
#include "menu.h"

#define THREAD_SLEEP 100

static mutex_t saveBufferLock = LWP_MUTEX_NULL;
unsigned char *savebuffer;
u8 *ext_font_ttf = NULL;
static FILE * file; // file pointer - the only one we should ever use!
static bool unmountRequired[DEVICE_LENGTH] = { false, false, false };
static bool isMounted[DEVICE_LENGTH] = { false, false, false };

static DISC_INTERFACE* sd = &__io_wiisd;
static DISC_INTERFACE* usb = &__io_usbstorage;

static const int loadDevices[3] = { DEVICE_AUTO, DEVICE_SD, DEVICE_USB };
static const int saveDevices[3] = { DEVICE_AUTO, DEVICE_SD, DEVICE_USB };

// device thread
static lwp_t devicethread = LWP_THREAD_NULL;
static volatile bool deviceHalt = true;

// device thread synchronization
static mutex_t deviceMutex    = LWP_MUTEX_NULL;
static cond_t  deviceWakeCond = LWP_COND_NULL; // main -> device: wake / re-check halt
static cond_t  deviceHaltCond = LWP_COND_NULL; // device -> main: now halted
static bool    deviceIdle     = false;          // protected by deviceMutex

/****************************************************************************
 * ResumeDeviceThread
 *
 * Signals the device thread to start, and resumes the thread.
 ***************************************************************************/
void
ResumeDeviceThread()
{
	LWP_MutexLock(deviceMutex);
	deviceHalt = false;
	LWP_CondSignal(deviceWakeCond);
	LWP_MutexUnlock(deviceMutex);
}

/****************************************************************************
 * HaltGui
 *
 * Signals the device thread to stop.
 ***************************************************************************/
void
HaltDeviceThread()
{
	deviceHalt = true;
	LWP_MutexLock(deviceMutex);
	LWP_CondSignal(deviceWakeCond); // interrupt condvar sleep if the thread is in one
	while(!deviceIdle)
		LWP_CondWait(deviceHaltCond, deviceMutex);
	LWP_MutexUnlock(deviceMutex);
}

/****************************************************************************
 * devicecallback
 *
 * This checks our devices for changes (SD/USB removed)
 ***************************************************************************/
static void *
devicecallback (void *arg)
{
	while (1)
	{
		if(isMounted[DEVICE_SD])
		{
			if(!sd->isInserted(sd)) // check if the device was removed
			{
				unmountRequired[DEVICE_SD] = true;
				isMounted[DEVICE_SD] = false;
			}
		}

		if(isMounted[DEVICE_USB])
		{
			if(!usb->isInserted(usb)) // check if the device was removed
			{
				unmountRequired[DEVICE_USB] = true;
				isMounted[DEVICE_USB] = false;
			}
		}

		// sleep ~1 sec in 100us steps so we can react to a halt request quickly
		for(int i = 0; i < 10000 && !deviceHalt; i++)
			usleep(THREAD_SLEEP);

		// if halted, block here until ResumeDeviceThread wakes us
		if(deviceHalt)
		{
			LWP_MutexLock(deviceMutex);
			deviceIdle = true;
			LWP_CondBroadcast(deviceHaltCond); // tell HaltDeviceThread we've stopped
			while(deviceHalt)
				LWP_CondWait(deviceWakeCond, deviceMutex);
			deviceIdle = false;
			LWP_MutexUnlock(deviceMutex);
		}
	}
	return NULL;
}

/****************************************************************************
 * InitDeviceThread
 *
 * libOGC provides a nice wrapper for LWP access.
 * This function sets up a new local queue and attaches the thread to it.
 ***************************************************************************/
void
InitDeviceThread()
{
	savebuffer = (u8 *)extmem_malloc(SAVEBUFFERSIZE);
	LWP_MutexInit(&saveBufferLock, false);

	LWP_MutexInit(&deviceMutex, false);
	LWP_CondInit(&deviceWakeCond);
	LWP_CondInit(&deviceHaltCond);
	LWP_CreateThread(&devicethread, devicecallback, NULL, NULL, 0, 40);
}

/****************************************************************************
 * UnmountAllFAT
 * Unmounts all FAT devices
 ***************************************************************************/
void UnmountAllFAT()
{
	fatUnmount("sd:");
	fatUnmount("usb:");
}

/****************************************************************************
 * MountFAT
 * Checks if the device needs to be (re)mounted
 * If so, unmounts the device
 * Attempts to mount the device specified
 * Sets libfat to use the device by default
 ***************************************************************************/

static bool MountFAT(int device, int silent)
{
	bool mounted = false;
	int retry = 1;
	char name[10], name2[10];
	DISC_INTERFACE* disc = NULL;

	switch(device)
	{
		case DEVICE_SD:
			sprintf(name, "sd");
			sprintf(name2, "sd:");
			disc = sd;
			break;
		case DEVICE_USB:
			sprintf(name, "usb");
			sprintf(name2, "usb:");
			disc = usb;
			break;
		default:
			return false; // unknown device
	}

	if(unmountRequired[device])
	{
		unmountRequired[device] = false;
		fatUnmount(name2);
		disc->shutdown(disc);
		isMounted[device] = false;
	}

	while(retry)
	{
		if(fatMountSimple(name, disc))
			mounted = true;

		if(mounted || silent)
			break;

		if(device == DEVICE_SD)
			retry = ErrorPromptRetry("SD card not found!");
		else
			retry = ErrorPromptRetry("USB drive not found!");
	}

	isMounted[device] = mounted;
	return mounted;
}

void MountAllFAT()
{
	MountFAT(DEVICE_SD, SILENT);
	MountFAT(DEVICE_USB, SILENT);
}

bool FindDevice(char * filepath, int * device)
{
	if(!filepath || filepath[0] == 0)
		return false;

	if(strncmp(filepath, "sd:", 3) == 0)
	{
		*device = DEVICE_SD;
		return true;
	}
	else if(strncmp(filepath, "usb:", 4) == 0)
	{
		*device = DEVICE_USB;
		return true;
	}
	return false;
}

/****************************************************************************
 * ChangeInterface
 * Attempts to mount/configure the device specified
 ***************************************************************************/
bool ChangeInterface(int device, bool silent)
{
	if(device == DEVICE_AUTO)
		return false;

	if(isMounted[device])
		return true;

	bool mounted = false;

	switch(device)
	{
		case DEVICE_SD:
		case DEVICE_USB:
			mounted = MountFAT(device, silent);
			break;
	}

	return mounted;
}

bool ChangeInterface(char * filepath, bool silent)
{
	int device = -1;

	if(!FindDevice(filepath, &device))
		return false;

	return ChangeInterface(device, silent);
}

bool isValidLoadDevice(int device)
{
	for (int i = 0; i < 3; i++) {
		if (loadDevices[i] == device) {
			return true;
		}
	}
	return false;
}

bool isValidSaveDevice(int device)
{
	for (int i = 0; i < 3; i++) {
		if (saveDevices[i] == device) {
			return true;
		}
	}
	return false;
}

int getNextLoadDevice(int device)
{
	for (int i = 0; i < 3; i++) {
		if (loadDevices[i] == device) {
			return loadDevices[(i + 1) % 3];
		}
	}
	return DEVICE_AUTO;
}

int getNextSaveDevice(int device)
{
	for (int i = 0; i < 3; i++) {
		if (saveDevices[i] == device) {
			return saveDevices[(i + 1) % 3];
		}
	}
	return DEVICE_AUTO;
}

/****************************************************************************
* autoLoadMethod()
* Auto-determines and sets the load device
* Returns device set
****************************************************************************/
int autoLoadMethod(bool silent)
{
	if(GCSettings.LoadMethod > DEVICE_AUTO && isValidLoadDevice(GCSettings.LoadMethod)) {
		return GCSettings.LoadMethod;
	}

	char fullPath[MAXPATHLEN];
	int device = DEVICE_AUTO;

	if(!silent)
		ShowAction ("Attempting to determine load device...");

	// look for the app folder first
	for (int i = 1; i < 3; i++) {
		if (ChangeInterface(loadDevices[i], SILENT)) {
			sprintf(fullPath, "%s%s", pathPrefix[loadDevices[i]], APPFOLDER);

			if(DirExists(fullPath)) {
				device = loadDevices[i];
				break;
			}
		}
	}

	// set to first connected device instead
	if(device == DEVICE_AUTO) {
		for (int i = 1; i < 3; i++) {
			if (ChangeInterface(loadDevices[i], SILENT)) {
				device = loadDevices[i];
				break;
			}
		}
	}

	GCSettings.LoadMethod = device; // load device found for later use
	CancelAction();
	return device;
}

/****************************************************************************
* autoSaveMethod()
* Auto-determines and sets the save device
* Returns device set
****************************************************************************/
int autoSaveMethod(bool silent)
{
	if(GCSettings.SaveMethod > DEVICE_AUTO && isValidSaveDevice(GCSettings.SaveMethod)) {
		return GCSettings.SaveMethod;
	}

	char fullPath[MAXPATHLEN];
	int device = DEVICE_AUTO;

	if(!silent)
		ShowAction ("Attempting to determine save device...");

	// look for the saves folder first
	for (int i = 1; i < 3; i++) {
		if (ChangeInterface(saveDevices[i], SILENT)) {
			sprintf(fullPath, "%s%s", pathPrefix[saveDevices[i]], SAVEFOLDER);

			if(DirExists(fullPath)) {
				device = saveDevices[i];
				break;
			}
		}
	}

	// set to first connected device instead
	if(device == DEVICE_AUTO) {
		for (int i = 1; i < 3; i++) {
			if (ChangeInterface(saveDevices[i], SILENT)) {
				device = saveDevices[i];
				break;
			}
		}
	}

	GCSettings.SaveMethod = device; // save device found for later use

	if(device == DEVICE_AUTO && !silent)
		ErrorPrompt("Unable to locate a save device!");

	CancelAction();
	return device;
}

void CreateAppPath(char * origpath)
{
	if(!origpath || origpath[0] == 0)
		return;

	char * path = strdup(origpath); // make a copy so we don't mess up original

	if(!path)
		return;
	
	char * loc = strrchr(path,'/');
	if (loc != NULL)
		*loc = 0; // strip file name

	int pos = 0;

	// replace fat:/ with sd:/
	if(strncmp(path, "fat:/", 5) == 0 || strncmp(path, "sd1:/", 5) == 0)
	{
		pos++;
		path[1] = 's';
		path[2] = 'd';
	}
	if(ChangeInterface(&path[pos], SILENT))
		snprintf(appPath, MAXPATHLEN-1, "%s", &path[pos]);

	free(path);
}

bool DirExists(const char * path) {
	DIR *dir = opendir(path);
	if (dir) {
		closedir(dir);
		return true;
	}
	return false;
}

bool CreateDirectory(char * path) {
	if(DirExists(path)) {
		return true;
	}
	if(mkdir(path, 0777) != 0) {
		return false;
	}
	return true;
}

/****************************************************************************
 * AllocSaveBuffer ()
 * Clear and allocate the savebuffer
 ***************************************************************************/
void
AllocSaveBuffer ()
{
	LWP_MutexLock(saveBufferLock);
	memset (savebuffer, 0, SAVEBUFFERSIZE);
}

/****************************************************************************
 * FreeSaveBuffer ()
 * Free the savebuffer memory
 ***************************************************************************/
void
FreeSaveBuffer ()
{
	LWP_MutexUnlock(saveBufferLock);
}

/****************************************************************************
 * LoadFile
 ***************************************************************************/
size_t
LoadFile (char * rbuffer, char *filepath, size_t buffersize, bool silent)
{
	char probe[32];
	size_t size = 0, offset = 0, readsize = 0;
	int retry = 1;
	int device;

	if(!FindDevice(filepath, &device))
		return 0;

	// stop checking if devices were removed/inserted
	// since we're loading a file
	HaltDeviceThread();

	// open the file
	while(retry)
	{
		if(!ChangeInterface(device, silent))
			break;

		file = fopen (filepath, "rb");

		if(!file)
		{
			if(silent)
				break;

			retry = ErrorPromptRetry("Error opening file!");
			continue;
		}

		readsize = fread (probe, 1, sizeof(probe), file);

		if(!readsize)
		{
			unmountRequired[device] = true;
			retry = ErrorPromptRetry("Error reading file!");
			fclose (file);
			continue;
		}

		fseeko(file,0,SEEK_END);
		size = ftello(file);
		fseeko(file,0,SEEK_SET);

		if(size > buffersize) {
			size = 0;
		}
		else {
			while(!feof(file))
			{
				ShowProgress ("Loading...", offset, size);
				readsize = fread (rbuffer + offset, 1, 4096, file); // read in next chunk

				if(readsize <= 0)
					break; // reading finished (or failed)

				offset += readsize;
			}
			size = offset;
			CancelAction();
		}
		retry = 0;
		fclose (file);
	}

	// go back to checking if devices were inserted/removed
	ResumeDeviceThread();
	CancelAction();
	return size;
}

size_t LoadFile(char * filepath, bool silent)
{
	return LoadFile((char *)savebuffer, filepath, SAVEBUFFERSIZE, silent);
}

size_t LoadFont(char * filepath)
{
	FILE *file = fopen (filepath, "rb");

	if(!file) {
		ErrorPrompt("Font file not found!");
		return 0;
	}

	fseeko(file,0,SEEK_END);
	size_t loadSize = ftello(file);

	if(loadSize == 0) {
		ErrorPrompt("Error loading font!");
		return 0;
	}

	if(ext_font_ttf) {
		extmem_free(ext_font_ttf);
	}

	ext_font_ttf = (u8 *)extmem_malloc(loadSize);

	if(!ext_font_ttf) {
		ErrorPrompt("Font file is too large!");
		fclose(file);
		return 0;
	}

	fseeko(file,0,SEEK_SET);
	fread (ext_font_ttf, 1, loadSize, file);
	fclose(file);
	return loadSize;
}

/****************************************************************************
 * SaveFile
 * Write buffer to file
 ***************************************************************************/
size_t
SaveFile (char * buffer, char *filepath, size_t datasize, bool silent)
{
	size_t written = 0;
	size_t writesize, nextwrite;
	int retry = 1;
	int device;
		
	if(!FindDevice(filepath, &device))
		return 0;

	if(datasize == 0)
		return 0;

	// stop checking if devices were removed/inserted
	// since we're saving a file
	HaltDeviceThread();

	if(!silent)
		ShowAction("Saving...");

	while(!written && retry == 1)
	{
		if(!ChangeInterface(device, silent))
			break;

		file = fopen (filepath, "wb");

		if(!file)
		{
			if(silent)
				break;

			retry = ErrorPromptRetry("Error creating file!");
			continue;
		}

		while(written < datasize)
		{
			if(datasize - written > 4096) nextwrite=4096;
			else nextwrite = datasize-written;
			writesize = fwrite (buffer+written, 1, nextwrite, file);
			if(writesize != nextwrite) break; // write failure
			written += writesize;
		}
		fclose (file);

		if(written != datasize) written = 0;

		if(!written)
		{
			unmountRequired[device] = true;
			if(silent) break;
			retry = ErrorPromptRetry("Error saving file!");
		}
	}

	// go back to checking if devices were inserted/removed
	ResumeDeviceThread();
	if(!silent)
		CancelAction();
	return written;
}

size_t SaveFile(char * filepath, size_t datasize, bool silent)
{
	return SaveFile((char *)savebuffer, filepath, datasize, silent);
}
