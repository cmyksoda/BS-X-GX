/****************************************************************************
 * Snes9x GX
 *
 * softdev July 2006
 * crunchy2 May 2007
 * Michniewski 2008
 * Daryl Borth 2008-2026
 *
 * system.cpp
 *
 * Console support functions
 ***************************************************************************/

#include <gccore.h>
#include <sys/iosupport.h>

#include <wiiuse/wpad.h>

#include "system.h"
#include "video.h"
#include "audio.h"
#include "fileop.h"
#include "input.h"
#include "memmanager.h"
#include "font_ttf.h"
#include "utils/wiidrc.h"
#include "utils/FreeTypeGX.h"

extern "C" {
extern void __exception_setreload(int t);
s32 __STM_Close();
s32 __STM_Init();
}

int ShutdownRequested = 0;
int ResetRequested = 0;
static bool isWiiVC = false;

/****************************************************************************
 * USB Gecko Debugging
 ***************************************************************************/

static bool gecko = false;
static mutex_t gecko_mutex = 0;

static ssize_t __out_write(struct _reent *r, void* fd, const char *ptr, size_t len)
{
	if (!gecko || len == 0)
		return len;

	if(!ptr || len < 0)
		return -1;

	u32 level;
	LWP_MutexLock(gecko_mutex);
	level = IRQ_Disable();
	usb_sendbuffer(1, ptr, len);
	IRQ_Restore(level);
	LWP_MutexUnlock(gecko_mutex);
	return len;
}

const devoptab_t gecko_out = {
	"stdout",	// device name
	0,			// size of file structure
	NULL,		// device open
	NULL,		// device close
	__out_write,// device write
	NULL,		// device read
	NULL,		// device seek
	NULL,		// device fstat
	NULL,		// device stat
	NULL,		// device link
	NULL,		// device unlink
	NULL,		// device chdir
	NULL,		// device rename
	NULL,		// device mkdir
	0,			// dirStateSize
	NULL,		// device diropen_r
	NULL,		// device dirreset_r
	NULL,		// device dirnext_r
	NULL,		// device dirclose_r
	NULL		// device statvfs_r
};

static void USBGeckoOutput()
{
	gecko = usb_isgeckoalive(1);
	LWP_MutexInit(&gecko_mutex, false);

	devoptab_list[STD_OUT] = &gecko_out;
	devoptab_list[STD_ERR] = &gecko_out;
}

/****************************************************************************
 * Startup / Shutdown / Reboot / Exit
 ***************************************************************************/

void ShutdownCB()
{
	ShutdownRequested = 1;
}
void ResetCB()
{
	ResetRequested = 1;
}

void SystemInit() {
	L2Enhance();

	u32 ios = IOS_GetVersion();

	if(!SupportedIOS(ios))
	{
		s32 preferred = IOS_GetPreferredVersion();

		if(SupportedIOS(preferred))
			IOS_ReloadIOS(preferred);
	}

	USBGeckoOutput();
	__exception_setreload(8);

	InitMemManager();
	InitVideo();
	InitAudio();

	// Wii Power/Reset buttons
	__STM_Close();
	__STM_Init();
	SYS_SetPowerCallback(ShutdownCB);
	SYS_SetResetCallback(ResetCB);

	WiiDRC_Init();
	isWiiVC = WiiDRC_Inited();
	WPAD_Init();
	WPAD_SetPowerButtonCallback((WPADShutdownCallback)ShutdownCB);
	USBStorage_Initialize();

	SetupPads();
	InitDeviceThread();
	MountAllFAT(); // Initialize libFAT for SD and USB
	InitFreeType((u8*)font_ttf, font_ttf_size); // Initialize font system
}

static void ExitCleanup()
{
	ShutdownAudio();
	StopGX();

	HaltDeviceThread();
	UnmountAllFAT();
}

void SystemExit(int exitAction)
{
	ShutoffRumble();

	ExitCleanup();

	if(ShutdownRequested) {
		SYS_ResetSystem(SYS_POWEROFF_STANDBY, 0, FALSE);
	}
	else {
		if(exitAction == EXITACTION_WII_AUTO) // Auto
		{
			char * sig = (char *)0x80001804;
			if(
				sig[0] == 'S' &&
				sig[1] == 'T' &&
				sig[2] == 'U' &&
				sig[3] == 'B' &&
				sig[4] == 'H' &&
				sig[5] == 'A' &&
				sig[6] == 'X' &&
				sig[7] == 'X')
				exitAction = EXITACTION_WII_RETURN_TO_LOADER; // Exit to HBC
			else
				exitAction = EXITACTION_WII_RETURN_TO_MENU; // HBC not found
		}

		if(exitAction == EXITACTION_WII_RETURN_TO_MENU) // Exit to Menu
		{
			SYS_ResetSystem(SYS_RETURNTOMENU, 0, FALSE);
		}
		else if(exitAction == EXITACTION_WII_POWER_OFF) // Shutdown Wii
		{
			SYS_ResetSystem(SYS_POWEROFF_STANDBY, 0, FALSE);
		}
		else // Exit to Loader
		{
			exit(0);
		}
	}
}

typedef enum {
    CONSOLE_WII,
	CONSOLE_WIIU_VWII,
	CONSOLE_WIIU_WIIVC,
	CONSOLE_DOLPHIN
} ConsoleType;

static inline bool IsWiiU() {
	return (*(vu16*)0xCD8005A0 == 0xCAFE) || (*(vu32*)0xCD8000A0 & 0x00080000);
}

static inline bool IsDolphinEmulator() {
    s32 fd = IOS_Open("/dev/dolphin", 0);

    if (fd >= 0) {
        IOS_Close(fd);
        return true;
    }

    return false;
}
static ConsoleType GetConsoleType() {
	if (IsDolphinEmulator()) {
		return CONSOLE_DOLPHIN;
	}

	if (IsWiiU()) {
		if (isWiiVC) {
			return CONSOLE_WIIU_WIIVC;
		}
		return CONSOLE_WIIU_VWII;
	}

	return CONSOLE_WII;
}

static u32 GetCPUSpeedMHz() {
	u32 busClock = SYS_GetBusFrequency(); // ~243 MHz on Wii/vWii
	u32 multiplier = SYS_GetCoreMultiplier(); // 3x standard, 5x+ under unlocked vWii/VC

	if (busClock > 0 && multiplier > 0) {
		u64 coreClockHz = (u64)busClock * multiplier;
		return (u32)(coreClockHz / 1000000);
	}
	return 729; // Fallback
}

char * getConsoleDetails() {
    static char description[64];
    ConsoleType type = GetConsoleType();
    u32 mhz = GetCPUSpeedMHz();

    char speedStr[16];
    if (mhz >= 1000) {
        snprintf(speedStr, sizeof(speedStr), "%.2f GHz", mhz / 1000.0f);
    } else {
        snprintf(speedStr, sizeof(speedStr), "%u MHz", mhz);
    }

	switch(type) {
		case CONSOLE_WII:
			snprintf(description, sizeof(description), "Wii (%s), IOS: %d", speedStr, IOS_GetVersion());
			break;

		case CONSOLE_WIIU_VWII:
			snprintf(description, sizeof(description), "vWii (%s), IOS: %d", speedStr, IOS_GetVersion());
			break;

		case CONSOLE_WIIU_WIIVC:
			snprintf(description, sizeof(description), "Wii U VC (%s), IOS: %d", speedStr, IOS_GetVersion());
			break;

		case CONSOLE_DOLPHIN:
			snprintf(description, sizeof(description), "Dolphin Emulator");
			break;
    }

    return description;
}

char * getMemoryFreeInfo() {
    static char memoryFreeInfo[50];
    float mem1_mb = 0.0f;

    // Wii uses libogc2's malloc_wii split-heap mspace wrapper.
    // fordblks tracks the actual free memory inside the MEM1 pool.
    struct mallinfo mi = mallinfo();
    mem1_mb = (float)mi.fordblks / (1024.0f * 1024.0f);

    uint32_t mem2_bytes = SYS_GetArena2Size();
    float mem2_mb = (float)mem2_bytes / (1024.0f * 1024.0f);

    snprintf(memoryFreeInfo, sizeof(memoryFreeInfo), "MEM1 free: %.2fMB, MEM2 free: %.2fMB", mem1_mb, mem2_mb);

    return memoryFreeInfo;
}

/****************************************************************************
 * IOS Check
 ***************************************************************************/
bool SupportedIOS(u32 ios)
{
	if(IsDolphinEmulator()) {
		return true;
	}

	if(ios == 58 || ios == 61)
		return true;

	return false;
}

bool SaneIOS(u32 ios)
{
	if(IsDolphinEmulator()) {
		return true;
	}

	bool res = false;
	u32 num_titles=0;
	u32 tmd_size;

	if(ios > 200)
		return false;

	if (ES_GetNumTitles(&num_titles) < 0)
		return false;

	if(num_titles < 1)
		return false;

	u64 *titles = (u64 *)memalign(32, num_titles * sizeof(u64) + 32);

	if(!titles)
		return false;

	if (ES_GetTitles(titles, num_titles) < 0)
	{
		free(titles);
		return false;
	}

	u32 *tmdbuffer = (u32 *)memalign(32, MAX_SIGNED_TMD_SIZE);

	if(!tmdbuffer)
	{
		free(titles);
		return false;
	}

	for(u32 n=0; n < num_titles; n++)
	{
		if((titles[n] & 0xFFFFFFFF) != ios)
			continue;

		if (ES_GetStoredTMDSize(titles[n], &tmd_size) < 0)
			break;

		if (tmd_size > 4096)
			break;

		if (ES_GetStoredTMD(titles[n], (signed_blob *)tmdbuffer, tmd_size) < 0)
			break;

		if (tmdbuffer[1] || tmdbuffer[2])
		{
			res = true;
			break;
		}
	}
	free(tmdbuffer);
    free(titles);
	return res;
}
