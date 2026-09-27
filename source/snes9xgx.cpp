/****************************************************************************
 * BS-X GX (built on Snes9x GX)
 *
 * softdev July 2006
 * crunchy2 May 2007-July 2007
 * Michniewski 2008
 * Daryl Borth 2008-2026
 * BS-X GX changes 2026
 *
 * snes9xgx.cpp
 *
 * This file controls overall program flow. Most things start and end here!
 ***************************************************************************/

#include "snes9xgx.h"
#include "system.h"
#include "s9xsupport.h"
#include "video.h"
#include "videofilters.h"
#include "audio.h"
#include "menu.h"
#include "sram.h"
#include "freeze.h"
#include "preferences.h"
#include "fileop.h"
#include "filebrowser.h"
#include "input.h"
#include "memmanager.h"
#include "persist.h"

#include "snes9x/snes9x.h"
#include "snes9x/fxemu.h"
#include "snes9x/memmap.h"
#include "snes9x/apu/apu.h"

bool MenuRequested = false;
char appPath[1024] = { 0 };
static bool firstRun = true;

// the local Dolphin test rig links its own; release builds keep this no-op
__attribute__((weak)) void TestHookFrame() {}

int main(int argc, char *argv[])
{
	SystemInit();
	DefaultSettings (); // Set defaults
	InitializeSnes9x(); // ensure Snes9x memory is in MEM1 for Wii
	ResetVideo_Menu (); // change to menu video mode
	S9xInitSync(); // initialize frame sync
	InitGUIThreads();
	PersistInit();

#ifdef HW_RVL
	// store path app was loaded from
	if(argc > 0 && argv[0] != NULL)
		CreateAppPath(argv[0]);
#endif

	while (1) // main loop
	{
		// go back to checking if devices were inserted/removed
		// since we're entering the menu
		ResumeDeviceThread();
		SwitchAudioMode(1);
		SwitchMemoryModeMenu();

		if(SNESROMSize == 0)
			MainMenu(MENU_BSXBOOT);
		else
			MainMenu(MENU_GAME);

		if (firstRun)
		{
			firstRun = false;
			switch (GCSettings.sfxOverclock)
			{
				case 0: Settings.SuperFXSpeedPerLine = 5823405; break;
				case 1: Settings.SuperFXSpeedPerLine = 0.417 * 20.5e6; break;
				case 2: Settings.SuperFXSpeedPerLine = 0.417 * 40.5e6; break;
				case 3: Settings.SuperFXSpeedPerLine = 0.417 * 60.5e6; break;
				case 4: Settings.SuperFXSpeedPerLine = 0.417 * 80.5e6; break;
				case 5: Settings.SuperFXSpeedPerLine = 0.417 * 100.5e6; break;
				case 6: Settings.SuperFXSpeedPerLine = 0.417 * 120.5e6; break;
			}

			if (GCSettings.sfxOverclock > 0)
			S9xResetSuperFX();
			S9xReset();

			switch (GCSettings.Interpolation)
			{
			case 0: Settings.InterpolationMethod = DSP_INTERPOLATION_GAUSSIAN; break;
			case 1: Settings.InterpolationMethod = DSP_INTERPOLATION_LINEAR; break;
			case 2: Settings.InterpolationMethod = DSP_INTERPOLATION_CUBIC; break;
			case 3: Settings.InterpolationMethod = DSP_INTERPOLATION_SINC; break;
			case 4: Settings.InterpolationMethod = DSP_INTERPOLATION_NONE; break;
			}
		}
		
		MenuRequested = false;
		SwitchAudioMode(0);

		Settings.Mute = GCSettings.MuteAudio;
		Settings.SupportHiRes = (GCSettings.HiResolution == 1);
		Settings.MaxSpriteTilesPerLine = (GCSettings.SpriteLimit ? 34 : 128);
		Settings.SkipFrames = (GCSettings.FrameSkip ? AUTO_FRAMERATE : 0);
		Settings.AutoDisplayMessages = (Settings.DisplayFrameRate || Settings.DisplayTime ? true : false);
		Settings.MultiPlayer5Master = (GCSettings.Controller == CTRL_PAD4 ? true : false);
		Settings.SuperScopeMaster = (GCSettings.Controller == CTRL_SCOPE ? true : false);
		Settings.MouseMaster = (GCSettings.Controller == CTRL_MOUSE || GCSettings.Controller == CTRL_MOUSE_PORT2 || GCSettings.Controller == CTRL_MOUSE_BOTH_PORTS);
		Settings.JustifierMaster = (GCSettings.Controller == CTRL_JUST ? true : false);
		SetControllers ();

		// stop checking if devices were removed/inserted
		// since we're starting emulation again
		HaltDeviceThread();

		SwitchMemoryModeGame();
		AudioStart ();

		FrameTimer = 0;
		setFrameTimerMethod (); // set frametimer method every time a ROM is loaded

		CheckVideo = 2;		// force video update
		prevRenderedFrameCount = IPPU.RenderedFramesCount;
		SelectFilterMethod(GCSettings.videoUpscalingFilter); // Initialize / Re-evaluate active filter
		PersistResume();

		while(1) // emulation loop
		{
			S9xMainLoop ();
			ReportButtons ();
			TestHookFrame ();
			ClearButtonsReported ();
			PersistTick ();

			if(ResetRequested)
			{
				S9xSoftReset (); // reset game
				ResetRequested = 0;
			}
			if (MenuRequested)
			{
				MenuRequested = false;
				PersistFlushSync();
				SwitchMemoryModeMenu();
				TakeScreenshot();
				ResetVideo_Menu();
				break;
			}
			#ifdef HW_RVL
			if(ShutdownRequested)
				ExitApp();
			#endif
		} // emulation loop
	} // main loop
}

void ExitApp() {
	PersistFlushSync(); // before anything else can fail
	SavePrefs(SILENT);
	SystemExit(GCSettings.ExitAction, false);
}
