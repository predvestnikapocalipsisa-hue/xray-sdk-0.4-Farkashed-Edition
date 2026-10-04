#pragma once

// Shared by the standalone launcher and the editors. No X-Ray runtime required.
#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

namespace FarkashedLauncher
{
    constexpr size_t MaxRecentFiles = 25;

    struct RecentFile
    {
        std::wstring path;
        std::wstring editor;
        std::wstring sdkDirectory;
        ULONGLONG opened = 0;
    };

    inline std::wstring ModuleDirectory()
    {
        std::vector<wchar_t> path(32768);
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
        if (!length || length >= path.size())
            return {};
        std::wstring result(path.data(), length);
        const size_t slash = result.find_last_of(L"\\/");
        return result.substr(0, slash == 2 && result[1] == L':' ? slash + 1 : slash);
    }

    inline std::wstring FullPath(const std::wstring& path)
    {
        if (path.empty())
            return {};
        const DWORD length = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
        if (!length)
            return {};
        std::vector<wchar_t> buffer(length);
        const DWORD written = GetFullPathNameW(path.c_str(), length, buffer.data(), nullptr);
        return written && written < length ? std::wstring(buffer.data(), written) : std::wstring();
    }

    inline bool FileExists(const std::wstring& path)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    }

    inline std::wstring FromAnsi(const char* value)
    {
        if (!value || !*value)
            return {};
        const int length = MultiByteToWideChar(CP_ACP, 0, value, -1, nullptr, 0);
        if (length <= 1)
            return {};
        std::vector<wchar_t> buffer(length);
        MultiByteToWideChar(CP_ACP, 0, value, -1, buffer.data(), length);
        return buffer.data();
    }

    inline std::wstring HistoryPath()
    {
        PWSTR local = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local)))
            return {};
        std::wstring directory = std::wstring(local) + L"\\FarkashedSDK";
        CoTaskMemFree(local);
        if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            return {};
        const std::wstring path = directory + L"\\Launcher.ini";
        // A BOM makes the profile APIs retain Unicode paths instead of using ANSI.
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            const WORD bom = 0xfeff;
            DWORD written = 0;
            WriteFile(file, &bom, sizeof(bom), &written, nullptr);
            CloseHandle(file);
        }
        return path;
    }

    class HistoryLock
    {
        HANDLE handle = nullptr;
        bool acquired = false;
    public:
        HistoryLock()
        {
            handle = CreateMutexW(nullptr, FALSE, L"Local\\FarkashedSDK.Launcher.History");
            if (handle)
            {
                const DWORD result = WaitForSingleObject(handle, 1000);
                acquired = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
            }
        }
        ~HistoryLock()
        {
            if (acquired)
                ReleaseMutex(handle);
            if (handle)
                CloseHandle(handle);
        }
        explicit operator bool() const { return acquired; }
        HistoryLock(const HistoryLock&) = delete;
        HistoryLock& operator=(const HistoryLock&) = delete;
    };

    inline std::wstring ReadValue(const std::wstring& file, const wchar_t* section, const wchar_t* key)
    {
        if (file.empty())
            return {};
        std::vector<wchar_t> buffer(32768);
        GetPrivateProfileStringW(section, key, L"", buffer.data(), DWORD(buffer.size()), file.c_str());
        return buffer.data();
    }

    inline bool ValidEditor(const std::wstring& editor)
    {
        return editor == L"level" || editor == L"actor" || editor == L"particle";
    }

    inline bool SameFile(const RecentFile& a, const RecentFile& b)
    {
        return a.editor == b.editor && _wcsicmp(a.path.c_str(), b.path.c_str()) == 0 &&
            _wcsicmp(a.sdkDirectory.c_str(), b.sdkDirectory.c_str()) == 0;
    }

    inline std::vector<RecentFile> ReadHistoryUnlocked(const std::wstring& file)
    {
        std::vector<RecentFile> files;
        for (size_t i = 0; i < MaxRecentFiles; ++i)
        {
            const std::wstring section = L"Recent" + std::to_wstring(i);
            RecentFile entry;
            entry.path = ReadValue(file, section.c_str(), L"Path");
            entry.editor = ReadValue(file, section.c_str(), L"Editor");
            entry.sdkDirectory = ReadValue(file, section.c_str(), L"SDKDirectory");
            entry.opened = _wcstoui64(ReadValue(file, section.c_str(), L"Opened").c_str(), nullptr, 10);
            if (!entry.path.empty() && !entry.sdkDirectory.empty() && ValidEditor(entry.editor))
                files.push_back(std::move(entry));
        }
        std::stable_sort(files.begin(), files.end(), [](const RecentFile& a, const RecentFile& b)
        {
            return a.opened > b.opened;
        });
        return files;
    }

    inline bool WriteHistoryUnlocked(const std::wstring& file, const std::vector<RecentFile>& files)
    {
        if (file.empty())
            return false;
        bool success = true;
        for (size_t i = 0; i < MaxRecentFiles; ++i)
        {
            const std::wstring section = L"Recent" + std::to_wstring(i);
            if (i >= files.size())
            {
                success = !!WritePrivateProfileStringW(section.c_str(), nullptr, nullptr, file.c_str()) && success;
                continue;
            }
            const RecentFile& entry = files[i];
            const std::wstring opened = std::to_wstring(entry.opened);
            success = !!WritePrivateProfileStringW(section.c_str(), L"Path", entry.path.c_str(), file.c_str()) && success;
            success = !!WritePrivateProfileStringW(section.c_str(), L"Editor", entry.editor.c_str(), file.c_str()) && success;
            success = !!WritePrivateProfileStringW(section.c_str(), L"SDKDirectory", entry.sdkDirectory.c_str(), file.c_str()) && success;
            success = !!WritePrivateProfileStringW(section.c_str(), L"Opened", opened.c_str(), file.c_str()) && success;
        }
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, file.c_str());
        return success;
    }

    inline std::vector<RecentFile> ReadHistory()
    {
        HistoryLock lock;
        return lock ? ReadHistoryUnlocked(HistoryPath()) : std::vector<RecentFile>();
    }

    inline bool Remember(const std::wstring& path, const std::wstring& editor,
        const std::wstring& sdkDirectory = ModuleDirectory(), ULONGLONG timestamp = 0)
    {
        RecentFile entry;
        entry.path = FullPath(path);
        entry.editor = editor;
        entry.sdkDirectory = FullPath(sdkDirectory);
        if (!ValidEditor(editor) || entry.sdkDirectory.empty() || !FileExists(entry.path))
            return false;
        FILETIME time;
        GetSystemTimeAsFileTime(&time);
        entry.opened = timestamp ? timestamp : (ULONGLONG(time.dwHighDateTime) << 32) | time.dwLowDateTime;
        HistoryLock lock;
        if (!lock)
            return false;
        const std::wstring file = HistoryPath();
        auto files = ReadHistoryUnlocked(file);
        const auto previous = std::find_if(files.begin(), files.end(), [&](const RecentFile& other)
        {
            return SameFile(entry, other);
        });
        if (previous != files.end())
            entry.opened = (std::max)(entry.opened, previous->opened);
        files.erase(std::remove_if(files.begin(), files.end(), [&](const RecentFile& other)
        {
            return SameFile(entry, other);
        }), files.end());
        files.push_back(std::move(entry));
        std::stable_sort(files.begin(), files.end(), [](const RecentFile& a, const RecentFile& b)
        {
            return a.opened > b.opened;
        });
        if (files.size() > MaxRecentFiles)
            files.resize(MaxRecentFiles);
        return WriteHistoryUnlocked(file, files);
    }

    inline bool Forget(const RecentFile& entry)
    {
        HistoryLock lock;
        if (!lock)
            return false;
        const std::wstring file = HistoryPath();
        auto files = ReadHistoryUnlocked(file);
        files.erase(std::remove_if(files.begin(), files.end(), [&](const RecentFile& other)
        {
            return SameFile(entry, other);
        }), files.end());
        return WriteHistoryUnlocked(file, files);
    }

    inline std::wstring Setting(const wchar_t* key)
    {
        HistoryLock lock;
        return lock ? ReadValue(HistoryPath(), L"Launcher", key) : std::wstring();
    }

    inline bool SetSetting(const wchar_t* key, const std::wstring& value)
    {
        HistoryLock lock;
        if (!lock)
            return false;
        const std::wstring file = HistoryPath();
        return !file.empty() && !!WritePrivateProfileStringW(L"Launcher", key, value.c_str(), file.c_str());
    }
}
