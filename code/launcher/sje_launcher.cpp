/*
===========================================================================
SerenityJediEngine2027 launcher
===========================================================================

A window like the Jedi Academy launcher, in the SerenityJediEngine blue: SINGLEPLAYER, MULTIPLAYER, SETTINGS and EXIT,
with UPDATES / MODDB / YOUTUBE below the version at the bottom left.
- MULTIPLAYER opens its own list: START MULTIPLAYER, START SERVER and BACK. START SERVER runs
  "Start SerenityJediEngine-MP-FFA-Server.bat" from the SerenityJediEngine2027 mod folder in its own console window and
  the launcher stays open (so the game can be started next, to join the server).
- SETTINGS: graphic mode, display, resolution and controller (written into the game's configs and passed as "+set"
  values on the command line), the launcher's music and its update check, and the installation check.
- UPDATES: this build's date against the newest download on the SerenityJediEngine ModDB page (read in the background
  when the launcher starts, if switched on in SETTINGS); opens the downloads page. A dot on UPDATES when ModDB has a
  newer download that hasn't been looked at yet.
- INSTALLATION CHECK: the SerenityJediEngine2027 mod folder against texts/AllowedFiles.txt (built into the exe): every
  file and folder listed there must be there, and nothing else may be (Windows' desktop.ini / Thumbs.db are ignored).
  In a RELEASE build of the launcher (NDEBUG, as the game's Sys_RequireLauncher) SINGLEPLAYER, START MULTIPLAYER and
  START SERVER only start when the check passes: otherwise the INSTALLATION CHECK page shows what is extra or missing
  and the player must fix the folder first. Debug builds show the check but always start.
It starts SerenityJediEngine2027-SP.x86_64.exe or SerenityJediEngine2027-MP.x86_64.exe from its own folder and then
closes. The options are remembered in SerenityJediEngine2027-Launcher.ini in
Documents\STARWARS SerenityJediEngine\SerenityJediEngine2027, next to the game's own configs and saves.

The background (the SP main menu art), the menu sound and the main menu music are built into the exe, see
sje_launcher.rc. The music loops while the launcher is open; the speaker button next to minimise (or SETTINGS) switches
it off (remembered in the ini).
*/

#include <algorithm> // before gdiplus.h: with NOMINMAX its headers use std::min / std::max
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <mmsystem.h>
#include <winhttp.h>

#include <cstdio>
#include <cstring>
#include <cwctype>
#include <ctime>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "resource.h"
#include "win32/AutoVersion.h"

namespace
{
	constexpr int kWidth = 1088;
	constexpr int kHeight = 607;

	constexpr int kButtonCount = 4;
	constexpr int kButtonW = 320;
	constexpr int kButtonH = 36;
	constexpr int kButtonGap = 8;
	constexpr int kButtonTop = 398;

	// what is under the mouse: the menu list's buttons are 0.., the rest these
	constexpr int kHitNone = -1;
	constexpr int kHitMinimise = 100;
	constexpr int kHitClose = 101;
	constexpr int kHitMusic = 102;
	constexpr int kHitUpdates = 110;     // the links at the bottom left
	constexpr int kHitModdb = 111;
	constexpr int kHitYoutube = 112;
	constexpr int kHitBack = 120;        // the panels' buttons
	constexpr int kHitRendPerf = 130;    // SETTINGS
	constexpr int kHitRendQuality = 131;
	constexpr int kHitFullscreen = 132;
	constexpr int kHitWindowed = 133;
	constexpr int kHitResPrev = 134;
	constexpr int kHitResNext = 135;
	constexpr int kHitController = 136;
	constexpr int kHitMusicSwitch = 137;
	constexpr int kHitUpdateSwitch = 138;
	constexpr int kHitOpenCheck = 139;
	constexpr int kHitDownloads = 150;   // UPDATES
	constexpr int kHitCheckModdb = 151;
	constexpr int kHitCheckAgain = 160;  // INSTALLATION CHECK
	constexpr int kHitOpenFolder = 161;
	constexpr int kHitStartPending = 162;
	constexpr int kHitList = 163;        // the list (wheel)

	// background music: MCI plays the MP3 (from a temp file), PlaySound the menu sound alongside
	const wchar_t* const kMusicAlias = L"sje_launcher_music";
	constexpr int kMusicVolume = 600; // 0..1000

	const wchar_t* const kSPExe = L"SerenityJediEngine2027-SP.x86_64.exe";
	const wchar_t* const kMPExe = L"SerenityJediEngine2027-MP.x86_64.exe";
	const wchar_t* const kIniName = L"SerenityJediEngine2027-Launcher.ini";
	// the mod folder (fs_game) next to the exes, and the dedicated server batch file in it (it does "cd .." itself, so it
	// runs from the mod folder)
	const wchar_t* const kModFolder = L"SerenityJediEngine2027";
	const wchar_t* const kServerBat = L"Start SerenityJediEngine-MP-FFA-Server.bat";
	// Release builds of the game only start when this is set on their process (shared/sys/sys_main.cpp
	// Sys_RequireLauncher: SJE_LAUNCHER_ENV / SJE_LAUNCHER_TOKEN); keep the two the same.
	const wchar_t* const kLauncherEnv = L"SJE_LAUNCHER";
	const wchar_t* const kLauncherToken = L"sje-launcher-4b8e2d6a";
	// the game's home folder in Documents, with its configs and saves (Sys_DefaultHomePath: "STARWARS SerenityJediEngine\"
	// + HOMEPATH_NAME_WIN)
	const wchar_t* const kHomeParent = L"STARWARS SerenityJediEngine";
	const wchar_t* const kHomeFolder = L"SerenityJediEngine2027";

	// the links
	const wchar_t* const kModdbHost = L"www.moddb.com";
	const wchar_t* const kModdbDownloadsPath = L"/mods/serenityjediengine-20/downloads";
	const wchar_t* const kModdbUrl = L"https://www.moddb.com/mods/serenityjediengine-20";
	const wchar_t* const kDownloadsUrl = L"https://www.moddb.com/mods/serenityjediengine-20/downloads";
	const wchar_t* const kYoutubeUrl = L"https://www.youtube.com/@jacesoloaris3404/videos";

	// the installation check is enforced by release builds only (as the game's Sys_RequireLauncher)
#if defined(NDEBUG) && !defined(_DEBUG)
	constexpr bool kEnforceCheck = true;
#else
	constexpr bool kEnforceCheck = false;
#endif

	// worker threads post these to the window
	constexpr UINT kMsgModdbDone = WM_APP + 1; // lparam: ModdbResult* (the window deletes it)

	// SJE colours: the blue of the menu art and the icons
	const Gdiplus::Color kTextColor(255, 177, 194, 220);  // the main menu's button text (.695 .760 .861)
	const Gdiplus::Color kTextLit(255, 235, 245, 255);
	const Gdiplus::Color kEdgeColor(150, 70, 130, 200);
	const Gdiplus::Color kEdgeLit(235, 120, 200, 255);
	const Gdiplus::Color kGlow(45, 80, 170, 255);
	const Gdiplus::Color kBodyText(255, 200, 212, 230);   // the panels' text
	const Gdiplus::Color kDimText(255, 128, 146, 172);
	const Gdiplus::Color kBlue(255, 70, 150, 235);        // on, chosen, OK
	const Gdiplus::Color kAmber(255, 235, 170, 70);       // missing, a newer update
	const Gdiplus::Color kRed(255, 235, 85, 75);          // extra, can't start

	enum class Page { Main, Settings, Multiplayer, Updates, Install };

	// the game's config files (in the mod folder of the Documents home, else of the install folder)
	const wchar_t* const kSPConfig = L"SerenityJediEngine2027-SP.cfg";
	const wchar_t* const kMPConfig = L"SerenityJediEngine2027-MP.cfg";

	// resolutions SETTINGS steps through (0 x 0 = the desktop resolution, r_mode -2)
	struct Resolution
	{
		int width;
		int height;
	};
	const Resolution kResolutions[] = { { 0, 0 },{ 1280, 720 },{ 1366, 768 },{ 1600, 900 },{ 1920, 1080 },{ 2560, 1440 },
		{ 3840, 2160 } };
	// the engine's fixed r_mode list (shared/sdl/sdl_window.cpp r_vidModes, r_mode 0..20)
	const Resolution kRModes[] = { { 320, 240 },{ 400, 300 },{ 512, 384 },{ 640, 480 },{ 800, 600 },{ 856, 480 },
		{ 960, 720 },{ 1024, 768 },{ 1152, 864 },{ 1280, 720 },{ 1280, 1024 },{ 1366, 768 },{ 1440, 900 },
		{ 1600, 900 },{ 1600, 1200 },{ 1680, 1050 },{ 1920, 1080 },{ 2048, 1536 },{ 2400, 600 },{ 2560, 1440 },
		{ 3840, 2160 } };

	struct Settings
	{
		bool rend2 = false;
		bool windowed = false;
		bool controller = true;     // in_joystick: gamepad / joystick input (the game default is on)
		bool music = true;          // launcher background music (speaker button)
		bool check_updates = true;  // read the ModDB downloads page when the launcher starts
		Resolution resolution = { 0, 0 }; // { 0, 0 } = desktop
	};

	// the installation check
	enum class Problem { Extra, Missing };
	struct CheckItem
	{
		Problem problem;
		std::wstring path;   // relative to the mod folder; a folder ends with "\"
	};
	struct CheckResult
	{
		bool done = false;
		bool no_folder = false;    // no SerenityJediEngine2027 folder next to the launcher
		bool no_list = false;      // the list is missing from the exe (a broken build)
		int allowed = 0;           // files on the list
		std::vector<CheckItem> items;
		bool Ok() const { return done && !no_folder && !no_list && items.empty(); }
	};

	// the newest download on ModDB
	struct ModdbResult
	{
		bool ok = false;
		std::wstring title;
		std::wstring date;  // YYYY-MM-DD
	};

	// a clickable region of the last frame
	struct HotRegion
	{
		RECT rect;
		int id;
	};

	struct Launcher
	{
		HINSTANCE instance = nullptr;
		HWND hwnd = nullptr;
		std::wstring folder; // where the launcher (and the game exes) are
		Settings settings;
		Page page = Page::Main;
		int hover = kHitNone;
		int pressed = kHitNone;
		bool tracking_mouse = false;
		std::vector<HotRegion> hot;

		std::wstring music_file; // the music written to %TEMP% for MCI
		bool music_open = false;

		std::unique_ptr<Gdiplus::Bitmap> background;

		// the installation check, and what to start once it passes (0 none, 1 SP, 2 MP, 3 the server)
		CheckResult check;
		int pending_start = 0;
		int list_scroll = 0;

		// updates
		bool moddb_running = false;
		bool moddb_done = false;
		ModdbResult moddb;
		std::wstring seen_update; // the ModDB date looked at on UPDATES (no dot for it any more)
	};

	Launcher g_launcher;

	// ------------------------------------------------------------------------------------------
	// Resources
	// ------------------------------------------------------------------------------------------

	// The bytes of an RCDATA resource of the exe (valid while the program runs).
	bool GetResourceBytes(const int id, const void** bytes, DWORD* size)
	{
		const HRSRC res = FindResourceW(g_launcher.instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10)); // RT_RCDATA
		const HGLOBAL data = res ? LoadResource(g_launcher.instance, res) : nullptr;
		*bytes = data ? LockResource(data) : nullptr;
		*size = res ? SizeofResource(g_launcher.instance, res) : 0;
		return *bytes && *size;
	}

	// MCI can only play music from a file: writes the resource to %TEMP%\<name>. Empty string on failure.
	std::wstring WriteResourceToTemp(const int id, const wchar_t* name)
	{
		const void* bytes;
		DWORD size;
		wchar_t temp[MAX_PATH] = {};
		if (!GetResourceBytes(id, &bytes, &size) || !GetTempPathW(MAX_PATH, temp))
		{
			return {};
		}
		const std::wstring file = std::wstring(temp) + name;
		const HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
		if (h == INVALID_HANDLE_VALUE)
		{
			return {};
		}
		DWORD written = 0;
		const bool ok = WriteFile(h, bytes, size, &written, nullptr) && written == size;
		CloseHandle(h);
		if (!ok)
		{
			DeleteFileW(file.c_str());
			return {};
		}
		return file;
	}

	bool OpenMp3(const std::wstring& file, const wchar_t* alias)
	{
		const std::wstring open = L"open \"" + file + L"\" type mpegvideo alias " + alias;
		return mciSendStringW(open.c_str(), nullptr, 0, nullptr) == 0;
	}

	void CloseMp3(const wchar_t* alias)
	{
		const std::wstring close = std::wstring(L"close ") + alias;
		mciSendStringW(close.c_str(), nullptr, 0, nullptr);
	}

	void DeleteTempFile(std::wstring& file)
	{
		if (!file.empty())
		{
			DeleteFileW(file.c_str());
			file.clear();
		}
	}

	// ------------------------------------------------------------------------------------------
	// Sound
	// ------------------------------------------------------------------------------------------

	// The menu sound (hover, click and back, like the game's menus): a new one cuts off the one still
	// playing. wait: play it to the end first (for a click right before the launcher closes).
	void PlaySfx(const bool wait = false)
	{
		const void* bytes;
		DWORD size;
		if (GetResourceBytes(IDR_SND_MENU, &bytes, &size))
		{
			PlaySoundW(static_cast<LPCWSTR>(bytes), nullptr, SND_MEMORY | SND_NODEFAULT | (wait ? SND_SYNC : SND_ASYNC));
		}
	}

	// The music loops: when a play finishes, MCI sends MM_MCINOTIFY to the window, which starts it
	// again (see WndProc).
	void PlayMusicFromStart(const HWND hwnd)
	{
		const std::wstring cmd = std::wstring(L"play ") + kMusicAlias + L" from 0 notify";
		mciSendStringW(cmd.c_str(), nullptr, 0, hwnd);
	}

	void StartMusic(const HWND hwnd)
	{
		if (g_launcher.music_open || !g_launcher.settings.music)
		{
			return;
		}
		if (g_launcher.music_file.empty())
		{
			g_launcher.music_file = WriteResourceToTemp(IDR_MUSIC, L"SerenityJediEngine2027-Launcher-music.mp3");
		}
		if (g_launcher.music_file.empty() || !OpenMp3(g_launcher.music_file, kMusicAlias))
		{
			return; // no MP3 playback on this PC: just no music
		}
		g_launcher.music_open = true;
		const std::wstring volume = std::wstring(L"setaudio ") + kMusicAlias + L" volume to " + std::to_wstring(kMusicVolume);
		mciSendStringW(volume.c_str(), nullptr, 0, nullptr);
		PlayMusicFromStart(hwnd);
	}

	void StopMusic()
	{
		if (g_launcher.music_open)
		{
			CloseMp3(kMusicAlias);
			g_launcher.music_open = false;
		}
	}

	// ------------------------------------------------------------------------------------------
	// Art
	// ------------------------------------------------------------------------------------------

	// Loads a PNG from the exe's RCDATA resources.
	std::unique_ptr<Gdiplus::Bitmap> LoadPng(const int id)
	{
		const void* bytes;
		DWORD size;
		if (!GetResourceBytes(id, &bytes, &size))
		{
			return nullptr;
		}
		IStream* stream = SHCreateMemStream(static_cast<const BYTE*>(bytes), size);
		if (!stream)
		{
			return nullptr;
		}
		// the bitmap reads from the stream while it lives: copy it so the stream can go
		std::unique_ptr<Gdiplus::Bitmap> from_stream(Gdiplus::Bitmap::FromStream(stream));
		std::unique_ptr<Gdiplus::Bitmap> copy;
		if (from_stream && from_stream->GetLastStatus() == Gdiplus::Ok)
		{
			copy.reset(from_stream->Clone(0, 0, from_stream->GetWidth(), from_stream->GetHeight(), PixelFormat32bppARGB));
		}
		from_stream.reset();
		stream->Release();
		return copy;
	}

	// ------------------------------------------------------------------------------------------
	// Settings (SerenityJediEngine2027-Launcher.ini in Documents\STARWARS SerenityJediEngine\SerenityJediEngine2027,
	// with the game's configs and saves: a game installed under Program Files can't write next to its exe)
	// ------------------------------------------------------------------------------------------

	std::wstring IniPath()
	{
		static std::wstring path;
		if (path.empty())
		{
			wchar_t docs[MAX_PATH] = {};
			if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docs)))
			{
				std::wstring dir = std::wstring(docs) + L"\\" + kHomeParent;
				CreateDirectoryW(dir.c_str(), nullptr);
				dir += L"\\" + std::wstring(kHomeFolder);
				CreateDirectoryW(dir.c_str(), nullptr);
				path = dir + L"\\" + kIniName;

				// the first time, take over the options from where older launchers kept them: My Games, or next to
				// the launcher
				const std::wstring old_inis[] = {
					std::wstring(docs) + L"\\My Games\\" + kHomeFolder + L"\\" + kIniName,
					g_launcher.folder + L"\\" + kIniName };
				for (const std::wstring& old_ini : old_inis)
				{
					if (!PathFileExistsW(path.c_str()) && PathFileExistsW(old_ini.c_str()))
					{
						CopyFileW(old_ini.c_str(), path.c_str(), TRUE);
					}
				}
			}
			else
			{
				path = g_launcher.folder + L"\\" + kIniName; // no Documents folder: next to the launcher
			}
		}
		return path;
	}

	void LoadGameConfig(); // below, with the other game config code

	void LoadSettings()
	{
		const std::wstring ini = IniPath();
		g_launcher.settings.rend2 = GetPrivateProfileIntW(L"Options", L"Rend2", 0, ini.c_str()) != 0;
		g_launcher.settings.windowed = GetPrivateProfileIntW(L"Options", L"Windowed", 0, ini.c_str()) != 0;
		g_launcher.settings.controller = GetPrivateProfileIntW(L"Options", L"Controller", 1, ini.c_str()) != 0;
		g_launcher.settings.music = GetPrivateProfileIntW(L"Options", L"Music", 1, ini.c_str()) != 0;
		g_launcher.settings.check_updates = GetPrivateProfileIntW(L"Options", L"CheckUpdates", 1, ini.c_str()) != 0;
		wchar_t seen[64] = {};
		GetPrivateProfileStringW(L"Updates", L"Seen", L"", seen, 64, ini.c_str());
		g_launcher.seen_update = seen;
		LoadGameConfig(); // renderer, display, resolution: what the game's config says
	}

	void SaveSettings()
	{
		const std::wstring ini = IniPath();
		WritePrivateProfileStringW(L"Options", L"Rend2", g_launcher.settings.rend2 ? L"1" : L"0", ini.c_str());
		WritePrivateProfileStringW(L"Options", L"Windowed", g_launcher.settings.windowed ? L"1" : L"0", ini.c_str());
		WritePrivateProfileStringW(L"Options", L"Controller", g_launcher.settings.controller ? L"1" : L"0", ini.c_str());
		WritePrivateProfileStringW(L"Options", L"Music", g_launcher.settings.music ? L"1" : L"0", ini.c_str());
		WritePrivateProfileStringW(L"Options", L"CheckUpdates", g_launcher.settings.check_updates ? L"1" : L"0", ini.c_str());
		WritePrivateProfileStringW(L"Updates", L"Seen", g_launcher.seen_update.c_str(), ini.c_str());
	}

	// ------------------------------------------------------------------------------------------
	// The game's own configs: renderer, display and resolution are read from the config the game uses (the one in
	// Documents\STARWARS SerenityJediEngine\SerenityJediEngine2027\SerenityJediEngine2027 when it exists, else the one in
	// the install's SerenityJediEngine2027 folder) and written back into both the SP and the MP config: only those
	// "seta" lines are replaced (or added), every other line stays as it was.
	// ------------------------------------------------------------------------------------------

	std::wstring HomeModFolder()
	{
		wchar_t docs[MAX_PATH] = {};
		if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docs)))
		{
			return L"";
		}
		return std::wstring(docs) + L"\\" + kHomeParent + L"\\" + kHomeFolder + L"\\" + kModFolder;
	}

	// the config file of SP or MP: Documents first, then the install folder; when neither exists, the Documents one
	// (written on save; the game reads it when it starts)
	std::wstring ConfigPath(const bool multiplayer)
	{
		const wchar_t* const name = multiplayer ? kMPConfig : kSPConfig;
		const std::wstring home_dir = HomeModFolder();
		const std::wstring home = home_dir.empty() ? L"" : home_dir + L"\\" + name;
		const std::wstring install = g_launcher.folder + L"\\" + kModFolder + L"\\" + name;
		if (!home.empty() && PathFileExistsW(home.c_str()))
		{
			return home;
		}
		if (PathFileExistsW(install.c_str()))
		{
			return install;
		}
		return home.empty() ? install : home;
	}

	std::vector<std::string> ReadLines(const std::wstring& path)
	{
		std::vector<std::string> lines;
		FILE* f = nullptr;
		if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f)
		{
			return lines;
		}
		std::string text;
		char buf[4096];
		size_t n;
		while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		{
			text.append(buf, n);
		}
		fclose(f);
		size_t start = 0;
		while (start < text.size())
		{
			size_t end = text.find('\n', start);
			if (end == std::string::npos)
			{
				end = text.size();
			}
			std::string line = text.substr(start, end - start);
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			lines.push_back(line);
			start = end + 1;
		}
		return lines;
	}

	// the cvar name of a "seta name value" / "set name value" line, else ""
	std::string CfgLineCvar(const std::string& line, std::string* value)
	{
		size_t i = line.find_first_not_of(" \t");
		if (i == std::string::npos)
		{
			return "";
		}
		size_t word_end = line.find_first_of(" \t", i);
		if (word_end == std::string::npos)
		{
			return "";
		}
		const std::string cmd = line.substr(i, word_end - i);
		if (_stricmp(cmd.c_str(), "seta") && _stricmp(cmd.c_str(), "set"))
		{
			return "";
		}
		i = line.find_first_not_of(" \t", word_end);
		if (i == std::string::npos)
		{
			return "";
		}
		word_end = line.find_first_of(" \t", i);
		const std::string name = line.substr(i, word_end == std::string::npos ? std::string::npos : word_end - i);
		if (value)
		{
			value->clear();
			if (word_end != std::string::npos)
			{
				std::string v = line.substr(line.find_first_not_of(" \t", word_end) == std::string::npos ? line.size()
					: line.find_first_not_of(" \t", word_end));
				if (v.size() >= 2 && v.front() == '"')
				{
					const size_t close = v.find('"', 1);
					v = v.substr(1, close == std::string::npos ? std::string::npos : close - 1);
				}
				*value = v;
			}
		}
		return name;
	}

	bool ReadCfgValue(const std::vector<std::string>& lines, const char* cvar, std::string& value)
	{
		bool found = false;
		for (const std::string& line : lines)
		{
			std::string v;
			if (!_stricmp(CfgLineCvar(line, &v).c_str(), cvar))
			{
				value = v; // the last one wins, as when the game execs it
				found = true;
			}
		}
		return found;
	}

	// replaces the lines of these cvars (or adds them at the end); the rest of the file is kept as it was
	void WriteCfgValues(const std::wstring& path, const std::vector<std::pair<std::string, std::string>>& values)
	{
		std::vector<std::string> lines = ReadLines(path);
		for (const auto& kv : values)
		{
			const std::string line = "seta " + kv.first + " \"" + kv.second + "\"";
			bool replaced = false;
			for (std::string& l : lines)
			{
				if (!_stricmp(CfgLineCvar(l, nullptr).c_str(), kv.first.c_str()))
				{
					l = line;
					replaced = true;
				}
			}
			if (!replaced)
			{
				// keep a last empty line (the file's final newline) last
				if (!lines.empty() && lines.back().empty())
				{
					lines.insert(lines.end() - 1, line);
				}
				else
				{
					lines.push_back(line);
				}
			}
		}
		const size_t slash = path.find_last_of(L"\\/");
		if (slash != std::wstring::npos)
		{
			SHCreateDirectoryExW(nullptr, path.substr(0, slash).c_str(), nullptr);
		}
		FILE* f = nullptr;
		if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f)
		{
			return;
		}
		for (size_t i = 0; i < lines.size(); i++)
		{
			fputs(lines[i].c_str(), f);
			if (i + 1 < lines.size() || !lines[i].empty())
			{
				fputs("\r\n", f);
			}
		}
		fclose(f);
	}

	// renderer, display and resolution from the game's config (MP's, else SP's); keeps the launcher's values when
	// there is no config yet
	void LoadGameConfig()
	{
		Settings& s = g_launcher.settings;
		std::vector<std::string> lines = ReadLines(ConfigPath(true));
		if (lines.empty())
		{
			lines = ReadLines(ConfigPath(false));
		}
		if (lines.empty())
		{
			return;
		}
		std::string v;
		if (ReadCfgValue(lines, "cl_renderer", v))
		{
			s.rend2 = v.find("rend2") != std::string::npos;
		}
		if (ReadCfgValue(lines, "r_fullscreen", v))
		{
			s.windowed = atoi(v.c_str()) == 0;
		}
		if (ReadCfgValue(lines, "r_mode", v))
		{
			const int mode = atoi(v.c_str());
			if (mode == -1)
			{
				std::string w, h;
				ReadCfgValue(lines, "r_customwidth", w);
				ReadCfgValue(lines, "r_customheight", h);
				s.resolution = { atoi(w.c_str()), atoi(h.c_str()) };
			}
			else if (mode >= 0 && mode < static_cast<int>(sizeof kRModes / sizeof kRModes[0]))
			{
				s.resolution = kRModes[mode];
			}
			else
			{
				s.resolution = { 0, 0 };
			}
			if (s.resolution.width <= 0 || s.resolution.height <= 0)
			{
				s.resolution = { 0, 0 };
			}
		}
	}

	const char* RendererName(const bool rend2, const bool multiplayer)
	{
		return multiplayer
			? (rend2 ? "SerenityJediEngine2027-rdmp-rend2" : "SerenityJediEngine2027-rdmp")
			: (rend2 ? "SerenityJediEngine2027-rdsp-rend2" : "SerenityJediEngine2027-rdsp");
	}

	// the cvars the OPTIONS page sets, for the SP or the MP config / command line
	std::vector<std::pair<std::string, std::string>> GameConfigValues(const bool multiplayer)
	{
		const Settings& s = g_launcher.settings;
		std::vector<std::pair<std::string, std::string>> values = {
			{ "cl_renderer", RendererName(s.rend2, multiplayer) },
			{ "com_rend2", s.rend2 ? "1" : "0" },
			{ "r_fullscreen", s.windowed ? "0" : "1" } };
		if (s.resolution.width > 0)
		{
			values.push_back({ "r_mode", "-1" });
			values.push_back({ "r_customwidth", std::to_string(s.resolution.width) });
			values.push_back({ "r_customheight", std::to_string(s.resolution.height) });
		}
		else
		{
			values.push_back({ "r_mode", "-2" });
		}
		return values;
	}

	void SaveGameConfig()
	{
		WriteCfgValues(ConfigPath(false), GameConfigValues(false));
		WriteCfgValues(ConfigPath(true), GameConfigValues(true));
	}

	// the next resolution of the list (from a resolution that is not in it: the first)
	void NextResolution()
	{
		Settings& s = g_launcher.settings;
		const int count = static_cast<int>(sizeof kResolutions / sizeof kResolutions[0]);
		int next = 0;
		for (int i = 0; i < count; i++)
		{
			if (kResolutions[i].width == s.resolution.width && kResolutions[i].height == s.resolution.height)
			{
				next = (i + 1) % count;
				break;
			}
		}
		s.resolution = kResolutions[next];
	}

	// the one before (from a resolution that is not in the list: the last)
	void PreviousResolution()
	{
		Settings& s = g_launcher.settings;
		const int count = static_cast<int>(sizeof kResolutions / sizeof kResolutions[0]);
		int previous = count - 1;
		for (int i = 0; i < count; i++)
		{
			if (kResolutions[i].width == s.resolution.width && kResolutions[i].height == s.resolution.height)
			{
				previous = (i + count - 1) % count;
				break;
			}
		}
		s.resolution = kResolutions[previous];
	}


	// ------------------------------------------------------------------------------------------
	// Text helpers
	// ------------------------------------------------------------------------------------------

	std::wstring FromUtf8(const std::string& text)
	{
		if (text.empty())
		{
			return {};
		}
		const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
		std::wstring out(n, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], n);
		return out;
	}

	std::wstring Lower(std::wstring text)
	{
		for (wchar_t& c : text)
		{
			c = static_cast<wchar_t>(towlower(c));
		}
		return text;
	}

	std::wstring Trim(const std::wstring& text)
	{
		const size_t a = text.find_first_not_of(L" \t\r\n");
		if (a == std::wstring::npos)
		{
			return {};
		}
		const size_t b = text.find_last_not_of(L" \t\r\n");
		return text.substr(a, b - a + 1);
	}

	// days since 1970-01-01 of a date (proleptic Gregorian)
	long long DaysFromCivil(int y, const int m, const int d)
	{
		y -= m <= 2;
		const long long era = (y >= 0 ? y : y - 399) / 400;
		const unsigned yoe = static_cast<unsigned>(y - era * 400);
		const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
		const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
		return era * 146097 + static_cast<long long>(doe) - 719468;
	}

	// "YYYY-MM-DD" -> days, -1 when it isn't one
	long long DateDays(const std::wstring& date)
	{
		int y = 0, m = 0, d = 0;
		if (swscanf_s(date.c_str(), L"%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31)
		{
			return -1;
		}
		return DaysFromCivil(y, m, d);
	}

	// "5 November 2026"
	std::wstring DateText(const std::wstring& date)
	{
		static const wchar_t* const months[] = { L"January", L"February", L"March", L"April", L"May", L"June", L"July",
			L"August", L"September", L"October", L"November", L"December" };
		int y = 0, m = 0, d = 0;
		if (swscanf_s(date.c_str(), L"%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12)
		{
			return date;
		}
		return std::to_wstring(d) + L" " + months[m - 1] + L" " + std::to_wstring(y);
	}

	// the build's date (win32/AutoVersion.h VERSION_STRING "Day-05,Month-11,Year-26,BuildNum-05") as YYYY-MM-DD, and
	// its build number
	std::wstring BuildDate(int* build = nullptr)
	{
		int d = 0, m = 0, y = 0, b = 0;
		if (sscanf_s(VERSION_STRING, "Day-%d,Month-%d,Year-%d,BuildNum-%d", &d, &m, &y, &b) < 3)
		{
			return {};
		}
		if (build)
		{
			*build = b;
		}
		wchar_t text[16];
		swprintf_s(text, L"%04d-%02d-%02d", y < 100 ? 2000 + y : y, m, d);
		return text;
	}

	// ------------------------------------------------------------------------------------------
	// The installation check: the mod folder against texts/AllowedFiles.txt
	// ------------------------------------------------------------------------------------------

	std::wstring ModFolderPath()
	{
		return g_launcher.folder + L"\\" + kModFolder;
	}

	// the list built into the exe: lower-case key -> the path as written
	bool LoadAllowedList(std::vector<std::pair<std::wstring, std::wstring>>& list)
	{
		const void* bytes;
		DWORD size;
		if (!GetResourceBytes(IDR_ALLOWED_FILES, &bytes, &size))
		{
			return false;
		}
		std::string text(static_cast<const char*>(bytes), size);
		if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0)
		{
			text.erase(0, 3); // UTF-8 BOM
		}
		const std::wstring all = FromUtf8(text);
		size_t start = 0;
		while (start <= all.size())
		{
			size_t end = all.find(L'\n', start);
			if (end == std::wstring::npos)
			{
				end = all.size();
			}
			std::wstring line = Trim(all.substr(start, end - start));
			start = end + 1;
			if (line.empty() || line[0] == L'#')
			{
				continue;
			}
			std::replace(line.begin(), line.end(), L'/', L'\\');
			while (!line.empty() && line[0] == L'\\')
			{
				line.erase(0, 1);
			}
			list.emplace_back(Lower(line), line);
		}
		return !list.empty();
	}

	// files Windows puts in folders by itself
	bool IgnoredFile(const std::wstring& name)
	{
		const std::wstring lower = Lower(name);
		return lower == L"desktop.ini" || lower == L"thumbs.db";
	}

	void WalkModFolder(const std::wstring& root, const std::wstring& rel, const std::set<std::wstring>& allowed,
		std::set<std::wstring>& seen, std::vector<CheckItem>& extra)
	{
		WIN32_FIND_DATAW fd;
		const HANDLE find = FindFirstFileW((root + L"\\" + rel + L"*").c_str(), &fd);
		if (find == INVALID_HANDLE_VALUE)
		{
			return;
		}
		do
		{
			const std::wstring name = fd.cFileName;
			if (name == L"." || name == L"..")
			{
				continue;
			}
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				const std::wstring path = rel + name + L"\\";
				if (allowed.count(Lower(path)))
				{
					seen.insert(Lower(path));
					WalkModFolder(root, path, allowed, seen, extra);
				}
				else
				{
					extra.push_back({ Problem::Extra, path }); // a whole folder that isn't SJE's
				}
			}
			else if (!IgnoredFile(name))
			{
				const std::wstring path = rel + name;
				if (allowed.count(Lower(path)))
				{
					seen.insert(Lower(path));
				}
				else
				{
					extra.push_back({ Problem::Extra, path });
				}
			}
		} while (FindNextFileW(find, &fd));
		FindClose(find);
	}

	void RunCheck()
	{
		CheckResult& r = g_launcher.check;
		r = CheckResult();
		r.done = true;
		g_launcher.list_scroll = 0;

		std::vector<std::pair<std::wstring, std::wstring>> list;
		if (!LoadAllowedList(list))
		{
			r.no_list = true;
			return;
		}
		std::set<std::wstring> allowed;
		for (const auto& entry : list)
		{
			allowed.insert(entry.first);
			if (entry.first.back() != L'\\')
			{
				r.allowed++;
			}
		}
		const std::wstring root = ModFolderPath();
		const DWORD attributes = GetFileAttributesW(root.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			r.no_folder = true;
			return;
		}
		std::set<std::wstring> seen;
		std::vector<CheckItem> extra;
		WalkModFolder(root, L"", allowed, seen, extra);
		std::sort(extra.begin(), extra.end(), [](const CheckItem& a, const CheckItem& b)
			{
				return CompareStringOrdinal(a.path.c_str(), -1, b.path.c_str(), -1, TRUE) == CSTR_LESS_THAN;
			});
		r.items = extra;

		// missing: a missing folder is reported once, not each file in it
		std::vector<std::wstring> missing_folders;
		for (const auto& entry : list)
		{
			if (seen.count(entry.first))
			{
				continue;
			}
			bool inside_missing = false;
			for (const std::wstring& folder : missing_folders)
			{
				if (entry.first.compare(0, folder.size(), folder) == 0)
				{
					inside_missing = true;
					break;
				}
			}
			if (inside_missing)
			{
				continue;
			}
			if (entry.first.back() == L'\\')
			{
				missing_folders.push_back(entry.first);
			}
			r.items.push_back({ Problem::Missing, entry.second });
		}
	}

	int CountProblems(const Problem problem)
	{
		int n = 0;
		for (const CheckItem& item : g_launcher.check.items)
		{
			n += item.problem == problem;
		}
		return n;
	}

	// ------------------------------------------------------------------------------------------
	// Updates: the newest download on the ModDB downloads page
	// ------------------------------------------------------------------------------------------

	std::string HttpGet(const wchar_t* host, const wchar_t* path)
	{
		std::string body;
		const HINTERNET session = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) SerenityJediEngine2027-Launcher",
			WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!session)
		{
			return body;
		}
		WinHttpSetTimeouts(session, 8000, 8000, 10000, 15000);
		const HINTERNET connect = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
		const HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
			WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
		if (request)
		{
			DWORD decompress = 3; // WINHTTP_DECOMPRESSION_FLAG_ALL (Windows 8.1+; ignored before)
			WinHttpSetOption(request, 118 /* WINHTTP_OPTION_DECOMPRESSION */, &decompress, sizeof decompress);
			DWORD status = 0, status_size = sizeof status;
			if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
				WinHttpReceiveResponse(request, nullptr) &&
				WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
					&status, &status_size, WINHTTP_NO_HEADER_INDEX) && status == 200)
			{
				DWORD available = 0;
				while (WinHttpQueryDataAvailable(request, &available) && available > 0 && body.size() < 8 * 1024 * 1024)
				{
					std::string chunk(available, '\0');
					DWORD read = 0;
					if (!WinHttpReadData(request, &chunk[0], available, &read) || read == 0)
					{
						break;
					}
					body.append(chunk, 0, read);
				}
			}
			WinHttpCloseHandle(request);
		}
		if (connect)
		{
			WinHttpCloseHandle(connect);
		}
		WinHttpCloseHandle(session);
		return body;
	}

	// the text of an HTML snippet: no tags, the common entities, spaces folded
	std::wstring HtmlText(const std::string& html)
	{
		std::string text;
		bool in_tag = false;
		for (const char c : html)
		{
			if (c == '<')
			{
				in_tag = true;
			}
			else if (c == '>')
			{
				in_tag = false;
			}
			else if (!in_tag)
			{
				text += c;
			}
		}
		const std::pair<const char*, const char*> entities[] = { { "&amp;", "&" },{ "&quot;", "\"" },{ "&#039;", "'" },
			{ "&#39;", "'" },{ "&lt;", "<" },{ "&gt;", ">" },{ "&nbsp;", " " } };
		for (const auto& e : entities)
		{
			size_t at;
			while ((at = text.find(e.first)) != std::string::npos)
			{
				text.replace(at, strlen(e.first), e.second);
			}
		}
		std::wstring out;
		for (const wchar_t c : FromUtf8(text))
		{
			if (iswspace(c))
			{
				if (!out.empty() && out.back() != L' ')
				{
					out += L' ';
				}
			}
			else
			{
				out += c;
			}
		}
		return Trim(out);
	}

	// Each download on the page links to /mods/serenityjediengine-20/downloads/<name> and has the date it was added as
	// datetime="YYYY-MM-DD...". The newest date wins; its title is the text of the download link before it.
	ModdbResult ParseModdbDownloads(const std::string& html)
	{
		ModdbResult result;
		const std::string link = "/mods/serenityjediengine-20/downloads/";
		size_t at = 0;
		while ((at = html.find("datetime=\"", at)) != std::string::npos)
		{
			at += 10;
			const std::wstring date = FromUtf8(html.substr(at, 10));
			if (DateDays(date) < 0 || (result.ok && date <= result.date))
			{
				continue;
			}
			// the nearest download link before this date
			const size_t from = at > 6000 ? at - 6000 : 0;
			const size_t href = html.rfind(link, at);
			if (href == std::string::npos || href < from)
			{
				continue;
			}
			const size_t open_end = html.find('>', href);
			const size_t close = open_end == std::string::npos ? std::string::npos : html.find("</a>", open_end);
			if (close == std::string::npos || close > at)
			{
				continue;
			}
			const std::wstring title = HtmlText(html.substr(open_end + 1, close - open_end - 1));
			if (title.empty())
			{
				continue;
			}
			result.ok = true;
			result.date = date;
			result.title = title;
		}
		return result;
	}

	void StartModdbCheck()
	{
		if (g_launcher.moddb_running)
		{
			return;
		}
		g_launcher.moddb_running = true;
		const HWND hwnd = g_launcher.hwnd;
		std::thread([hwnd]()
			{
				auto* result = new ModdbResult(ParseModdbDownloads(HttpGet(kModdbHost, kModdbDownloadsPath)));
				if (!PostMessageW(hwnd, kMsgModdbDone, 0, reinterpret_cast<LPARAM>(result)))
				{
					delete result;
				}
			}).detach();
	}

	// ModDB has a download added after this build (two days' grace: a release goes up a little after it is built)
	bool NewerOnModdb()
	{
		const ModdbResult& m = g_launcher.moddb;
		const long long build = DateDays(BuildDate());
		return g_launcher.moddb_done && m.ok && build >= 0 && DateDays(m.date) > build + 2;
	}

	bool UpdateDot()
	{
		return NewerOnModdb() && g_launcher.moddb.date != g_launcher.seen_update;
	}

	void OpenUrl(const wchar_t* url)
	{
		ShellExecuteW(g_launcher.hwnd, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
	}

	// ------------------------------------------------------------------------------------------
	// Layout
	// ------------------------------------------------------------------------------------------

	// the menu list on the main and multiplayer pages
	int ButtonCount()
	{
		return g_launcher.page == Page::Multiplayer ? 3 : kButtonCount;
	}

	RECT ButtonRect(const int index)
	{
		const int x = (kWidth - kButtonW) / 2;
		const int y = kButtonTop + index * (kButtonH + kButtonGap);
		return RECT{ x, y, x + kButtonW, y + kButtonH };
	}

	RECT MusicRect() { return RECT{ kWidth - 84, 8, kWidth - 64, 28 }; }
	RECT MinimiseRect() { return RECT{ kWidth - 58, 8, kWidth - 38, 28 }; }
	RECT CloseRect() { return RECT{ kWidth - 32, 8, kWidth - 12, 28 }; }

	// the panel of SETTINGS / UPDATES / INSTALLATION CHECK
	const RECT kPanel = { 84, 40, 1004, 584 };
	constexpr int kColumnLeft = 128;
	constexpr int kColumnRight = 568;
	constexpr int kColumnWidth = 392;

	RECT MakeRect(const int x, const int y, const int w, const int h)
	{
		return RECT{ x, y, x + w, y + h };
	}

	void AddHot(const RECT& r, const int id)
	{
		g_launcher.hot.push_back({ r, id });
	}

	int HitTest(const POINT pt)
	{
		// the last drawn is on top
		for (auto it = g_launcher.hot.rbegin(); it != g_launcher.hot.rend(); ++it)
		{
			if (PtInRect(&it->rect, pt))
			{
				return it->id;
			}
		}
		return kHitNone;
	}

	std::wstring ButtonLabel(const int index)
	{
		if (g_launcher.page == Page::Multiplayer)
		{
			static const wchar_t* const mp_labels[3] = { L"START MULTIPLAYER", L"START SERVER", L"BACK" };
			return mp_labels[index];
		}
		static const wchar_t* const labels[kButtonCount] = { L"SINGLEPLAYER", L"MULTIPLAYER", L"SETTINGS", L"EXIT" };
		return labels[index];
	}

	// the build date, the same as the game's (win32/AutoVersion.h)
	const wchar_t* VersionText()
	{
		return L"" VERSION_STRING;
	}

	// ------------------------------------------------------------------------------------------
	// Drawing
	// ------------------------------------------------------------------------------------------

	Gdiplus::RectF ToRectF(const RECT& r)
	{
		return Gdiplus::RectF(static_cast<Gdiplus::REAL>(r.left), static_cast<Gdiplus::REAL>(r.top),
			static_cast<Gdiplus::REAL>(r.right - r.left), static_cast<Gdiplus::REAL>(r.bottom - r.top));
	}

	void AddRoundRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& r, Gdiplus::REAL radius)
	{
		radius = (std::min)(radius, (std::min)(r.Width, r.Height) / 2.0f);
		const Gdiplus::REAL d = radius * 2;
		path.AddArc(r.X, r.Y, d, d, 180, 90);
		path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
		path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
		path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
		path.CloseFigure();
	}

	// the fonts: a Trajan-like serif as on the SJE icon lettering (Palatino Linotype ships with Windows) for the buttons
	// and headings, Segoe UI for the panels' text
	struct Fonts
	{
		std::unique_ptr<Gdiplus::FontFamily> serif, ui;
		const Gdiplus::FontFamily* Serif() const { return serif->IsAvailable() ? serif.get() : Gdiplus::FontFamily::GenericSerif(); }
		const Gdiplus::FontFamily* Ui() const { return ui->IsAvailable() ? ui.get() : Gdiplus::FontFamily::GenericSansSerif(); }
	};

	Fonts& GetFonts()
	{
		static Fonts fonts;
		if (!fonts.serif)
		{
			fonts.serif = std::make_unique<Gdiplus::FontFamily>(L"Palatino Linotype");
			fonts.ui = std::make_unique<Gdiplus::FontFamily>(L"Segoe UI");
		}
		return fonts;
	}

	// text in a box; wraps unless single_line, ellipsis at the end when it doesn't fit
	void Text(Gdiplus::Graphics& g, const std::wstring& text, const Gdiplus::Font& font, const Gdiplus::Color& color,
		const Gdiplus::RectF& box, const Gdiplus::StringAlignment align = Gdiplus::StringAlignmentNear,
		const Gdiplus::StringAlignment valign = Gdiplus::StringAlignmentNear, const bool single_line = false)
	{
		Gdiplus::StringFormat fmt;
		fmt.SetAlignment(align);
		fmt.SetLineAlignment(valign);
		fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
		if (single_line)
		{
			fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
		}
		const Gdiplus::SolidBrush brush(color);
		g.DrawString(text.c_str(), -1, &font, box, &fmt, &brush);
	}

	// a button in the style of the main menu: dark glass, blue edge, a blue saber glow on hover
	void DrawBarButton(Gdiplus::Graphics& g, const RECT& r, const std::wstring& label, const int id,
		const Gdiplus::REAL points = 14.0f, const bool accent = false)
	{
		AddHot(r, id);
		const bool lit = g_launcher.hover == id;
		const bool down = lit && g_launcher.pressed == id;
		const int shift = down ? 1 : 0;
		const Gdiplus::RectF box(static_cast<Gdiplus::REAL>(r.left) + 0.5f, static_cast<Gdiplus::REAL>(r.top + shift) + 0.5f,
			static_cast<Gdiplus::REAL>(r.right - r.left) - 1.0f, static_cast<Gdiplus::REAL>(r.bottom - r.top) - 1.0f);
		Gdiplus::GraphicsPath path;
		AddRoundRect(path, box, 7.0f);

		if (lit || accent)
		{
			for (const Gdiplus::REAL width : { 9.0f, 5.0f, 2.5f })
			{
				Gdiplus::Pen glow(lit ? kGlow : Gdiplus::Color(28, 80, 170, 255), width);
				g.DrawPath(&glow, &path);
			}
		}
		const Gdiplus::LinearGradientBrush fill(Gdiplus::RectF(box.X, box.Y - 1, box.Width, box.Height + 2),
			lit ? Gdiplus::Color(225, 22, 62, 122) : accent ? Gdiplus::Color(215, 14, 40, 84) : Gdiplus::Color(200, 10, 18, 34),
			lit ? Gdiplus::Color(225, 6, 20, 48) : Gdiplus::Color(200, 2, 6, 14),
			Gdiplus::LinearGradientModeVertical);
		g.FillPath(&fill, &path);
		Gdiplus::Pen edge(lit || accent ? kEdgeLit : kEdgeColor, lit ? 1.5f : 1.0f);
		g.DrawPath(&edge, &path);

		// long labels shrink to stay inside the bar
		Gdiplus::Font font(GetFonts().Serif(), points, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
		const Gdiplus::REAL max_width = box.Width - 30.0f;
		Gdiplus::RectF bounds;
		g.MeasureString(label.c_str(), -1, &font, Gdiplus::PointF(0, 0), &bounds);
		std::unique_ptr<Gdiplus::Font> fitted;
		const Gdiplus::Font* use_font = &font;
		if (bounds.Width > max_width)
		{
			fitted = std::make_unique<Gdiplus::Font>(GetFonts().Serif(), points * max_width / bounds.Width, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
			use_font = fitted.get();
		}
		const Gdiplus::RectF shadow_rect(box.X + 1, box.Y + 2, box.Width, box.Height);
		Text(g, label, *use_font, Gdiplus::Color(200, 0, 0, 0), shadow_rect, Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter, true);
		Text(g, label, *use_font, lit ? kTextLit : kTextColor, box, Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter, true);
	}

	// a small rounded link at the bottom left (UPDATES / MODDB / YOUTUBE), with a dot when there is something new
	int DrawPill(Gdiplus::Graphics& g, const int x, const int y, const std::wstring& label, const int id, const bool dot = false)
	{
		const Gdiplus::Font font(GetFonts().Serif(), 9.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
		Gdiplus::RectF bounds;
		g.MeasureString(label.c_str(), -1, &font, Gdiplus::PointF(0, 0), &bounds);
		const int w = static_cast<int>(bounds.Width) + 22 + (dot ? 12 : 0);
		const RECT r = MakeRect(x, y, w, 24);
		AddHot(r, id);
		const bool lit = g_launcher.hover == id;
		Gdiplus::GraphicsPath path;
		const Gdiplus::RectF box(static_cast<Gdiplus::REAL>(x) + 0.5f, static_cast<Gdiplus::REAL>(y) + 0.5f,
			static_cast<Gdiplus::REAL>(w) - 1.0f, 23.0f);
		AddRoundRect(path, box, 12.0f);
		if (lit)
		{
			for (const Gdiplus::REAL width : { 7.0f, 3.5f })
			{
				Gdiplus::Pen glow(kGlow, width);
				g.DrawPath(&glow, &path);
			}
		}
		const Gdiplus::SolidBrush fill(lit ? Gdiplus::Color(220, 20, 52, 104) : Gdiplus::Color(190, 6, 12, 26));
		g.FillPath(&fill, &path);
		Gdiplus::Pen edge(lit ? kEdgeLit : kEdgeColor, 1.0f);
		g.DrawPath(&edge, &path);
		Text(g, label, font, lit ? kTextLit : kTextColor, Gdiplus::RectF(box.X + 11, box.Y, box.Width, box.Height),
			Gdiplus::StringAlignmentNear, Gdiplus::StringAlignmentCenter, true);
		if (dot)
		{
			const Gdiplus::SolidBrush amber(kAmber);
			const Gdiplus::REAL dx = box.X + box.Width - 16.0f;
			const Gdiplus::REAL dy = box.Y + box.Height / 2 - 4.0f;
			const Gdiplus::SolidBrush halo(Gdiplus::Color(70, 235, 170, 70));
			g.FillEllipse(&halo, dx - 3, dy - 3, 14.0f, 14.0f);
			g.FillEllipse(&amber, dx, dy, 8.0f, 8.0f);
		}
		return w;
	}

	// two choices side by side; the chosen one is lit blue
	void DrawChoice(Gdiplus::Graphics& g, const RECT& r, const wchar_t* left, const wchar_t* right, const bool right_on,
		const int left_id, const int right_id)
	{
		const int half = (r.right - r.left) / 2;
		const Gdiplus::Font font(GetFonts().Serif(), 9.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
		for (int side = 0; side < 2; side++)
		{
			const RECT part = side ? RECT{ r.left + half, r.top, r.right, r.bottom } : RECT{ r.left, r.top, r.left + half, r.bottom };
			const int id = side ? right_id : left_id;
			AddHot(part, id);
			const bool on = side ? right_on : !right_on;
			const bool lit = g_launcher.hover == id;
			const Gdiplus::RectF box = ToRectF(part);
			const Gdiplus::SolidBrush fill(on ? Gdiplus::Color(235, 24, 74, 140) : lit ? Gdiplus::Color(200, 16, 34, 64) : Gdiplus::Color(170, 4, 10, 22));
			g.FillRectangle(&fill, box);
			Text(g, side ? right : left, font, on ? kTextLit : lit ? kTextColor : kDimText, box,
				Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter, true);
		}
		Gdiplus::GraphicsPath path;
		AddRoundRect(path, Gdiplus::RectF(static_cast<Gdiplus::REAL>(r.left) + 0.5f, static_cast<Gdiplus::REAL>(r.top) + 0.5f,
			static_cast<Gdiplus::REAL>(r.right - r.left) - 1.0f, static_cast<Gdiplus::REAL>(r.bottom - r.top) - 1.0f), 4.0f);
		Gdiplus::Pen edge(kEdgeColor, 1.0f);
		g.DrawPath(&edge, &path);
		Gdiplus::Pen split(kEdgeColor, 1.0f);
		g.DrawLine(&split, static_cast<INT>(r.left + half), static_cast<INT>(r.top + 1), static_cast<INT>(r.left + half), static_cast<INT>(r.bottom - 1));
	}

	// an on / off switch
	void DrawSwitch(Gdiplus::Graphics& g, const int x, const int y, const bool on, const int id)
	{
		const RECT r = MakeRect(x, y, 48, 24);
		AddHot(MakeRect(x - 6, y - 4, 60, 32), id);
		const bool lit = g_launcher.hover == id;
		Gdiplus::GraphicsPath path;
		const Gdiplus::RectF box(static_cast<Gdiplus::REAL>(r.left) + 0.5f, static_cast<Gdiplus::REAL>(r.top) + 0.5f, 47.0f, 23.0f);
		AddRoundRect(path, box, 12.0f);
		if (lit)
		{
			Gdiplus::Pen glow(kGlow, 6.0f);
			g.DrawPath(&glow, &path);
		}
		const Gdiplus::SolidBrush fill(on ? Gdiplus::Color(240, 30, 96, 176) : Gdiplus::Color(200, 8, 14, 28));
		g.FillPath(&fill, &path);
		Gdiplus::Pen edge(on || lit ? kEdgeLit : kEdgeColor, 1.0f);
		g.DrawPath(&edge, &path);
		const Gdiplus::SolidBrush knob(on ? kTextLit : kDimText);
		g.FillEllipse(&knob, on ? box.X + 27.0f : box.X + 4.0f, box.Y + 4.0f, 16.0f, 16.0f);
	}

	// a small arrow button (resolution < >)
	void DrawArrow(Gdiplus::Graphics& g, const RECT& r, const bool right, const int id)
	{
		AddHot(r, id);
		const bool lit = g_launcher.hover == id;
		const Gdiplus::RectF box = ToRectF(r);
		const Gdiplus::SolidBrush fill(lit ? Gdiplus::Color(220, 22, 62, 122) : Gdiplus::Color(170, 4, 10, 22));
		g.FillRectangle(&fill, box);
		Gdiplus::Pen pen(lit ? kTextLit : kTextColor, 2.0f);
		const Gdiplus::REAL cx = box.X + box.Width / 2, cy = box.Y + box.Height / 2;
		const Gdiplus::REAL d = right ? 3.0f : -3.0f;
		g.DrawLine(&pen, cx - d, cy - 6.0f, cx + d, cy);
		g.DrawLine(&pen, cx + d, cy, cx - d, cy + 6.0f);
	}

	// a tick (OK) or a cross (a problem), drawn: a font may not have the marks
	void DrawMark(Gdiplus::Graphics& g, const Gdiplus::REAL x, const Gdiplus::REAL y, const bool ok, const Gdiplus::Color& color)
	{
		const Gdiplus::SolidBrush ring(Gdiplus::Color(50, color.GetR(), color.GetG(), color.GetB()));
		g.FillEllipse(&ring, x, y, 22.0f, 22.0f);
		Gdiplus::Pen pen(color, 2.4f);
		pen.SetStartCap(Gdiplus::LineCapRound);
		pen.SetEndCap(Gdiplus::LineCapRound);
		if (ok)
		{
			const Gdiplus::PointF tick[] = { { x + 6.0f, y + 11.5f },{ x + 9.5f, y + 15.0f },{ x + 16.0f, y + 7.5f } };
			g.DrawLines(&pen, tick, 3);
		}
		else
		{
			g.DrawLine(&pen, x + 7.0f, y + 7.0f, x + 15.0f, y + 15.0f);
			g.DrawLine(&pen, x + 15.0f, y + 7.0f, x + 7.0f, y + 15.0f);
		}
	}

	// a panel heading: the SJE serif, a thin saber-blue rule under it
	void Heading(Gdiplus::Graphics& g, const std::wstring& text, const int x, const int y, const int w, const Gdiplus::REAL points = 11.0f)
	{
		const Gdiplus::Font font(GetFonts().Serif(), points, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
		Text(g, text, font, kTextColor, Gdiplus::RectF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y),
			static_cast<Gdiplus::REAL>(w), 24.0f), Gdiplus::StringAlignmentNear, Gdiplus::StringAlignmentNear, true);
		const Gdiplus::LinearGradientBrush rule(Gdiplus::RectF(static_cast<Gdiplus::REAL>(x), 0, static_cast<Gdiplus::REAL>(w), 1),
			Gdiplus::Color(200, 90, 160, 240), Gdiplus::Color(0, 90, 160, 240), Gdiplus::LinearGradientModeHorizontal);
		g.FillRectangle(&rule, static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + 26), static_cast<Gdiplus::REAL>(w), 1.0f);
	}

	// the art dims, and a dark glass panel with a title takes the middle (SETTINGS / UPDATES / INSTALLATION CHECK)
	void DrawPanel(Gdiplus::Graphics& g, const std::wstring& title)
	{
		const Gdiplus::SolidBrush dim(Gdiplus::Color(150, 0, 2, 8));
		g.FillRectangle(&dim, 0, 0, kWidth, kHeight);
		const Gdiplus::RectF box = ToRectF(kPanel);
		Gdiplus::GraphicsPath path;
		AddRoundRect(path, box, 12.0f);
		for (const Gdiplus::REAL width : { 12.0f, 6.0f })
		{
			Gdiplus::Pen glow(Gdiplus::Color(22, 80, 170, 255), width);
			g.DrawPath(&glow, &path);
		}
		const Gdiplus::LinearGradientBrush fill(Gdiplus::RectF(box.X, box.Y - 1, box.Width, box.Height + 2),
			Gdiplus::Color(236, 8, 18, 38), Gdiplus::Color(242, 2, 5, 12), Gdiplus::LinearGradientModeVertical);
		g.FillPath(&fill, &path);
		Gdiplus::Pen edge(kEdgeColor, 1.0f);
		g.DrawPath(&edge, &path);

		const Gdiplus::Font font(GetFonts().Serif(), 19.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
		const Gdiplus::RectF title_box(box.X, box.Y + 16.0f, box.Width, 36.0f);
		Text(g, title, font, Gdiplus::Color(200, 0, 0, 0), Gdiplus::RectF(title_box.X + 1, title_box.Y + 2, title_box.Width, title_box.Height),
			Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter, true);
		Text(g, title, font, kTextColor, title_box, Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter, true);
		const Gdiplus::REAL mid = box.X + box.Width / 2;
		const Gdiplus::LinearGradientBrush left_rule(Gdiplus::RectF(mid - 220, 0, 220, 1), Gdiplus::Color(0, 90, 160, 240),
			Gdiplus::Color(220, 110, 180, 255), Gdiplus::LinearGradientModeHorizontal);
		const Gdiplus::LinearGradientBrush right_rule(Gdiplus::RectF(mid, 0, 220, 1), Gdiplus::Color(220, 110, 180, 255),
			Gdiplus::Color(0, 90, 160, 240), Gdiplus::LinearGradientModeHorizontal);
		g.FillRectangle(&left_rule, mid - 220, box.Y + 56.0f, 220.0f, 1.0f);
		g.FillRectangle(&right_rule, mid, box.Y + 56.0f, 220.0f, 1.0f);
	}

	void DrawBack(Gdiplus::Graphics& g)
	{
		DrawBarButton(g, MakeRect((kWidth - 220) / 2, kPanel.bottom - 54, 220, 34), L"BACK", kHitBack);
	}

	// a label on the left of a settings row
	void RowLabel(Gdiplus::Graphics& g, const std::wstring& text, const int x, const int y, const int w, const int h = 30)
	{
		const Gdiplus::Font font(GetFonts().Ui(), 10.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
		Text(g, text, font, kBodyText, Gdiplus::RectF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y),
			static_cast<Gdiplus::REAL>(w), static_cast<Gdiplus::REAL>(h)), Gdiplus::StringAlignmentNear, Gdiplus::StringAlignmentCenter);
	}

	void DrawSettings(Gdiplus::Graphics& g)
	{
		const Settings& s = g_launcher.settings;
		DrawPanel(g, L"SETTINGS");
		const int y0 = kPanel.top + 82;

		// the game: written into the game's configs and passed on the command line
		Heading(g, L"GAME", kColumnLeft, y0, kColumnWidth);
		constexpr int control_x = kColumnLeft + 150;
		constexpr int control_w = kColumnWidth - 150;
		RowLabel(g, L"Graphic mode", kColumnLeft, y0 + 44, 150);
		DrawChoice(g, MakeRect(control_x, y0 + 44, control_w, 30), L"PERFORMANCE", L"QUALITY", s.rend2, kHitRendPerf, kHitRendQuality);
		RowLabel(g, L"Display", kColumnLeft, y0 + 92, 150);
		DrawChoice(g, MakeRect(control_x, y0 + 92, control_w, 30), L"FULLSCREEN", L"WINDOWED", s.windowed, kHitFullscreen, kHitWindowed);
		RowLabel(g, L"Resolution", kColumnLeft, y0 + 140, 150);
		DrawArrow(g, MakeRect(control_x, y0 + 140, 32, 30), false, kHitResPrev);
		DrawArrow(g, MakeRect(control_x + control_w - 32, y0 + 140, 32, 30), true, kHitResNext);
		{
			const RECT mid = MakeRect(control_x + 32, y0 + 140, control_w - 64, 30);
			const Gdiplus::SolidBrush fill(Gdiplus::Color(170, 4, 10, 22));
			g.FillRectangle(&fill, ToRectF(mid));
			Gdiplus::Pen edge(kEdgeColor, 1.0f);
			g.DrawRectangle(&edge, ToRectF(MakeRect(control_x, y0 + 140, control_w - 1, 29)));
			const Gdiplus::Font font(GetFonts().Serif(), 10.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
			const std::wstring text = s.resolution.width > 0
				? std::to_wstring(s.resolution.width) + L" x " + std::to_wstring(s.resolution.height) : std::wstring(L"DESKTOP");
			Text(g, text, font, kTextLit, ToRectF(mid), Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter, true);
		}
		RowLabel(g, L"Controller (gamepad / joystick)", kColumnLeft, y0 + 188, 300);
		DrawSwitch(g, kColumnLeft + kColumnWidth - 48, y0 + 191, s.controller, kHitController);
		{
			const Gdiplus::Font note(GetFonts().Ui(), 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
			Text(g, L"Saved into the game's SP and MP configs. Quality uses the Rend2 renderer.", note, kDimText,
				Gdiplus::RectF(static_cast<Gdiplus::REAL>(kColumnLeft), static_cast<Gdiplus::REAL>(y0 + 236),
					static_cast<Gdiplus::REAL>(kColumnWidth), 40.0f));
		}

		// the launcher
		Heading(g, L"LAUNCHER", kColumnRight, y0, kColumnWidth);
		RowLabel(g, L"Music in the launcher", kColumnRight, y0 + 44, 300);
		DrawSwitch(g, kColumnRight + kColumnWidth - 48, y0 + 47, s.music, kHitMusicSwitch);
		RowLabel(g, L"Look for updates on ModDB when the launcher starts", kColumnRight, y0 + 86, 300, 44);
		DrawSwitch(g, kColumnRight + kColumnWidth - 48, y0 + 96, s.check_updates, kHitUpdateSwitch);

		// the installation
		Heading(g, L"INSTALLATION", kColumnRight, y0 + 150, kColumnWidth);
		const CheckResult& c = g_launcher.check;
		const bool ok = c.Ok();
		std::wstring status;
		if (!c.done)
		{
			status = L"Not checked yet.";
		}
		else if (c.no_folder)
		{
			status = L"The SerenityJediEngine2027 folder is not next to the launcher.";
		}
		else if (ok)
		{
			status = L"All the mod's files are in place, nothing extra.";
		}
		else
		{
			const int extra = CountProblems(Problem::Extra), missing = CountProblems(Problem::Missing);
			status = (extra ? std::to_wstring(extra) + (extra == 1 ? L" extra file" : L" extra files") : L"") +
				(extra && missing ? L", " : L"") +
				(missing ? std::to_wstring(missing) + (missing == 1 ? L" missing file" : L" missing files") : L"") +
				L" in the mod folder.";
		}
		DrawMark(g, static_cast<Gdiplus::REAL>(kColumnRight), static_cast<Gdiplus::REAL>(y0 + 194), ok,
			ok ? kBlue : (kEnforceCheck ? kRed : kAmber));
		RowLabel(g, status, kColumnRight + 32, y0 + 186, kColumnWidth - 32, 38);
		DrawBarButton(g, MakeRect(kColumnRight, y0 + 236, 250, 32), L"INSTALLATION CHECK", kHitOpenCheck, 11.0f);
		{
			const Gdiplus::Font note(GetFonts().Ui(), 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
			Text(g, kEnforceCheck
				? L"The game only starts when the SerenityJediEngine2027 folder holds exactly the mod's files."
				: L"Debug build: the check is shown, the game starts anyway.",
				note, kDimText, Gdiplus::RectF(static_cast<Gdiplus::REAL>(kColumnRight), static_cast<Gdiplus::REAL>(y0 + 280),
					static_cast<Gdiplus::REAL>(kColumnWidth), 40.0f));
		}
		DrawBack(g);
	}

	void DrawUpdates(Gdiplus::Graphics& g)
	{
		DrawPanel(g, L"UPDATES");
		const int x = 184, w = kWidth - 2 * x;
		int y = kPanel.top + 86;
		const Gdiplus::Font label(GetFonts().Serif(), 11.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
		const Gdiplus::Font body(GetFonts().Ui(), 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
		const Gdiplus::Font note(GetFonts().Ui(), 9.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);

		int build = 0;
		const std::wstring build_date = BuildDate(&build);
		Text(g, L"THIS BUILD", label, kTextColor, Gdiplus::RectF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y), 200.0f, 26.0f));
		Text(g, build_date.empty() ? std::wstring(VersionText()) : DateText(build_date) + L"   (build " + std::to_wstring(build) + L")",
			body, kBodyText, Gdiplus::RectF(static_cast<Gdiplus::REAL>(x + 210), static_cast<Gdiplus::REAL>(y), static_cast<Gdiplus::REAL>(w - 210), 26.0f));
		y += 40;
		Text(g, L"ON MODDB", label, kTextColor, Gdiplus::RectF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y), 200.0f, 26.0f));
		const ModdbResult& m = g_launcher.moddb;
		std::wstring moddb_text;
		Gdiplus::Color moddb_color = kBodyText;
		if (g_launcher.moddb_running)
		{
			moddb_text = L"Looking at the SerenityJediEngine downloads on ModDB ...";
		}
		else if (!g_launcher.moddb_done)
		{
			moddb_text = L"Not looked at yet: press LOOK AGAIN, or open the downloads page.";
		}
		else if (!m.ok)
		{
			moddb_text = L"Could not read the ModDB downloads page (no connection, or ModDB changed its page). Open it to look for a newer download.";
			moddb_color = kDimText;
		}
		else
		{
			moddb_text = L"Newest download: " + m.title + L", added " + DateText(m.date) + L".";
		}
		Text(g, moddb_text, body, moddb_color, Gdiplus::RectF(static_cast<Gdiplus::REAL>(x + 210), static_cast<Gdiplus::REAL>(y),
			static_cast<Gdiplus::REAL>(w - 210), 52.0f));
		y += 72;

		// the verdict
		const bool newer = NewerOnModdb();
		const bool known = g_launcher.moddb_done && m.ok && !g_launcher.moddb_running;
		if (known)
		{
			const Gdiplus::Color color = newer ? kAmber : kBlue;
			DrawMark(g, static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + 2), !newer, color);
			const Gdiplus::Font big(GetFonts().Serif(), 13.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
			Text(g, newer ? L"A NEWER DOWNLOAD IS ON MODDB" : L"YOU HAVE THE NEWEST BUILD", big, color,
				Gdiplus::RectF(static_cast<Gdiplus::REAL>(x + 34), static_cast<Gdiplus::REAL>(y), static_cast<Gdiplus::REAL>(w - 34), 28.0f),
				Gdiplus::StringAlignmentNear, Gdiplus::StringAlignmentCenter, true);
			y += 40;
		}
		Text(g, L"SerenityJediEngine updates are released on ModDB. Download the newest version there and install it "
			L"over this one (the SerenityJediEngine2027 folder must then hold exactly the new release's files).",
			note, kDimText, Gdiplus::RectF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y), static_cast<Gdiplus::REAL>(w), 44.0f));

		const int by = kPanel.bottom - 150;
		DrawBarButton(g, MakeRect(kWidth / 2 - 290, by, 300, 36), L"OPEN THE DOWNLOADS", kHitDownloads, 12.0f, newer);
		DrawBarButton(g, MakeRect(kWidth / 2 + 30, by, 260, 36), g_launcher.moddb_running ? L"LOOKING ..." : L"LOOK AGAIN", kHitCheckModdb, 12.0f);
		int px = kWidth / 2 - 120;
		px += DrawPill(g, px, by + 52, L"MODDB PAGE", kHitModdb) + 12;
		DrawPill(g, px, by + 52, L"YOUTUBE VIDEOS", kHitYoutube);
		DrawBack(g);
	}

	constexpr int kListTop = 200;
	constexpr int kRowH = 26;
	constexpr int kListRows = 9;

	void DrawInstall(Gdiplus::Graphics& g)
	{
		const CheckResult& c = g_launcher.check;
		const bool ok = c.Ok();
		DrawPanel(g, L"INSTALLATION CHECK");
		const int x = 144, w = kWidth - 2 * x;
		const int y = kPanel.top + 76;
		const Gdiplus::Font big(GetFonts().Serif(), 13.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
		const Gdiplus::Font body(GetFonts().Ui(), 10.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
		const Gdiplus::Font small_text(GetFonts().Ui(), 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
		const Gdiplus::Color bad = kEnforceCheck ? kRed : kAmber;

		std::wstring headline, explain;
		if (c.no_list)
		{
			headline = L"THE LAUNCHER'S FILE LIST IS MISSING";
			explain = L"This launcher was built without texts/AllowedFiles.txt. Download SerenityJediEngine again from ModDB.";
		}
		else if (c.no_folder)
		{
			headline = L"NO SERENITYJEDIENGINE2027 FOLDER";
			explain = L"The launcher must be in the game folder, next to the SerenityJediEngine2027 folder:\n" + ModFolderPath();
		}
		else if (ok)
		{
			headline = L"ALL THE MOD'S FILES ARE IN PLACE";
			explain = L"The SerenityJediEngine2027 folder holds the " + std::to_wstring(c.allowed) +
				L" files of this release and nothing else.";
		}
		else
		{
			const int extra = CountProblems(Problem::Extra), missing = CountProblems(Problem::Missing);
			headline = kEnforceCheck ? L"SERENITYJEDIENGINE CAN'T START YET" : L"THE MOD FOLDER IS NOT AS RELEASED";
			if (extra)
			{
				explain = L"Only SerenityJediEngine's own files may be in the SerenityJediEngine2027 folder. Remove the "
					L"files and folders marked EXTRA (other mods' .pk3 files included)";
				explain += missing ? L", and install SerenityJediEngine again from ModDB to bring back the ones marked MISSING." : L".";
			}
			else
			{
				explain = L"Files of the mod are missing. Install SerenityJediEngine again from ModDB to bring back the ones marked MISSING.";
			}
			explain += kEnforceCheck ? L" Then press CHECK AGAIN." : L" (Debug build: the game starts anyway.)";
		}
		DrawMark(g, static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + 3), ok, ok ? kBlue : bad);
		Text(g, headline, big, ok ? kBlue : bad, Gdiplus::RectF(static_cast<Gdiplus::REAL>(x + 34), static_cast<Gdiplus::REAL>(y),
			static_cast<Gdiplus::REAL>(w - 34), 28.0f), Gdiplus::StringAlignmentNear, Gdiplus::StringAlignmentCenter, true);
		Text(g, explain, body, kBodyText, Gdiplus::RectF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + 34),
			static_cast<Gdiplus::REAL>(w), 50.0f));

		// the list
		const RECT list = MakeRect(x, kListTop + 8, w, kListRows * kRowH + 8);
		AddHot(list, kHitList);
		const Gdiplus::SolidBrush list_fill(Gdiplus::Color(150, 0, 0, 0));
		g.FillRectangle(&list_fill, ToRectF(list));
		Gdiplus::Pen list_edge(Gdiplus::Color(90, 70, 130, 200), 1.0f);
		g.DrawRectangle(&list_edge, ToRectF(MakeRect(list.left, list.top, w - 1, list.bottom - list.top - 1)));
		const int count = static_cast<int>(c.items.size());
		const int max_scroll = (std::max)(0, count - kListRows);
		g_launcher.list_scroll = (std::max)(0, (std::min)(g_launcher.list_scroll, max_scroll));
		if (count == 0)
		{
			Text(g, ok ? L"Folder: " + ModFolderPath() : std::wstring(), small_text, kDimText, ToRectF(list),
				Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter, true);
		}
		const Gdiplus::Font tag_font(GetFonts().Ui(), 7.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
		for (int i = 0; i < kListRows && g_launcher.list_scroll + i < count; i++)
		{
			const CheckItem& item = c.items[g_launcher.list_scroll + i];
			const Gdiplus::REAL ry = static_cast<Gdiplus::REAL>(list.top + 4 + i * kRowH);
			const Gdiplus::Color color = item.problem == Problem::Extra ? bad : kAmber;
			Gdiplus::GraphicsPath tag;
			AddRoundRect(tag, Gdiplus::RectF(static_cast<Gdiplus::REAL>(x + 10), ry + 4, 64.0f, 18.0f), 4.0f);
			const Gdiplus::SolidBrush tag_fill(Gdiplus::Color(60, color.GetR(), color.GetG(), color.GetB()));
			g.FillPath(&tag_fill, &tag);
			Gdiplus::Pen tag_edge(color, 1.0f);
			g.DrawPath(&tag_edge, &tag);
			Text(g, item.problem == Problem::Extra ? L"EXTRA" : L"MISSING", tag_font, color,
				Gdiplus::RectF(static_cast<Gdiplus::REAL>(x + 10), ry + 4, 64.0f, 18.0f), Gdiplus::StringAlignmentCenter,
				Gdiplus::StringAlignmentCenter, true);
			const std::wstring shown = item.path.back() == L'\\' ? item.path + L"   (folder)" : item.path;
			Text(g, shown, body, kBodyText, Gdiplus::RectF(static_cast<Gdiplus::REAL>(x + 86), ry, static_cast<Gdiplus::REAL>(w - 100),
				static_cast<Gdiplus::REAL>(kRowH)), Gdiplus::StringAlignmentNear, Gdiplus::StringAlignmentCenter, true);
		}
		if (max_scroll > 0)
		{
			// a thin scroll bar on the right
			const Gdiplus::REAL track_h = static_cast<Gdiplus::REAL>(list.bottom - list.top - 8);
			const Gdiplus::REAL thumb_h = (std::max)(24.0f, track_h * kListRows / count);
			const Gdiplus::REAL thumb_y = list.top + 4 + (track_h - thumb_h) * g_launcher.list_scroll / max_scroll;
			const Gdiplus::SolidBrush thumb(Gdiplus::Color(180, 90, 150, 220));
			g.FillRectangle(&thumb, static_cast<Gdiplus::REAL>(list.right - 7), thumb_y, 3.0f, thumb_h);
			Text(g, L"Scroll for all " + std::to_wstring(count) + L".", small_text, kDimText,
				Gdiplus::RectF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(list.bottom + 2),
					static_cast<Gdiplus::REAL>(w), 20.0f), Gdiplus::StringAlignmentFar);
		}

		// what to do
		const int by = kPanel.bottom - 104;
		const int pending = g_launcher.pending_start;
		if (pending && ok)
		{
			const wchar_t* const label = pending == 1 ? L"START SINGLEPLAYER" : pending == 2 ? L"START MULTIPLAYER" : L"START SERVER";
			DrawBarButton(g, MakeRect(kWidth / 2 - 330, by, 210, 36), L"CHECK AGAIN", kHitCheckAgain, 12.0f);
			DrawBarButton(g, MakeRect(kWidth / 2 - 105, by, 210, 36), L"OPEN FOLDER", kHitOpenFolder, 12.0f);
			DrawBarButton(g, MakeRect(kWidth / 2 + 120, by, 210, 36), label, kHitStartPending, 12.0f, true);
		}
		else
		{
			DrawBarButton(g, MakeRect(kWidth / 2 - 230, by, 220, 36), L"CHECK AGAIN", kHitCheckAgain, 12.0f, !ok);
			DrawBarButton(g, MakeRect(kWidth / 2 + 10, by, 220, 36), L"OPEN FOLDER", kHitOpenFolder, 12.0f);
		}
		DrawBack(g);
	}

	void DrawWindowButtons(Gdiplus::Graphics& g)
	{
		const Gdiplus::Color blue(220, 90, 150, 220);
		const Gdiplus::Color bright(255, 150, 210, 255);
		AddHot(MusicRect(), kHitMusic);
		AddHot(MinimiseRect(), kHitMinimise);
		AddHot(CloseRect(), kHitClose);

		// speaker: waves when the music is on, a cross when it is off
		const RECT s = MusicRect();
		Gdiplus::Pen music_pen(g_launcher.hover == kHitMusic ? bright : blue, 2);
		const Gdiplus::REAL sx = static_cast<Gdiplus::REAL>(s.left);
		const Gdiplus::REAL sy = static_cast<Gdiplus::REAL>(s.top);
		g.DrawEllipse(&music_pen, sx, sy, 20.0f, 20.0f);
		const Gdiplus::PointF speaker[] = {
			{ sx + 4.5f, sy + 8.0f },{ sx + 7.5f, sy + 8.0f },{ sx + 11.0f, sy + 5.0f },
			{ sx + 11.0f, sy + 15.0f },{ sx + 7.5f, sy + 12.0f },{ sx + 4.5f, sy + 12.0f } };
		Gdiplus::Pen thin_pen(g_launcher.hover == kHitMusic ? bright : blue, 1.5f);
		g.DrawPolygon(&thin_pen, speaker, 6);
		if (g_launcher.settings.music)
		{
			g.DrawArc(&thin_pen, sx + 9.0f, sy + 7.0f, 6.0f, 6.0f, -60.0f, 120.0f);
			g.DrawArc(&thin_pen, sx + 8.0f, sy + 4.5f, 9.5f, 11.0f, -60.0f, 120.0f);
		}
		else
		{
			g.DrawLine(&thin_pen, sx + 13.0f, sy + 8.0f, sx + 17.0f, sy + 12.0f);
			g.DrawLine(&thin_pen, sx + 17.0f, sy + 8.0f, sx + 13.0f, sy + 12.0f);
		}

		const RECT m = MinimiseRect();
		Gdiplus::Pen min_pen(g_launcher.hover == kHitMinimise ? bright : blue, 2);
		g.DrawEllipse(&min_pen, static_cast<INT>(m.left), static_cast<INT>(m.top), 20, 20);
		g.DrawLine(&min_pen, static_cast<INT>(m.left + 5), static_cast<INT>(m.top + 10), static_cast<INT>(m.left + 15), static_cast<INT>(m.top + 10));

		const RECT c = CloseRect();
		Gdiplus::Pen close_pen(g_launcher.hover == kHitClose ? bright : blue, 2);
		g.DrawEllipse(&close_pen, static_cast<INT>(c.left), static_cast<INT>(c.top), 20, 20);
		g.DrawLine(&close_pen, static_cast<INT>(c.left + 5), static_cast<INT>(c.top + 5), static_cast<INT>(c.left + 15), static_cast<INT>(c.top + 15));
		g.DrawLine(&close_pen, static_cast<INT>(c.left + 15), static_cast<INT>(c.top + 5), static_cast<INT>(c.left + 5), static_cast<INT>(c.top + 15));
	}

	void Paint(const HWND hwnd)
	{
		PAINTSTRUCT ps;
		const HDC hdc = BeginPaint(hwnd, &ps);
		g_launcher.hot.clear();

		// draw into a back buffer, then copy it in one go (no flicker)
		Gdiplus::Bitmap buffer(kWidth, kHeight, PixelFormat32bppARGB);
		{
			Gdiplus::Graphics g(&buffer);
			g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
			g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
			g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

			if (g_launcher.background)
			{
				g.DrawImage(g_launcher.background.get(), 0, 0, kWidth, kHeight);
			}
			else
			{
				g.Clear(Gdiplus::Color(255, 0, 0, 0));
			}

			const Page page = g_launcher.page;
			if (page == Page::Main || page == Page::Multiplayer)
			{
				for (int i = 0; i < ButtonCount(); i++)
				{
					DrawBarButton(g, ButtonRect(i), ButtonLabel(i), i);
				}
				// the links, bottom left
				int x = 12;
				x += DrawPill(g, x, kHeight - 34, L"UPDATES", kHitUpdates, UpdateDot()) + 8;
				x += DrawPill(g, x, kHeight - 34, L"MODDB", kHitModdb) + 8;
				DrawPill(g, x, kHeight - 34, L"YOUTUBE", kHitYoutube);
			}
			else if (page == Page::Settings)
			{
				DrawSettings(g);
			}
			else if (page == Page::Updates)
			{
				DrawUpdates(g);
			}
			else
			{
				DrawInstall(g);
			}

			DrawWindowButtons(g);

			const Gdiplus::Font version_font(GetFonts().Ui(), 8, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
			Text(g, VersionText(), version_font, Gdiplus::Color(160, 200, 200, 200), Gdiplus::RectF(0, kHeight - 20, kWidth - 10, 20),
				Gdiplus::StringAlignmentFar);
		}

		// copy the finished frame 1:1, no filtering
		Gdiplus::Graphics screen(hdc);
		screen.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
		screen.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
		screen.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
		screen.DrawImage(&buffer, 0, 0, kWidth, kHeight);
		EndPaint(hwnd, &ps);
	}

	// ------------------------------------------------------------------------------------------
	// Starting the game
	// ------------------------------------------------------------------------------------------

	bool StartGame(const HWND hwnd, const bool multiplayer)
	{
		const Settings& s = g_launcher.settings;
		const std::wstring exe = g_launcher.folder + L"\\" + (multiplayer ? kMPExe : kSPExe);
		if (!PathFileExistsW(exe.c_str()))
		{
			const std::wstring msg = L"Could not find " + exe + L"\n\nThe launcher must be in the SerenityJediEngine2027 game folder.";
			MessageBoxW(hwnd, msg.c_str(), L"SerenityJediEngine2027", MB_OK | MB_ICONERROR);
			return false;
		}

		// "+set" on the command line overrides the values in the configs (they are the configs' own values unless
		// SETTINGS changed them, and then it wrote them into the configs too)
		std::wstring cmd = L"\"" + exe + L"\"";
		for (const auto& kv : GameConfigValues(multiplayer))
		{
			cmd += L" +set " + std::wstring(kv.first.begin(), kv.first.end()) + L" " + std::wstring(kv.second.begin(), kv.second.end());
		}
		cmd += s.controller ? L" +set in_joystick 1" : L" +set in_joystick 0";

		SetEnvironmentVariableW(kLauncherEnv, kLauncherToken); // the game inherits it: lets a release build start

		STARTUPINFOW si = { sizeof(si) };
		PROCESS_INFORMATION pi = {};
		std::wstring cmd_buffer = cmd; // CreateProcessW may write into it
		if (!CreateProcessW(exe.c_str(), &cmd_buffer[0], nullptr, nullptr, FALSE, 0, nullptr, g_launcher.folder.c_str(), &si, &pi))
		{
			MessageBoxW(hwnd, (L"Could not start " + exe).c_str(), L"SerenityJediEngine2027", MB_OK | MB_ICONERROR);
			return false;
		}
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return true;
	}

	// Runs the FFA dedicated server batch file from the mod folder in a console window of its own. The batch file
	// restarts the server when it crashes; closing its window stops it.
	bool StartServer(const HWND hwnd)
	{
		const std::wstring mod = ModFolderPath();
		const std::wstring bat = mod + L"\\" + kServerBat;
		if (!PathFileExistsW(bat.c_str()))
		{
			const std::wstring msg = L"Could not find " + bat + L"\n\nThe launcher must be in the SerenityJediEngine2027 game folder.";
			MessageBoxW(hwnd, msg.c_str(), L"SerenityJediEngine2027", MB_OK | MB_ICONERROR);
			return false;
		}

		wchar_t comspec[MAX_PATH] = {};
		if (!GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH))
		{
			GetSystemDirectoryW(comspec, MAX_PATH);
			wcscat_s(comspec, L"\\cmd.exe");
		}
		// cmd /c ""<bat>"": the outer quotes are cmd's, the inner ones keep the spaces in the path
		std::wstring cmd = L"\"" + std::wstring(comspec) + L"\" /c \"\"" + bat + L"\"\"";

		SetEnvironmentVariableW(kLauncherEnv, kLauncherToken); // the server inherits it, as the game does

		STARTUPINFOW si = { sizeof(si) };
		PROCESS_INFORMATION pi = {};
		if (!CreateProcessW(comspec, &cmd[0], nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, mod.c_str(), &si, &pi))
		{
			MessageBoxW(hwnd, (L"Could not start " + bat).c_str(), L"SerenityJediEngine2027", MB_OK | MB_ICONERROR);
			return false;
		}
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return true;
	}

	void RunCheckWithCursor()
	{
		const HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
		RunCheck();
		SetCursor(old);
	}

	// what: 1 singleplayer, 2 multiplayer, 3 the dedicated server. A release build checks the mod folder first and
	// shows INSTALLATION CHECK instead when it isn't as released.
	void TryStart(const HWND hwnd, const int what)
	{
		if (kEnforceCheck)
		{
			RunCheckWithCursor();
			if (!g_launcher.check.Ok())
			{
				PlaySfx();
				g_launcher.pending_start = what;
				g_launcher.page = Page::Install;
				return;
			}
		}
		if (what == 3)
		{
			PlaySfx();
			if (StartServer(hwnd)) // the launcher stays open, to start the game and join
			{
				g_launcher.pending_start = 0;
				g_launcher.page = Page::Multiplayer;
			}
			return;
		}
		PlaySfx(true); // the launcher closes right after
		if (StartGame(hwnd, what == 2))
		{
			DestroyWindow(hwnd);
		}
	}

	void GoBack()
	{
		const Page page = g_launcher.page;
		const int pending = g_launcher.pending_start;
		g_launcher.pending_start = 0;
		if (page == Page::Multiplayer)
		{
			g_launcher.page = Page::Main;
		}
		else if (page == Page::Install && !pending)
		{
			g_launcher.page = Page::Settings; // opened from SETTINGS
		}
		else if (page == Page::Install && pending >= 2)
		{
			g_launcher.page = Page::Multiplayer;
		}
		else
		{
			g_launcher.page = Page::Main;
		}
	}

	void SetMusic(const HWND hwnd, const bool on)
	{
		g_launcher.settings.music = on;
		SaveSettings();
		if (on)
		{
			StartMusic(hwnd);
		}
		else
		{
			StopMusic();
		}
	}

	void OpenUpdates()
	{
		g_launcher.page = Page::Updates;
		if (g_launcher.moddb_done && g_launcher.moddb.ok && g_launcher.seen_update != g_launcher.moddb.date)
		{
			g_launcher.seen_update = g_launcher.moddb.date; // looked at: no dot for this one any more
			SaveSettings();
		}
		if (!g_launcher.moddb_done && !g_launcher.moddb_running)
		{
			StartModdbCheck(); // switched off at start: look now
		}
	}

	void OnClick(const HWND hwnd, const int hit)
	{
		Settings& s = g_launcher.settings;
		const Page page = g_launcher.page;
		if (hit >= 0 && hit < 10)
		{
			if (page == Page::Main)
			{
				switch (hit)
				{
				case 0: TryStart(hwnd, 1); return;
				case 1: PlaySfx(); g_launcher.page = Page::Multiplayer; break;
				case 2: PlaySfx(); g_launcher.page = Page::Settings; break;
				default: PlaySfx(true); DestroyWindow(hwnd); return;
				}
			}
			else if (page == Page::Multiplayer)
			{
				switch (hit)
				{
				case 0: TryStart(hwnd, 2); return;
				case 1: TryStart(hwnd, 3); break;
				default: PlaySfx(); g_launcher.page = Page::Main; break;
				}
			}
			InvalidateRect(hwnd, nullptr, FALSE);
			return;
		}

		PlaySfx();
		bool game_config = false;
		switch (hit)
		{
		case kHitMusic: SetMusic(hwnd, !s.music); break;
		case kHitUpdates: OpenUpdates(); break;
		case kHitModdb: OpenUrl(kModdbUrl); break;
		case kHitYoutube: OpenUrl(kYoutubeUrl); break;
		case kHitDownloads: OpenUrl(kDownloadsUrl); break;
		case kHitCheckModdb: StartModdbCheck(); break;
		case kHitBack: GoBack(); break;
		case kHitRendPerf: s.rend2 = false; game_config = true; break;
		case kHitRendQuality: s.rend2 = true; game_config = true; break;
		case kHitFullscreen: s.windowed = false; game_config = true; break;
		case kHitWindowed: s.windowed = true; game_config = true; break;
		case kHitResPrev: PreviousResolution(); game_config = true; break;
		case kHitResNext: NextResolution(); game_config = true; break;
		case kHitController: s.controller = !s.controller; SaveSettings(); break;
		case kHitMusicSwitch: SetMusic(hwnd, !s.music); break;
		case kHitUpdateSwitch:
			s.check_updates = !s.check_updates;
			SaveSettings();
			break;
		case kHitOpenCheck:
			RunCheckWithCursor();
			g_launcher.pending_start = 0;
			g_launcher.page = Page::Install;
			break;
		case kHitCheckAgain: RunCheckWithCursor(); break;
		case kHitOpenFolder:
		{
			const std::wstring folder = PathFileExistsW(ModFolderPath().c_str()) ? ModFolderPath() : g_launcher.folder;
			ShellExecuteW(hwnd, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			break;
		}
		case kHitStartPending:
			TryStart(hwnd, g_launcher.pending_start);
			return;
		default:
			break;
		}
		if (game_config)
		{
			SaveSettings();
			SaveGameConfig(); // renderer, display, resolution: into the game's configs
		}
		InvalidateRect(hwnd, nullptr, FALSE);
	}

	// ------------------------------------------------------------------------------------------
	// Window
	// ------------------------------------------------------------------------------------------

	// moving onto these plays the menu sound, as the game's menu buttons do
	bool SoundsOnHover(const int hit)
	{
		return (hit >= 0 && hit < 10) || hit == kHitBack || hit == kHitDownloads || hit == kHitCheckModdb ||
			hit == kHitOpenCheck || hit == kHitCheckAgain || hit == kHitOpenFolder || hit == kHitStartPending;
	}

	bool Clickable(const int hit)
	{
		return hit != kHitNone && hit != kHitList;
	}

	void SetHover(const HWND hwnd, const int hit)
	{
		if (hit != g_launcher.hover)
		{
			g_launcher.hover = hit;
			InvalidateRect(hwnd, nullptr, FALSE);
		}
	}

	LRESULT CALLBACK WndProc(const HWND hwnd, const UINT msg, const WPARAM wparam, const LPARAM lparam)
	{
		switch (msg)
		{
		case WM_NCHITTEST:
		{
			// drag the window by anything that is not a button
			POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
			ScreenToClient(hwnd, &pt);
			return HitTest(pt) == kHitNone ? HTCAPTION : HTCLIENT;
		}
		case WM_SETCURSOR:
			if (LOWORD(lparam) == HTCLIENT)
			{
				POINT pt;
				GetCursorPos(&pt);
				ScreenToClient(hwnd, &pt);
				SetCursor(LoadCursorW(nullptr, Clickable(HitTest(pt)) ? IDC_HAND : IDC_ARROW));
				return TRUE;
			}
			return DefWindowProcW(hwnd, msg, wparam, lparam);
		case WM_MOUSEMOVE:
		{
			if (!g_launcher.tracking_mouse)
			{
				TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
				g_launcher.tracking_mouse = TrackMouseEvent(&tme) != FALSE;
			}
			const int hit = HitTest(POINT{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) });
			if (hit != g_launcher.hover && SoundsOnHover(hit))
			{
				PlaySfx(); // moved onto another button
			}
			SetHover(hwnd, hit);
			return 0;
		}
		case WM_MOUSELEAVE:
		case WM_NCMOUSEMOVE:
			g_launcher.tracking_mouse = false;
			g_launcher.pressed = kHitNone;
			SetHover(hwnd, kHitNone);
			return 0;
		case WM_MOUSEWHEEL:
		{
			if (g_launcher.page == Page::Install)
			{
				g_launcher.list_scroll -= GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA * 3;
				InvalidateRect(hwnd, nullptr, FALSE);
			}
			return 0;
		}
		case WM_LBUTTONDOWN:
			g_launcher.pressed = HitTest(POINT{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) });
			SetCapture(hwnd);
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		case WM_LBUTTONUP:
		{
			ReleaseCapture();
			const int hit = HitTest(POINT{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) });
			const int pressed = g_launcher.pressed;
			g_launcher.pressed = kHitNone;
			InvalidateRect(hwnd, nullptr, FALSE);
			if (hit != pressed || !Clickable(hit))
			{
				return 0; // released somewhere else
			}
			if (hit == kHitClose)
			{
				PlaySfx(true);
				DestroyWindow(hwnd);
			}
			else if (hit == kHitMinimise)
			{
				PlaySfx();
				ShowWindow(hwnd, SW_MINIMIZE);
			}
			else
			{
				OnClick(hwnd, hit);
			}
			return 0;
		}
		case WM_KEYDOWN:
			if (wparam == VK_ESCAPE)
			{
				if (g_launcher.page != Page::Main)
				{
					PlaySfx();
					GoBack();
					InvalidateRect(hwnd, nullptr, FALSE);
				}
				else
				{
					PlaySfx(true);
					DestroyWindow(hwnd);
				}
			}
			return 0;
		case kMsgModdbDone:
		{
			std::unique_ptr<ModdbResult> result(reinterpret_cast<ModdbResult*>(lparam));
			g_launcher.moddb_running = false;
			g_launcher.moddb_done = true;
			g_launcher.moddb = *result;
			if (g_launcher.page == Page::Updates && g_launcher.moddb.ok)
			{
				g_launcher.seen_update = g_launcher.moddb.date; // on the page right now: seen
				SaveSettings();
			}
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		}
		case MM_MCINOTIFY:
			// the music reached its end: play it again (loop). The menu sound plays without notify.
			if (wparam == MCI_NOTIFY_SUCCESSFUL && g_launcher.music_open)
			{
				PlayMusicFromStart(hwnd);
			}
			return 0;
		case WM_ERASEBKGND:
			return 1; // everything is painted in WM_PAINT
		case WM_PAINT:
			Paint(hwnd);
			return 0;
		case WM_DESTROY:
			StopMusic();
			PostQuitMessage(0);
			return 0;
		default:
			return DefWindowProcW(hwnd, msg, wparam, lparam);
		}
	}
}

int WINAPI wWinMain(_In_ const HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ const int show) // SAL as winbase.h (C28251)
{
	g_launcher.instance = instance;

	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	PathRemoveFileSpecW(path);
	g_launcher.folder = path;
	LoadSettings();
	RunCheck(); // the folder listing only: quick

	Gdiplus::GdiplusStartupInput gdiplus_input;
	ULONG_PTR gdiplus_token = 0;
	Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr);
	g_launcher.background = LoadPng(IDR_BG);

	WNDCLASSEXW wc = { sizeof(wc) };
	wc.lpfnWndProc = WndProc;
	wc.hInstance = instance;
	wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_LAUNCHER));
	wc.hIconSm = wc.hIcon;
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wc.lpszClassName = L"SerenityJediEngine2027Launcher";
	RegisterClassExW(&wc);

	// centred, borderless (the art has its own frame and window buttons)
	const int x = (GetSystemMetrics(SM_CXSCREEN) - kWidth) / 2;
	const int y = (GetSystemMetrics(SM_CYSCREEN) - kHeight) / 2;
	const HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"SerenityJediEngine2027", WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
		x, y, kWidth, kHeight, nullptr, nullptr, instance, nullptr);
	if (!hwnd)
	{
		g_launcher.background.reset();
		Gdiplus::GdiplusShutdown(gdiplus_token);
		return 1;
	}
	g_launcher.hwnd = hwnd;
	ShowWindow(hwnd, show);
	UpdateWindow(hwnd);
	StartMusic(hwnd);
	if (g_launcher.settings.check_updates)
	{
		StartModdbCheck();
	}

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	DeleteTempFile(g_launcher.music_file);
	g_launcher.background.reset(); // before GDI+ goes
	GetFonts().serif.reset();
	GetFonts().ui.reset();
	Gdiplus::GdiplusShutdown(gdiplus_token);
	return 0;
}
