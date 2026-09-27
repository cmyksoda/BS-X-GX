/****************************************************************************
 * Snes9x Nintendo Wii/Gamecube Port
 *
 * Tantric 2008-2023
 *
 * fileop.h
 *
 * File operations
 ****************************************************************************/

#ifndef _FILEOP_H_
#define _FILEOP_H_

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "memmanager.h"

#define SAVEBUFFERSIZE (1024 * 1024 * 2) // the BS-X BIOS, with room to spare

void InitDeviceThread();
void ResumeDeviceThread();
void HaltDeviceThread();
void MountAllFAT();
void UnmountAllFAT();
bool FindDevice(char * filepath, int * device);
bool ChangeInterface(int device, bool silent);
bool ChangeInterface(char * filepath, bool silent);
bool isValidLoadDevice(int device);
bool isValidSaveDevice(int device);
int getNextLoadDevice(int device);
int getNextSaveDevice(int device);
int autoLoadMethod(bool silent);
int autoSaveMethod(bool silent);
void CreateAppPath(char * origpath);
bool DirExists(const char * path);
bool CreateDirectory(char * path);
void AllocSaveBuffer();
void FreeSaveBuffer();
size_t LoadFile(char * rbuffer, char *filepath, size_t buffersize, bool silent);
size_t LoadFile(char * filepath, bool silent);
size_t LoadFont(char *filepath);
size_t SaveFile(char * buffer, char *filepath, size_t datasize, bool silent);
size_t SaveFile(char * filepath, size_t datasize, bool silent);

extern unsigned char *savebuffer;
extern u8 *ext_font_ttf;

#endif
