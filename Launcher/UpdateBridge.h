#pragma once
#include "RecentFiles.h"
#include <shellapi.h>

namespace SDKUpdates
{
    // Run the updater from a disposable copy so its own executable can be replaced.
    inline bool Open(const std::wstring& target)
    {
        wchar_t temp[MAX_PATH] = {}, unique[MAX_PATH] = {};
        if (!GetTempPathW(MAX_PATH, temp) || !GetTempFileNameW(temp, L"xru", 0, unique)) return false;
        DeleteFileW(unique);
        if (!CreateDirectoryW(unique, nullptr)) return false;
        const std::wstring destination = unique;
        const std::wstring source = FarkashedLauncher::ModuleDirectory();
        const wchar_t* required[] = { L"Launcher.exe", L"LauncherAssets.dll", L"discord-rpc.dll" };
        for (const wchar_t* name : required)
            if (!CopyFileW((source + L"\\" + name).c_str(), (destination + L"\\" + name).c_str(), FALSE)) return false;
        // Preserve app-local Visual C++ runtimes when the SDK ships them instead
        // of relying on the system redistributable (including debug builds).
        const wchar_t* runtimes[] = { L"msvcp*.dll", L"vcruntime*.dll", L"concrt*.dll", L"ucrtbase*.dll" };
        for (const wchar_t* pattern : runtimes)
        {
            WIN32_FIND_DATAW file = {};
            HANDLE search = FindFirstFileW((source + L"\\" + pattern).c_str(), &file);
            if (search == INVALID_HANDLE_VALUE) continue;
            bool copied = true;
            do
            {
                if (!(file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                    !CopyFileW((source + L"\\" + file.cFileName).c_str(),
                        (destination + L"\\" + file.cFileName).c_str(), FALSE))
                { copied = false; break; }
            } while (FindNextFileW(search, &file));
            FindClose(search);
            if (!copied) return false;
        }
        const std::wstring args = L"--check-updates --update-target \"" + FarkashedLauncher::FullPath(target) + L"\"";
        return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", (destination + L"\\Launcher.exe").c_str(),
            args.c_str(), destination.c_str(), SW_SHOWNORMAL)) > 32;
    }
}
