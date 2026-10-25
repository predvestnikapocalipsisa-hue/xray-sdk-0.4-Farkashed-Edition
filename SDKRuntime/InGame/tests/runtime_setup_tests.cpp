#include "../RuntimeSetup.h"
#include <iostream>
#include <stdexcept>

using namespace SDKInGameSetup;

static void Require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
static void Put(const std::wstring& path, const std::string& value)
{
    Directory(Parent(path));
    Write(path, value);
}
static void Package(const std::wstring& runtime)
{
    for (const auto* name : {L"SDKSceneHost.exe", L"xrEngine.exe", L"OpenAL32.dll", L"nvngx_dlss.dll", L"dbghelp.dll"})
        Put(runtime + L"\\bin\\" + name, "fixture");
    Put(runtime + L"\\defaults\\fsgame.ltx", "$game_data$ = false| true| $fs_root$| gamedata\\\r\n");
    Put(runtime + L"\\defaults\\preview.ltx", "preview");
    Put(runtime + L"\\defaults\\gamedata\\config\\external.ltx", "default");
}

int wmain()
{
    try {
        wchar_t temporary[MAX_PATH]{};
        GetTempPathW(MAX_PATH, temporary);
        const auto root = std::wstring(temporary) + L"SDK InGame setup tests " + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        const auto sdk = root + L"\\installed SDK";
        Package(sdk + L"\\InGame");
        Require(Resources(sdk + L"\\InGame"), "overlay without system.ltx rejected");
        Require(!File(sdk + L"\\InGame\\defaults\\gamedata\\config\\system.ltx"), "fixture must not supply archive-owned system.ltx");
        Require(!Resources(root + L"\\incomplete"), "missing overlay accepted");
        Put(sdk + L"\\game\\gamedata.db0", "archive fixture");
        std::wstring error;
        Require(Ensure(sdk, nullptr, error), "first setup failed");
        const auto runtime = sdk + L"\\InGame";
        Require(Read(runtime + L"\\gamedata\\config\\external.ltx") == "default", "defaults not installed");
        Require(Read(runtime + L"\\_appdata_\\sdk-preview.ltx") == "preview", "preview not installed");
        const auto config = Read(runtime + L"\\fsgame.runtime.ltx");
        Require(ArchiveSource(config) == sdk + L"\\game\\", "archive alias wrong (spaces or separators)");
        Require(Archives(ArchiveSource(config)), "saved archive source not readable");

        Put(runtime + L"\\gamedata\\config\\external.ltx", "user override");
        Put(runtime + L"\\_appdata_\\sdk-preview.ltx", "user settings");
        Require(Ensure(sdk, nullptr, error), "repeat setup failed");
        Require(Read(runtime + L"\\gamedata\\config\\external.ltx") == "user override", "user override overwritten");
        Require(Read(runtime + L"\\_appdata_\\sdk-preview.ltx") == "user settings", "preview settings overwritten");
        Require(Read(runtime + L"\\fsgame.runtime.ltx") == config, "repeat setup changed config");

        const auto externalGame = root + L"\\external game";
        Put(externalGame + L"\\gamedata.db1", "archive fixture");
        std::string configured = config;
        const auto begin = configured.find("$sdk_archive_source$");
        configured.replace(begin, configured.find('\n', begin) - begin + 1,
            "$sdk_archive_source$ = false| false| " + EnginePath(externalGame) + "\\\r\n");
        Put(runtime + L"\\fsgame.runtime.ltx", configured);
        Require(Ensure(sdk, nullptr, error), "external game setup failed");
        Require(Read(runtime + L"\\fsgame.runtime.ltx") == configured, "saved game folder ignored");

        const auto repository = root + L"\\source tree";
        Package(repository + L"\\InGame");
        const auto development = repository + L"\\BuildedSDK\\x64_Release";
        Put(development + L"\\game\\gamedata.db0", "archive fixture");
        Require(Ensure(development, nullptr, error), "automatic development import failed");
        Require(Binaries(development + L"\\InGame"), "runtime binaries not imported");
        Require(File(development + L"\\InGame\\gamedata\\config\\external.ltx"), "imported resources missing");

        const auto legacySource = root + L"\\legacy source";
        // Legacy source trees keep resource templates alongside the OGSR overlay.
        for (const auto* name : {L"SDKSceneHost.exe", L"xrEngine.exe", L"OpenAL32.dll", L"nvngx_dlss.dll", L"dbghelp.dll"})
            Put(legacySource + L"\\InGame\\bin\\" + name, "fixture");
        Put(legacySource + L"\\InGame\\fsgame.ltx", "$game_data$ = false| true| $fs_root$| gamedata\\\r\n");
        Put(legacySource + L"\\InGame\\preview.ltx", "preview");
        Put(legacySource + L"\\OGSR\\Game\\Resources_SoC_1.0006\\gamedata\\config\\external.ltx", "OGSR base");
        Put(legacySource + L"\\InGame\\overrides\\config\\external.ltx", "SDK compatibility");
        const auto legacySdk = legacySource + L"\\BuildedSDK\\x64_Release";
        Put(legacySdk + L"\\game\\gamedata.db0", "archive fixture");
        Require(Ensure(legacySdk, nullptr, error), "legacy source import failed");
        Require(Read(legacySdk + L"\\InGame\\gamedata\\config\\external.ltx") == "SDK compatibility", "compatibility override not applied");

        Put(runtime + L"\\fsgame.runtime.ltx", "invalid config");
        Require(!Ensure(sdk, nullptr, error) && !error.empty(), "invalid config not diagnosed");
        Require(Read(runtime + L"\\fsgame.runtime.ltx") == "invalid config", "invalid config was destroyed");
        Require(!Archives(L""), "empty archive path accepted");
        Require(ArchiveSource("$sdk_archive_source$ = false").empty(), "malformed alias accepted");
        std::cout << "PASS: first/repeated setup, user overrides, saved game path, package/legacy import, invalid config\n";
        std::wcout << L"Fixtures: " << root << L"\n";
        return 0;
    } catch (const Failure& failure) {
        std::wcerr << failure.message << L"\n";
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << "\n";
    }
    return 1;
}
