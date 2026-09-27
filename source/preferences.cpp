/****************************************************************************
 * Snes9x Nintendo Wii/Gamecube Port
 *
 * Tantric 2008-2023
 *
 * preferences.cpp
 *
 * Preferences save/load to XML file
 ***************************************************************************/

#include <gccore.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ogcsys.h>
#include <mxml.h>

#include "snes9xgx.h"
#include "system.h"
#include "menu.h"
#include "fileop.h"
#include "videofilters.h"
#include "video.h"
#include "input.h"
#include "button_mapping.h"

#include "snes9x/apu/apu.h"

struct SGCSettings GCSettings;

/****************************************************************************
 * Prepare Preferences Data
 *
 * This sets up the save buffer for saving.
 ***************************************************************************/
static mxml_node_t *xml = NULL;
static mxml_node_t *data = NULL;
static mxml_node_t *section = NULL;
static mxml_node_t *item = NULL;
static mxml_node_t *elem = NULL;

static char temp[200];

static const char* BtoStr(bool b)
{
    return b ? "1" : "0";
}
static const char * toStr(int i)
{
	sprintf(temp, "%d", i);
	return temp;
}

static const char * FtoStr(float i)
{
	sprintf(temp, "%.2f", i);
	return temp;
}

static void createXMLSection(const char * name, const char * description)
{
	section = mxmlNewElement(data, "section");
	mxmlElementSetAttr(section, "name", name);
	mxmlElementSetAttr(section, "description", description);
}

static void createXMLSetting(const char * name, const char * description, const char * value)
{
	item = mxmlNewElement(section, "setting");
	mxmlElementSetAttr(item, "name", name);
	mxmlElementSetAttr(item, "value", value);
	mxmlElementSetAttr(item, "description", description);
}

static void createXMLController(u32 controller[], const char * name, const char * description)
{
	item = mxmlNewElement(section, "controller");
	mxmlElementSetAttr(item, "name", name);
	mxmlElementSetAttr(item, "description", description);

	// create buttons
	for(int i=0; i < MAXJP; i++)
	{
		elem = mxmlNewElement(item, "button");
		mxmlElementSetAttr(elem, "number", toStr(i));
		mxmlElementSetAttr(elem, "assignment", toStr(controller[i]));
	}
}

static const char * XMLSaveCallback(mxml_node_t *node, int where)
{
	const char *name;

	name = mxmlGetElement(node);

	if(where == MXML_WS_BEFORE_CLOSE)
	{
		if(!strcmp(name, "file") || !strcmp(name, "section"))
			return ("\n");
		else if(!strcmp(name, "controller"))
			return ("\n\t");
	}
	if (where == MXML_WS_BEFORE_OPEN)
	{
		if(!strcmp(name, "file"))
			return ("\n");
		else if(!strcmp(name, "section"))
			return ("\n\n");
		else if(!strcmp(name, "setting") || !strcmp(name, "controller"))
			return ("\n\t");
		else if(!strcmp(name, "button"))
			return ("\n\t\t");
	}
	return (NULL);
}

static int
preparePrefsData ()
{
	xml = mxmlNewXML("1.0");
	mxmlSetWrapMargin(0); // disable line wrapping

	data = mxmlNewElement(xml, "file");
	mxmlElementSetAttr(data, "app", APPNAME);
	mxmlElementSetAttr(data, "version", APPVERSION);

	createXMLSection("File", "File Settings");

	createXMLSetting("LoadMethod", "Load Method", toStr(GCSettings.LoadMethod));
	createXMLSetting("SaveMethod", "Save Method", toStr(GCSettings.SaveMethod));
	
	createXMLSection("Network", "Network Settings");

	createXMLSetting("stationURL", "Station URL (host[:port])", GCSettings.stationURL);

	createXMLSection("Video", "Video Settings");

	createXMLSetting("videoMode", "Output Mode", toStr(GCSettings.videoMode));
	createXMLSetting("videoAspectRatioCorrection", "Aspect Ratio Correction", toStr(GCSettings.videoAspectRatioCorrection));
	createXMLSetting("videoBilinearFilter", "Bilinear Filtering", BtoStr(GCSettings.videoBilinearFilter));
	createXMLSetting("videoHardwareSoften", "Hardware Soften", toStr(GCSettings.videoHardwareSoften));
	createXMLSetting("videoScanlines", "Scanlines", BtoStr(GCSettings.videoScanlines));
	createXMLSetting("videoUpscalingFilter", "Upscaling Filter Method", toStr(GCSettings.videoUpscalingFilter));
	createXMLSetting("videoZoomHor", "Horizontal Zoom Level", FtoStr(GCSettings.videoZoomHor));
	createXMLSetting("videoZoomVert", "Vertical Zoom Level", FtoStr(GCSettings.videoZoomVert));
	createXMLSetting("videoXshift", "Horizontal Video Shift", toStr(GCSettings.videoXshift));
	createXMLSetting("videoYshift", "Vertical Video Shift", toStr(GCSettings.videoYshift));

	createXMLSection("Emulation", "Emulation Settings");

	createXMLSetting("HiResolution", "SNES Hi-Res Mode", BtoStr(GCSettings.HiResolution));
	createXMLSetting("SpriteLimit", "Sprites per-line Limit", BtoStr(GCSettings.SpriteLimit));
	createXMLSetting("FrameSkip", "Frame Skipping", BtoStr(GCSettings.FrameSkip));
	createXMLSetting("Interpolation", "Interpolation", toStr(GCSettings.Interpolation));
	createXMLSetting("MuteAudio", "Mute", BtoStr(GCSettings.MuteAudio));

	createXMLSection("Menu", "Menu Settings");

	createXMLSetting("WiimoteOrientation", "Wiimote Orientation", toStr(GCSettings.WiimoteOrientation));
	createXMLSetting("ExitAction", "Exit Action", toStr(GCSettings.ExitAction));
	createXMLSetting("MusicVolume", "Music Volume", toStr(GCSettings.MusicVolume));
	createXMLSetting("SFXVolume", "Sound Effects Volume", toStr(GCSettings.SFXVolume));
	createXMLSetting("Rumble", "Rumble", BtoStr(GCSettings.Rumble));
	createXMLSetting("language", "Language", toStr(GCSettings.language));
	
	createXMLSection("Controller", "Controller Settings");

	createXMLSetting("Controller", "Controller", toStr(GCSettings.Controller));
	createXMLSetting("TurboModeEnabled", "Turbo Mode Enabled", BtoStr(GCSettings.TurboModeEnabled));
	createXMLSetting("TurboModeButton", "Turbo Mode Button", toStr(GCSettings.TurboModeButton));
	createXMLSetting("GamepadMenuToggle", "Gamepad Menu Toggle", toStr(GCSettings.GamepadMenuToggle));
	createXMLSetting("MapABXYRightStick", "Map ABXY Right Stick", BtoStr(GCSettings.MapABXYRightStick));

	createXMLController(btnmap[CTRL_PAD][CTRLR_GCPAD], "btnmap_pad_gcpad", "SNES Pad - GameCube Controller");
	createXMLController(btnmap[CTRL_PAD][CTRLR_WIIMOTE], "btnmap_pad_wiimote", "SNES Pad - Wiimote");
	createXMLController(btnmap[CTRL_PAD][CTRLR_CLASSIC], "btnmap_pad_classic", "SNES Pad - Classic Controller");
	createXMLController(btnmap[CTRL_PAD][CTRLR_WUPC], "btnmap_pad_wupc", "SNES Pad - Wii U Pro Controller");
	createXMLController(btnmap[CTRL_PAD][CTRLR_WIIDRC], "btnmap_pad_wiidrc", "SNES Pad - Wii U Gamepad");
	createXMLController(btnmap[CTRL_PAD][CTRLR_NUNCHUK], "btnmap_pad_nunchuk", "SNES Pad - Nunchuk + Wiimote");
	createXMLController(btnmap[CTRL_MOUSE][CTRLR_GCPAD], "btnmap_mouse_gcpad", "Mouse - GameCube Controller");
	createXMLController(btnmap[CTRL_MOUSE][CTRLR_WIIMOTE], "btnmap_mouse_wiimote", "Mouse - Wiimote");
	int datasize = mxmlSaveString(xml, (char *)savebuffer, SAVEBUFFERSIZE, XMLSaveCallback);

	mxmlDelete(xml);

	return datasize;
}

/****************************************************************************
 * loadXMLSetting
 *
 * Load XML elements into variables for an individual variable
 ***************************************************************************/

static void loadXMLSetting(char * var, const char * name, int maxsize)
{
	item = mxmlFindElement(xml, xml, "setting", "name", name, MXML_DESCEND);
	if(item)
	{
		const char * tmp = mxmlElementGetAttr(item, "value");
		if(tmp)
			snprintf(var, maxsize, "%s", tmp);
	}
}
static void loadXMLSetting(bool * var, const char * name)
{
	item = mxmlFindElement(xml, xml, "setting", "name", name, MXML_DESCEND);
	if(item)
	{
		const char * tmp = mxmlElementGetAttr(item, "value");
		if(tmp) {
			if (strcmp(tmp, "1") == 0 || strcasecmp(tmp, "true") == 0)
				*var = true;
			else
				*var = false;
		}
	}
}
static void loadXMLSetting(int * var, const char * name)
{
	item = mxmlFindElement(xml, xml, "setting", "name", name, MXML_DESCEND);
	if(item)
	{
		const char * tmp = mxmlElementGetAttr(item, "value");
		if(tmp)
			*var = atoi(tmp);
	}
}
static void loadXMLSetting(float * var, const char * name)
{
	item = mxmlFindElement(xml, xml, "setting", "name", name, MXML_DESCEND);
	if(item)
	{
		const char * tmp = mxmlElementGetAttr(item, "value");
		if(tmp)
			*var = atof(tmp);
	}
}

/****************************************************************************
 * loadXMLController
 *
 * Load XML elements into variables for a controller mapping
 ***************************************************************************/

static void loadXMLController(u32 controller[], const char * name)
{
	item = mxmlFindElement(xml, xml, "controller", "name", name, MXML_DESCEND);

	if(item)
	{
		// populate buttons
		for(int i=0; i < MAXJP; i++)
		{
			elem = mxmlFindElement(item, xml, "button", "number", toStr(i), MXML_DESCEND);
			if(elem)
			{
				const char * tmp = mxmlElementGetAttr(elem, "assignment");
				if(tmp)
					controller[i] = atoi(tmp);
			}
		}
	}
}

/****************************************************************************
 * decodePrefsData
 *
 * Decodes preferences - parses XML and loads preferences into the variables
 ***************************************************************************/

static bool
decodePrefsData ()
{
	xml = mxmlLoadString(NULL, (char *)savebuffer, MXML_TEXT_CALLBACK);

	if(!xml) {
		return false;
	}

	// File Settings

	loadXMLSetting(&GCSettings.LoadMethod, "LoadMethod");
	loadXMLSetting(&GCSettings.SaveMethod, "SaveMethod");

	// Network Settings

	loadXMLSetting(GCSettings.stationURL, "stationURL", sizeof(GCSettings.stationURL));

	// Video Settings

	loadXMLSetting(&GCSettings.videoMode, "videoMode");
	loadXMLSetting(&GCSettings.videoAspectRatioCorrection, "videoAspectRatioCorrection");
	loadXMLSetting(&GCSettings.videoBilinearFilter, "videoBilinearFilter");
	loadXMLSetting(&GCSettings.videoHardwareSoften, "videoHardwareSoften");
	loadXMLSetting(&GCSettings.videoUpscalingFilter, "videoUpscalingFilter");
	loadXMLSetting(&GCSettings.videoScanlines, "videoScanlines");
	loadXMLSetting(&GCSettings.videoZoomHor, "videoZoomHor");
	loadXMLSetting(&GCSettings.videoZoomVert, "videoZoomVert");
	loadXMLSetting(&GCSettings.videoXshift, "videoXshift");
	loadXMLSetting(&GCSettings.videoYshift, "videoYshift");

	// Emulation Settings
	loadXMLSetting(&GCSettings.HiResolution, "HiResolution");
	loadXMLSetting(&GCSettings.SpriteLimit, "SpriteLimit");
	loadXMLSetting(&GCSettings.FrameSkip, "FrameSkip");
	loadXMLSetting(&GCSettings.Interpolation, "Interpolation");
	loadXMLSetting(&GCSettings.MuteAudio, "MuteAudio");

	// Menu Settings

	loadXMLSetting(&GCSettings.WiimoteOrientation, "WiimoteOrientation");
	loadXMLSetting(&GCSettings.ExitAction, "ExitAction");
	loadXMLSetting(&GCSettings.MusicVolume, "MusicVolume");
	loadXMLSetting(&GCSettings.SFXVolume, "SFXVolume");
	loadXMLSetting(&GCSettings.Rumble, "Rumble");
	loadXMLSetting(&GCSettings.language, "language");

	// Controller Settings

	loadXMLSetting(&GCSettings.Controller, "Controller");
	loadXMLSetting(&GCSettings.TurboModeEnabled, "TurboModeEnabled");
	loadXMLSetting(&GCSettings.TurboModeButton, "TurboModeButton");
	loadXMLSetting(&GCSettings.GamepadMenuToggle, "GamepadMenuToggle");
	loadXMLSetting(&GCSettings.MapABXYRightStick, "MapABXYRightStick");

	loadXMLController(btnmap[CTRL_PAD][CTRLR_GCPAD], "btnmap_pad_gcpad");
	loadXMLController(btnmap[CTRL_PAD][CTRLR_WIIMOTE], "btnmap_pad_wiimote");
	loadXMLController(btnmap[CTRL_PAD][CTRLR_CLASSIC], "btnmap_pad_classic");
	loadXMLController(btnmap[CTRL_PAD][CTRLR_WUPC], "btnmap_pad_wupc");
	loadXMLController(btnmap[CTRL_PAD][CTRLR_WIIDRC], "btnmap_pad_wiidrc");
	loadXMLController(btnmap[CTRL_PAD][CTRLR_NUNCHUK], "btnmap_pad_nunchuk");
	loadXMLController(btnmap[CTRL_MOUSE][CTRLR_GCPAD], "btnmap_mouse_gcpad");
	loadXMLController(btnmap[CTRL_MOUSE][CTRLR_WIIMOTE], "btnmap_mouse_wiimote");

	mxmlDelete(xml);
	return true;
}

/****************************************************************************
 * FixInvalidSettings
 *
 * Attempts to correct at least some invalid settings - the ones that
 * might cause crashes
 ***************************************************************************/
void FixInvalidSettings()
{
	if(!isValidLoadDevice(GCSettings.LoadMethod))
		GCSettings.LoadMethod = DEVICE_AUTO;
	if(!isValidSaveDevice(GCSettings.SaveMethod))
		GCSettings.SaveMethod = DEVICE_AUTO;

	if(!(GCSettings.videoZoomHor > 0.5 && GCSettings.videoZoomHor < 1.5))
		GCSettings.videoZoomHor = 1.0;
	if(!(GCSettings.videoZoomVert > 0.5 && GCSettings.videoZoomVert < 1.5))
		GCSettings.videoZoomVert = 1.0;
	if(!(GCSettings.videoXshift > -50 && GCSettings.videoXshift < 50))
		GCSettings.videoXshift = 0;
	if(!(GCSettings.videoYshift > -50 && GCSettings.videoYshift < 50))
		GCSettings.videoYshift = 0;
	if(!(GCSettings.MusicVolume >= 0 && GCSettings.MusicVolume <= 100))
		GCSettings.MusicVolume = 20;
	if(!(GCSettings.SFXVolume >= 0 && GCSettings.SFXVolume <= 100))
		GCSettings.SFXVolume = 40;
	if(GCSettings.language < 0 || GCSettings.language >= LANG_LENGTH)
		GCSettings.language = LANG_ENGLISH;
	if(GCSettings.Controller > CTRL_PAD4 || GCSettings.Controller < CTRL_MOUSE)
		GCSettings.Controller = CTRL_PAD2;
	if(!(GCSettings.videoHardwareSoften >= VIDEO_HW_SOFTEN_OFF && GCSettings.videoHardwareSoften < VIDEO_HW_SOFTEN_LENGTH))
		GCSettings.videoHardwareSoften = VIDEO_HW_SOFTEN_AUTO;
	if(!(GCSettings.videoAspectRatioCorrection >= VIDEO_ASPECT_RATIO_CORRECTION_NONE && GCSettings.videoAspectRatioCorrection < VIDEO_ASPECT_RATIO_CORRECTION_LENGTH))
		GCSettings.videoAspectRatioCorrection = VIDEO_ASPECT_RATIO_CORRECTION_NONE;
	if(!(GCSettings.videoMode >= VIDEOMODE_AUTO && GCSettings.videoMode < VIDEOMODE_LENGTH))
		GCSettings.videoMode = VIDEOMODE_AUTO;
	if(!(GCSettings.videoUpscalingFilter >= FILTER_NONE && GCSettings.videoUpscalingFilter <= NUM_FILTERS))
		GCSettings.videoUpscalingFilter = FILTER_NONE;
}

/****************************************************************************
 * DefaultSettings
 *
 * Sets all the defaults!
 ***************************************************************************/
void
DefaultSettings ()
{
	memset (&GCSettings, 0, sizeof (GCSettings));

	ResetControls(); // controller button mappings

	GCSettings.LoadMethod = DEVICE_AUTO;
	GCSettings.SaveMethod = DEVICE_AUTO;
	GCSettings.stationURL[0] = 0;

	GCSettings.Controller = CTRL_PAD2;

	GCSettings.videoMode = VIDEOMODE_AUTO;
	GCSettings.videoBilinearFilter = true;
	GCSettings.videoHardwareSoften = VIDEO_HW_SOFTEN_SHARP;
	GCSettings.videoScanlines = false;
	GCSettings.videoUpscalingFilter = FILTER_NONE;

	if (CONF_GetAspectRatio() == CONF_ASPECT_16_9)
		GCSettings.videoAspectRatioCorrection = VIDEO_ASPECT_RATIO_CORRECTION_16_9;
	else
		GCSettings.videoAspectRatioCorrection = VIDEO_ASPECT_RATIO_CORRECTION_NONE;

	GCSettings.videoZoomHor = 1.0; // horizontal zoom level
	GCSettings.videoZoomVert = 1.0; // vertical zoom level
	GCSettings.videoXshift = 0; // horizontal video shift
	GCSettings.videoYshift = 0; // vertical video shift

	GCSettings.WiimoteOrientation = WIIMOTE_ORIENTATION_VERTICAL;
	GCSettings.ExitAction = EXITACTION_WII_AUTO;
	GCSettings.MusicVolume = 20;
	GCSettings.SFXVolume = 40;
	GCSettings.Rumble = true;
	
	GCSettings.language = CONF_GetLanguage();

	if(GCSettings.language == LANG_TRAD_CHINESE)
		GCSettings.language = LANG_SIMP_CHINESE;

	/****************** SNES9x Settings ***********************/

	// Default ALL to false
	memset (&Settings, 0, sizeof (Settings));

	// General

	Settings.MouseMaster = false;
	Settings.MultiPlayer5Master = false;
	Settings.DontSaveOopsSnapshot = true;
	Settings.ApplyCheats = true;

	Settings.HDMATimingHack = 100;
	Settings.BlockInvalidVRAMAccessMaster = true;
	
	Settings.IsPatched = 0;

	// Sound
	Settings.SoundSync = true;
	Settings.SixteenBitSound = true;
	Settings.Stereo = true;
	Settings.ReverseStereo = true;
	Settings.SoundPlaybackRate = 48000;
	Settings.SoundInputRate = 31920;
	Settings.DynamicRateControl = true;
	Settings.SeparateEchoBuffer = false;
	GCSettings.MuteAudio = false;
	GCSettings.Interpolation = 0;
	Settings.InterpolationMethod = DSP_INTERPOLATION_GAUSSIAN;

	// Graphics
	Settings.Transparency = true;
	Settings.MaxSpriteTilesPerLine = 34;
	Settings.SkipFrames = AUTO_FRAMERATE;
	Settings.TurboSkipFrames = 19;
	Settings.AutoDisplayMessages = false;
	Settings.InitialInfoStringTimeout = 200; // # frames to display messages for
	Settings.DisplayFrameRate = false;
	Settings.DisplayTime = false;
	GCSettings.HiResolution = true; // Enabled by default
	GCSettings.SpriteLimit = true; // Enabled by default
	GCSettings.FrameSkip = true; // Enabled by default

	// Frame timings in 50hz and 60hz cpu mode
	Settings.FrameTimePAL = 20000;
	Settings.FrameTimeNTSC = 16667;

	/* Initialize Super FX CPU to normal speed by default */
	Settings.SuperFXSpeedPerLine = 5823405;
	
	Settings.SuperFXClockMultiplier = 100;
	Settings.OverclockMode = 0;
	Settings.OneClockCycle = 6;
	Settings.OneSlowClockCycle = 8;
	Settings.TwoClockCycles = 12;

	GCSettings.TurboModeEnabled = true; // Enabled by default
	GCSettings.TurboModeButton = 0; // Default is Right Analog Stick (0)
	GCSettings.GamepadMenuToggle = GAMEPAD_MENU_TOGGLE_DEFAULT;
	GCSettings.MapABXYRightStick = false;
}

/****************************************************************************
 * Save Preferences
 ***************************************************************************/
static char prefpath[MAXPATHLEN] = { 0 };

bool
SavePrefs (bool silent)
{
	char filepath[MAXPATHLEN];
	int datasize;
	int offset = 0;
	int device = DEVICE_AUTO;
	
	if(prefpath[0] != 0)
	{
		sprintf(filepath, "%s/%s", prefpath, PREF_FILE_NAME);
		FindDevice(filepath, &device);
	}
	else if(appPath[0] != 0)
	{
		sprintf(filepath, "%s/%s", appPath, PREF_FILE_NAME);
		strcpy(prefpath, appPath);
		FindDevice(filepath, &device);
	}
	else
	{
		autoSaveMethod(true);
		device = GCSettings.SaveMethod;

		if(!ChangeInterface(device, silent)) {
			return false;
		}
		
		sprintf(filepath, "%s%s", pathPrefix[device], APPFOLDER);
		if(!CreateDirectory(filepath)) {
			return false;
		}

		sprintf(filepath, "%s%s/%s", pathPrefix[device], APPFOLDER, PREF_FILE_NAME);
		sprintf(prefpath, "%s%s", pathPrefix[device], APPFOLDER);
	}
	
	if(device == DEVICE_AUTO)
		return false;

	if (!silent)
		ShowAction ("Saving preferences...");

	FixInvalidSettings();

	AllocSaveBuffer ();
	datasize = preparePrefsData ();

	offset = SaveFile(filepath, datasize, silent);

	FreeSaveBuffer ();

	CancelAction();

	if (offset > 0)
	{
		if (!silent)
			InfoPrompt("Preferences saved");

		if(appPath[0] == 0)
			strcpy(appPath, prefpath);
		return true;
	}
	return false;
}

/****************************************************************************
 * Load Preferences from specified filepath
 ***************************************************************************/
bool
LoadPrefsFromMethod (char * path)
{
	bool retval = false;
	int offset = 0;
	char filepath[MAXPATHLEN];
	sprintf(filepath, "%s/%s", path, PREF_FILE_NAME);

	AllocSaveBuffer ();

	offset = LoadFile(filepath, SILENT);

	if (offset > 0)
		retval = decodePrefsData ();

	FreeSaveBuffer ();
	
	if(retval)
	{
		strcpy(prefpath, path);

		if(appPath[0] == 0)
			strcpy(appPath, prefpath);
	}

	return retval;
}

/****************************************************************************
 * Load Preferences
 * Checks sources consecutively until we find a preference file
 ***************************************************************************/
static bool prefLoadAttempted = false;

bool LoadPrefs()
{
	if(prefLoadAttempted) // already attempted loading
		return true;

	prefLoadAttempted = true;

	bool prefFound = false;
	char filepath[5][MAXPATHLEN];
	int numDevices;

	numDevices = 5;
	sprintf(filepath[0], "%s", appPath);
	sprintf(filepath[1], "sd:/apps/%s", APPFOLDER);
	sprintf(filepath[2], "usb:/apps/%s", APPFOLDER);
	sprintf(filepath[3], "sd:/%s", APPFOLDER);
	sprintf(filepath[4], "usb:/%s", APPFOLDER);

	for(int i=0; i<numDevices; i++) {
		prefFound = LoadPrefsFromMethod(filepath[i]);

		if(prefFound)
			break;
	}

	if(!prefFound) {
		return false;
	}

	FixInvalidSettings();

	if(GCSettings.videoMode > VIDEOMODE_AUTO) {
		ResetVideo_Menu();
	}

	bg_music = (u8 * )bg_music_ogg;
	bg_music_size = bg_music_ogg_size;
	LoadBgMusic();

	ChangeLanguage();
	return true;
}

void CreateMissingDirectories() {
	char path[MAXPATHLEN];

	if (GCSettings.SaveMethod > DEVICE_AUTO && ChangeInterface(GCSettings.SaveMethod, NOTSILENT)) {
		sprintf(path, "%s%s", pathPrefix[GCSettings.SaveMethod], APPFOLDER);
		CreateDirectory(path);
		sprintf(path, "%s%s", pathPrefix[GCSettings.SaveMethod], SAVEFOLDER);
		CreateDirectory(path);
	}

	// where the BS-X BIOS goes
	if (GCSettings.LoadMethod > DEVICE_AUTO && ChangeInterface(GCSettings.LoadMethod, NOTSILENT)) {
		sprintf(path, "%s%s", pathPrefix[GCSettings.LoadMethod], APPFOLDER);
		CreateDirectory(path);
	}
}
