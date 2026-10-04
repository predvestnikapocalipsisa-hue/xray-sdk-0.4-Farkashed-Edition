#include "stdafx.h"
#include <mmsystem.h>
#include <objbase.h>
#include "xrCore.h"

#ifdef DEBUG
#include <malloc.h>
#endif // DEBUG

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dxerr.lib")

#ifndef _WIN64
#pragma comment(lib, "legacy_stdio_definitions.lib")
#endif

XRCORE_API xrCore Core;

static xr_vector<xr_string> missingSDKFiles;
static xr_string sdkStructure;
static bool sdkStartupChecking = true;

// Check includes before CInifile loads them: a partial configuration must not
// reach the game factory, which assumes all inherited sections exist.
static bool CheckSDKIncludes(const char* filename, xr_vector<xr_string>& visited, unsigned depth = 0)
{
	if (depth > 64)
	{
		Core.ReportMissingSDKFile("system.ltx: include nesting exceeds 64 levels");
		return false;
	}
	if (std::find(visited.begin(), visited.end(), xr_string(filename)) != visited.end())
		return true;
	visited.push_back(filename);
	IReader* reader = FS.r_open(filename);
	if (!reader)
	{
		Core.ReportMissingSDKFile(filename);
		return false;
	}
	string_path drive, directory, parent;
	_splitpath(filename, drive, directory, NULL, NULL);
	strconcat(sizeof(parent), parent, drive, directory);
	bool complete = true;
	while (!reader->eof())
	{
		string4096 line;
		reader->r_string(line, sizeof(line));
		_Trim(line);
		if (strncmp(line, "#include", 8) != 0)
			continue;
		string_path include, resolved;
		_GetItem(line, 1, include, '"');
		if (!include[0])
			continue;
		strconcat(sizeof(resolved), resolved, parent, include);
		if (!CheckSDKIncludes(resolved, visited, depth + 1))
			complete = false;
	}
	FS.r_close(reader);
	return complete;
}

void xrCore::ReportMissingSDKFile(const char* path)
{
	const bool firstMissingFile = !SDKFallback;
	SDKFallback = true;
	if (std::find(missingSDKFiles.begin(), missingSDKFiles.end(), xr_string(path)) == missingSDKFiles.end())
		missingSDKFiles.push_back(path);
	Msg("! SDK fallback: missing %s", path);
	if (firstMissingFile && !sdkStartupChecking && MessageBoxA(NULL,
		"An SDK resource is missing. Fallback mode has been enabled; unavailable operations are disabled.\n\n"
		"View the SDK structure and missing resources?", "SDK fallback", MB_YESNO | MB_ICONWARNING) == IDYES)
		ShowSDKStructure();
}

bool xrCore::SDKFileAvailable(const char* alias, const char* name)
{
	string_path path;
	return FS.path_exist(alias) && FS.exist(path, alias, name);
}

const char* xrCore::SDKStructure()
{
	sdkStructure = "SDK structure (paths resolved from fs.ltx; defaults used for missing aliases)\r\n\r\n";
	const char* aliases[] = {"$app_root$", "$game_data$", "$game_config$", "$game_shaders$",
		"$game_textures$", "$game_meshes$", "$game_sounds$", "$game_scripts$",
		"$objects$", "$textures$", "$sounds$", "$maps$", "$groups$", "$import$", "$local_root$", "$temp$"};
	for (const char* alias : aliases)
	{
		if (!FS.path_exist(alias))
			continue;
		const char* path = FS.get_path(alias)->m_Path;
		const DWORD attributes = GetFileAttributesA(path);
		sdkStructure += (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) ? "[OK] " : "[missing] ";
		sdkStructure += alias;
		sdkStructure += " -> ";
		sdkStructure += path;
		sdkStructure += "\r\n";
	}
	sdkStructure += "\r\nMissing files / default path aliases:\r\n";
	for (const xr_string& path : missingSDKFiles)
	{
		sdkStructure += "  ";
		sdkStructure += path;
		sdkStructure += "\r\n";
	}
	sdkStructure += "\r\nRequired game data: shaders.xr, shaders_xrlc.xr, gamemtl.xr, particles.xr, lanims.xr.\r\n"
		"Game configuration: system.ltx and its includes.\r\n"
		"Fallback keeps empty libraries and basic rendering. Spawn/scripts and unavailable libraries are disabled.\r\n"
		"Restore the original SDK data and restart the editor to enable all features.";
	return sdkStructure.c_str();
}

void xrCore::ShowSDKStructure()
{
	MessageBoxA(NULL, SDKStructure(), "SDK structure / fallback", MB_OK | MB_ICONINFORMATION);
}

void PrintBuildId()
{
	constexpr int MonthsCount = 12;

	static const char* MonthId[MonthsCount] =
	{
		"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};

	static const int DaysInMonth[MonthsCount] =
	{
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
	};

	static const int start_day = 31;	// 31
	static const int start_month = 1;	// January
	static const int start_year = 1999;	// 1999

	const char* BuildDate = __DATE__;

	int days;
	int months = 0;
	int years;

	string16 month;
	string256 buffer;
	strcpy_s(buffer, BuildDate);
	sscanf(buffer, "%s %d %d", month, &days, &years);
	month[15] = '\0';

	for (int i = 0; i < MonthsCount; i++)
	{
		if (_stricmp(MonthId[i], month))
			continue;

		months = i;
		break;
	}

	u32 build_id = (years - start_year) * 365 + days - start_day;

	for (int i = 0; i < months; ++i)
		build_id += DaysInMonth[i];

	for (int i = 0; i < start_month - 1; ++i)
		build_id -= DaysInMonth[i];

	Msg("'xrCore' build %d, %s\n", build_id, BuildDate);
}

namespace CPU
{
	extern void Detect();
};

void xrCore::InitCore(const char* AppName, LogCallback cb)
{
	SDKFallback = false;
	sdkStartupChecking = true;
	SDKHasGameConfig = false;
	SDKHasShaders = false;
	xr_strcpy(ApplicationName, AppName);

	// Init COM so we can use CoCreateInstance
	if (!strstr(GetCommandLine(), "-editor"))
		CoInitializeEx(NULL, COINIT_MULTITHREADED);

	xr_strcpy(Params, sizeof(Params), GetCommandLine());
	_strlwr_s(Params, sizeof(Params));

	string_path fn, dr, di;

	// application path
	GetModuleFileName(GetModuleHandle(MODULE_NAME), fn, sizeof(fn));
	_splitpath(fn, dr, di, nullptr, nullptr);
	strconcat(sizeof(ApplicationPath), ApplicationPath, dr, di);

	GetCurrentDirectory(sizeof(WorkingPath), WorkingPath);

	// User/Comp Name
	DWORD sz_user = sizeof(UserName);
	GetUserName(UserName, &sz_user);

	DWORD sz_comp = sizeof(CompName);
	GetComputerName(CompName, &sz_comp);

	// Mathematics & PSI detection
	CPU::Detect();

	Memory._initialize(strstr(Params, "-mem_debug") ? TRUE : FALSE);
	DUMP_PHASE;

	_setmaxstdio(2048);

	InitLog();
	_initialize_cpu();

	xr_FS = xr_new<ELocatorAPI>();
	xr_EFS = xr_new<EFS_Utils>();

	u32 flags = 0;

	if (strstr(Params, "-build"))
		flags |= ELocatorAPI::flBuildCopy;

	if (strstr(Params, "-ebuild"))
		flags |= ELocatorAPI::flBuildCopy | ELocatorAPI::flEBuildCopy;

#ifdef DEBUG
	if (strstr(Params, "-cache"))
		flags |= ELocatorAPI::flCacheFiles;
	else
		flags &= ~ELocatorAPI::flCacheFiles;
#endif // DEBUG

	flags |= ELocatorAPI::flScanAppRoot;

	if (0 != strstr(Params, "-file_activity"))
		flags |= ELocatorAPI::flDumpFileActivity;

	FS.InitFS(flags);
	SDKHasGameConfig = SDKFileAvailable("$game_config$", "system.ltx");
	if (SDKHasGameConfig)
	{
		string_path configPath;
		FS.update_path(configPath, "$game_config$", "system.ltx");
		xr_vector<xr_string> visited;
		SDKHasGameConfig = CheckSDKIncludes(configPath, visited);
		if (SDKHasGameConfig)
		{
			CInifile config(configPath, TRUE, TRUE, FALSE);
			if (!config.line_exist("hud_font_small", "shader") || !config.line_exist("hud_font_small", "texture"))
				ReportMissingSDKFile("system.ltx: hud_font_small shader/texture settings");
			else
			{
				const char* prefix = config.line_exist("string_table", "font_prefix") ? config.r_string("string_table", "font_prefix") : "";
				string_path texture, path;
				const char* fontTexture = config.r_string("hud_font_small", "texture");
				const bool independentFont = strstr(fontTexture, "ui_font_hud_01") || strstr(fontTexture, "ui_font_hud_02") || strstr(fontTexture, "ui_font_console_02");
				strconcat(sizeof(texture), texture, fontTexture, independentFont || !prefix ? "" : prefix);
				const char* extensions[] = {".ini", ".dds"};
				for (const char* extension : extensions)
				{
					if (!FS.exist(path, "$game_textures$", texture, extension))
						ReportMissingSDKFile(path);
				}
				if (!config.line_exist("string_table", "font_prefix"))
					ReportMissingSDKFile("system.ltx: string_table/font_prefix setting");
			}
		}
	}
	SDKHasShaders = SDKFileAvailable("$game_data$", "shaders.xr");
	const char* startupAssets[] = {"shaders.xr", "shaders_xrlc.xr", "gamemtl.xr", "particles.xr", "lanims.xr"};
	for (const char* asset : startupAssets)
	{
		if (!SDKFileAvailable("$game_data$", asset))
		{
			string_path path;
			FS.update_path(path, "$game_data$", asset);
			ReportMissingSDKFile(path);
		}
	}
	if (!SDKHasGameConfig)
	{
		string_path path;
		FS.update_path(path, "$game_config$", "system.ltx");
		ReportMissingSDKFile(path);
	}
	if (SDKFallback && MessageBoxA(NULL,
		"SDK data is incomplete. The editor will start in fallback mode with empty libraries and basic rendering.\n"
		"Features that need missing files are unavailable.\n\nView the SDK structure and missing files?",
		"SDK fallback", MB_YESNO | MB_ICONWARNING) == IDYES)
		ShowSDKStructure();
	PrintBuildId();
	EFS._initialize();

#ifdef DEBUG
	Msg("CRT heap 0x%08x", _get_heap_handle());
	Msg("Process heap 0x%08x", GetProcessHeap());
#endif // DEBUG

	SetLogCB(cb);
	sdkStartupChecking = false;
}

void xrCore::DestroyCore()
{
	xr_vector<xr_string>().swap(missingSDKFiles);
	{
		xr_string emptyStructure;
		sdkStructure.swap(emptyStructure);
	}
	FS.DestroyFS();
	EFS._destroy();
	xr_delete(xr_FS);
	xr_delete(xr_EFS);
	Memory._destroy();
}

BOOL WINAPI SdkCoreDllMain(HINSTANCE hinstDLL, DWORD ul_reason_for_call, LPVOID lpvReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_THREAD_ATTACH:
		timeBeginPeriod(1);
		break;

	case DLL_PROCESS_DETACH:
#ifdef USE_MEMORY_MONITOR
		memory_monitor::flush_each_time(true);
#endif // USE_MEMORY_MONITOR
		break;
	}
	return TRUE;
}
