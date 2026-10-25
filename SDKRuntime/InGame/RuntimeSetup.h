#pragma once

// Native setup shared by the launcher and Level Editor; no shell or PowerShell.
#include <windows.h>
#include <shobjidl.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")

namespace SDKInGameSetup
{
    struct Failure { std::wstring message; };
    inline bool File(const std::wstring& path)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    }
    inline std::wstring Parent(const std::wstring& path)
    {
        const size_t slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
    }
    inline std::wstring ModuleDirectory()
    {
        wchar_t path[32768]{};
        GetModuleFileNameW(nullptr, path, DWORD(sizeof(path) / sizeof(*path)));
        return Parent(path);
    }
    inline void Check(bool success, const std::wstring& path)
    {
        if (!success) throw Failure{L"Cannot prepare InGame file:\n" + path +
            L"\nCheck that the folder is writable and InGame is closed."};
    }
    inline void Directory(const std::wstring& path)
    {
        if (path.empty()) return;
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) return;
        Directory(Parent(path));
        Check(CreateDirectoryW(path.c_str(), nullptr) != FALSE || GetLastError() == ERROR_ALREADY_EXISTS, path);
    }
    inline void Copy(const std::wstring& from, const std::wstring& to, bool replace = false)
    {
        if (!replace && File(to)) return;
        if (_wcsicmp(from.c_str(), to.c_str()) == 0) return;
        Directory(Parent(to));
        Check(CopyFileW(from.c_str(), to.c_str(), !replace) != FALSE, to);
    }
    inline void Tree(const std::wstring& from, const std::wstring& to, bool replace = false)
    {
        WIN32_FIND_DATAW item{};
        HANDLE search = FindFirstFileW((from + L"\\*").c_str(), &item);
        if (search == INVALID_HANDLE_VALUE) throw Failure{L"InGame resources are missing:\n" + from};
        try {
            do {
                if (!wcscmp(item.cFileName, L".") || !wcscmp(item.cFileName, L"..")) continue;
                if (item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
                const auto source = from + L"\\" + item.cFileName;
                const auto target = to + L"\\" + item.cFileName;
                if (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) Tree(source, target, replace);
                else Copy(source, target, replace);
            } while (FindNextFileW(search, &item));
        } catch (...) { FindClose(search); throw; }
        FindClose(search);
    }
    inline std::string Read(const std::wstring& path)
    {
        std::ifstream stream(path.c_str(), std::ios::binary);
        Check(stream.is_open(), path);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }
    inline std::wstring Wide(const std::string& value)
    {
        if (value.empty()) return {};
        std::wstring result(MultiByteToWideChar(CP_ACP, 0, value.data(), int(value.size()), nullptr, 0), L'\0');
        MultiByteToWideChar(CP_ACP, 0, value.data(), int(value.size()), &result[0], int(result.size()));
        return result;
    }
    inline std::string EnginePath(const std::wstring& value)
    {
        BOOL substituted = FALSE;
        const int size = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, value.data(), int(value.size()), nullptr, 0, nullptr, &substituted);
        std::string result(size, '\0');
        WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, value.data(), int(value.size()), &result[0], size, nullptr, &substituted);
        if (substituted || result.find_first_of("|\r\n") != std::string::npos)
            throw Failure{L"The game folder path is unsupported by the engine. Choose a folder with a shorter, simple path."};
        return result;
    }
    inline std::wstring PickFolder(HWND owner, const wchar_t* title)
    {
        // The editor may not have initialized COM on its UI thread yet.
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        IFileOpenDialog* dialog = nullptr;
        std::wstring result;
        const HRESULT created = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
        if (SUCCEEDED(created)) {
            dialog->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
            dialog->SetTitle(title);
            const HRESULT shown = dialog->Show(owner);
            if (SUCCEEDED(shown)) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(dialog->GetResult(&item))) {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                        result = path;
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
            dialog->Release();
            if (FAILED(shown) && shown != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
                if (SUCCEEDED(initialized)) CoUninitialize();
                throw Failure{L"Cannot open the InGame folder chooser."};
            }
        }
        if (SUCCEEDED(initialized)) CoUninitialize();
        if (FAILED(created)) throw Failure{L"Cannot open the InGame folder chooser."};
        return result;
    }
    inline bool Binaries(const std::wstring& root)
    {
        for (const auto* name : {L"SDKSceneHost.exe", L"xrEngine.exe", L"OpenAL32.dll", L"nvngx_dlss.dll", L"dbghelp.dll"})
            if (!File(root + L"\\bin\\" + name)) return false;
        return true;
    }
    inline bool Resources(const std::wstring& root)
    {
        // system.ltx comes from the SoC archives, not the loose OGSR overlay.
        return (File(root + L"\\defaults\\fsgame.ltx") && File(root + L"\\defaults\\preview.ltx") &&
                File(root + L"\\defaults\\gamedata\\config\\external.ltx")) ||
            (File(root + L"\\fsgame.ltx") && File(root + L"\\preview.ltx") &&
                File(Parent(root) + L"\\OGSR\\Game\\Resources_SoC_1.0006\\gamedata\\config\\external.ltx"));
    }
    inline bool Archives(const std::wstring& folder)
    {
        if (folder.empty()) return false;
        WIN32_FIND_DATAW item{};
        HANDLE search = FindFirstFileW((folder + L"\\gamedata.db*").c_str(), &item);
        if (search == INVALID_HANDLE_VALUE) return false;
        bool found = false;
        do { if (!(item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { found = true; break; } }
        while (FindNextFileW(search, &item));
        FindClose(search);
        return found;
    }
    inline std::wstring ArchiveSource(const std::string& config)
    {
        const std::string key = "$sdk_archive_source$";
        const size_t start = config.find(key);
        if (start == std::string::npos) return {};
        const size_t first = config.find('|', start);
        if (first == std::string::npos) return {};
        const size_t second = config.find('|', first + 1);
        if (second == std::string::npos) return {};
        std::string value = config.substr(second + 1, config.find_first_of("\r\n", second) - second - 1);
        const size_t begin = value.find_first_not_of(" \t");
        if (begin == std::string::npos) return {};
        value = value.substr(begin, value.find_last_not_of(" \t") - begin + 1);
        return Wide(value);
    }
    inline void Write(const std::wstring& path, const std::string& contents)
    {
        if (File(path) && Read(path) == contents) return;
        const auto temporary = path + L".setup-" + std::to_wstring(GetCurrentProcessId());
        {
            std::ofstream stream(temporary.c_str(), std::ios::binary | std::ios::trunc);
            stream.write(contents.data(), std::streamsize(contents.size()));
            stream.close();
            Check(!stream.fail(), temporary);
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str());
            Check(false, path);
        }
    }
    // Returns false with an empty error when the user cancels a folder chooser.
    inline bool Ensure(const std::wstring& sdk, HWND owner, std::wstring& error, bool chooseGame = false)
    {
        error.clear();
        try {
            const auto runtime = sdk + L"\\InGame";
            std::wstring source = runtime;
            if (!Binaries(runtime) || !Resources(runtime)) {
                // Development builds can locate the already built runtime in their source tree.
                auto ancestor = sdk;
                for (unsigned depth = 0; depth < 6 && !ancestor.empty(); ++depth, ancestor = Parent(ancestor)) {
                    if (Binaries(ancestor + L"\\InGame") && Resources(ancestor + L"\\InGame")) { source = ancestor + L"\\InGame"; break; }
                }
                if (!Binaries(source) || !Resources(source)) {
                    source = PickFolder(owner, L"Select the built InGame folder (containing bin), or its SDK/source folder");
                    if (source.empty()) return false;
                    if ((!Binaries(source) || !Resources(source)) && Binaries(source + L"\\InGame") && Resources(source + L"\\InGame")) source += L"\\InGame";
                    if (!Binaries(source) || !Resources(source)) throw Failure{L"This folder does not contain the complete InGame runtime.\nChoose a built SDK package with InGame/bin and InGame/defaults, or the SDK source folder with a built InGame runtime."};
                }
                for (const auto* name : {L"SDKSceneHost.exe", L"xrEngine.exe", L"OpenAL32.dll", L"nvngx_dlss.dll", L"dbghelp.dll"})
                    Copy(source + L"\\bin\\" + name, runtime + L"\\bin\\" + name, true);
            }
            const auto defaults = runtime + L"\\defaults";
            if (!File(defaults + L"\\fsgame.ltx")) {
                if (File(source + L"\\defaults\\fsgame.ltx")) Tree(source + L"\\defaults", defaults);
                else {
                    const auto overlay = Parent(source) + L"\\OGSR\\Game\\Resources_SoC_1.0006\\gamedata";
                    Tree(overlay, defaults + L"\\gamedata");
                    Tree(source + L"\\overrides", defaults + L"\\gamedata", true);
                    Copy(source + L"\\preview.ltx", defaults + L"\\preview.ltx");
                    Copy(source + L"\\fsgame.ltx", defaults + L"\\fsgame.ltx");
                }
            }
            Tree(defaults + L"\\gamedata", runtime + L"\\gamedata");
            Directory(runtime + L"\\_appdata_");
            Directory(runtime + L"\\mods");
            Copy(defaults + L"\\preview.ltx", runtime + L"\\_appdata_\\sdk-preview.ltx");
            Copy(defaults + L"\\fsgame.ltx", runtime + L"\\fsgame.ltx");
            const auto configPath = runtime + L"\\fsgame.runtime.ltx";
            std::string config = Read(File(configPath) ? configPath : runtime + L"\\fsgame.ltx");
            std::wstring archives = ArchiveSource(config);
            if (!Archives(archives)) {
                archives.clear();
                for (const auto& candidate : {runtime, sdk + L"\\game", Parent(sdk) + L"\\game"})
                    if (Archives(candidate)) { archives = candidate; break; }
            }
            if (chooseGame || archives.empty()) {
                archives = PickFolder(owner, L"Select S.T.A.L.K.E.R. Shadow of Chernobyl 1.0006 (folder with gamedata.db files)");
                if (archives.empty()) return false;
                if (!Archives(archives) && Archives(archives + L"\\game")) archives += L"\\game";
                if (!Archives(archives)) throw Failure{L"No gamedata.db* archives found in the selected folder.\nChoose the Shadow of Chernobyl 1.0006 game folder."};
            }
            while (!archives.empty() && (archives.back() == L'\\' || archives.back() == L'/')) archives.pop_back();
            const std::string alias = "$sdk_archive_source$ = false| false| " + EnginePath(archives) + "\\\r\n";
            const auto old = config.find("$sdk_archive_source$");
            if (old != std::string::npos) {
                const auto end = config.find('\n', old);
                config.replace(old, end == std::string::npos ? config.size() - old : end - old + 1, alias);
            } else {
                const auto at = config.find("$game_data$ =");
                if (at == std::string::npos) throw Failure{L"InGame fsgame.ltx is missing the $game_data$ alias."};
                config.insert(at, alias);
            }
            Write(configPath, config);
            return true;
        } catch (const Failure& failure) { error = failure.message; return false; }
    }
}
