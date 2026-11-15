#include "EngineHookProtocol.h"
#include "../../External/Detours/detours.h"
#include <string>
#include <mutex>
#include <unordered_map>

namespace
{
    const SDKEngineHook::Payload* config = nullptr;
    decltype(&CreateFileW) OpenW = CreateFileW;
    decltype(&CreateFileA) OpenA = CreateFileA;
    decltype(&GetFileAttributesW) AttributesW = GetFileAttributesW;
    decltype(&GetFileAttributesA) AttributesA = GetFileAttributesA;
    decltype(&GetFileAttributesExW) AttributesExW = GetFileAttributesExW;
    decltype(&GetFileAttributesExA) AttributesExA = GetFileAttributesExA;
    decltype(&FindFirstFileExW) FirstExW = FindFirstFileExW;
    decltype(&FindFirstFileExA) FirstExA = FindFirstFileExA;
    decltype(&FindFirstFileW) FirstW = FindFirstFileW;
    decltype(&FindFirstFileA) FirstA = FindFirstFileA;
    decltype(&FindNextFileW) NextW = FindNextFileW;
    decltype(&FindNextFileA) NextA = FindNextFileA;
    decltype(&FindClose) CloseSearch = FindClose;

    struct Search
    {
        HANDLE game = INVALID_HANDLE_VALUE;
        WIN32_FIND_DATAW pending{};
        bool hasPending = false;
        std::wstring overlayDirectory;
    };
    std::mutex searchesMutex;
    std::unordered_map<HANDLE, Search> searches;

    std::wstring Wide(LPCSTR path)
    {
        if (!path) return {};
        const int length = MultiByteToWideChar(CP_ACP, 0, path, -1, nullptr, 0);
        if (!length) return {};
        std::wstring wide(length, L'\0');
        MultiByteToWideChar(CP_ACP, 0, path, -1, &wide[0], length);
        wide.pop_back();
        return wide;
    }

    std::wstring Redirect(LPCWSTR path)
    {
        if (!path || !*path) return {};
        if (wcsncmp(path, L"\\\\?\\", 4) == 0 && path[4] && path[5] == L':') path += 4;
        const DWORD length = GetFullPathNameW(path, 0, nullptr, nullptr);
        if (!length || length > 32768) return {};
        std::wstring full(length, L'\0');
        const DWORD size = GetFullPathNameW(path, length, &full[0], nullptr);
        if (!size || size >= length) return {};
        full.resize(size);
        const size_t root = wcslen(config->gameData);
        if (full.size() < root || CompareStringOrdinal(full.data(), int(root), config->gameData,
                int(root), TRUE) != CSTR_EQUAL) return {};
        if (size != root && full[root] != L'\\') return {};
        return std::wstring(config->resources) + full.substr(root);
    }

    std::wstring ExistingOverride(LPCWSTR path)
    {
        auto target = Redirect(path);
        if (target.empty() || AttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES) return {};
        return target;
    }

    bool IsReadOnly(DWORD access, DWORD disposition, DWORD flags)
    {
        constexpr DWORD writes = GENERIC_WRITE | GENERIC_ALL | MAXIMUM_ALLOWED | DELETE |
            WRITE_DAC | WRITE_OWNER | FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA |
            FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD;
        return disposition == OPEN_EXISTING && !(access & writes) && !(flags & FILE_FLAG_DELETE_ON_CLOSE);
    }

    HANDLE WINAPI OpenHookW(LPCWSTR path, DWORD access, DWORD sharing, LPSECURITY_ATTRIBUTES security,
        DWORD disposition, DWORD flags, HANDLE templateFile)
    {
        if (IsReadOnly(access, disposition, flags))
        {
            const auto target = ExistingOverride(path);
            if (!target.empty()) return OpenW(target.c_str(), access, sharing, security, disposition, flags, templateFile);
        }
        return OpenW(path, access, sharing, security, disposition, flags, templateFile);
    }

    HANDLE WINAPI OpenHookA(LPCSTR path, DWORD access, DWORD sharing, LPSECURITY_ATTRIBUTES security,
        DWORD disposition, DWORD flags, HANDLE templateFile)
    {
        if (IsReadOnly(access, disposition, flags))
        {
            const auto target = ExistingOverride(Wide(path).c_str());
            if (!target.empty()) return OpenW(target.c_str(), access, sharing, security, disposition, flags, templateFile);
        }
        return OpenA(path, access, sharing, security, disposition, flags, templateFile);
    }

    DWORD WINAPI AttributesHookW(LPCWSTR path)
    {
        const auto target = ExistingOverride(path);
        return AttributesW(target.empty() ? path : target.c_str());
    }

    DWORD WINAPI AttributesHookA(LPCSTR path)
    {
        const auto target = ExistingOverride(Wide(path).c_str());
        return target.empty() ? AttributesA(path) : AttributesW(target.c_str());
    }

    BOOL WINAPI AttributesExHookW(LPCWSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID result)
    {
        const auto target = ExistingOverride(path);
        return AttributesExW(target.empty() ? path : target.c_str(), level, result);
    }

    BOOL WINAPI AttributesExHookA(LPCSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID result)
    {
        const auto target = ExistingOverride(Wide(path).c_str());
        return target.empty() ? AttributesExA(path, level, result) : AttributesExW(target.c_str(), level, result);
    }

    HANDLE WINAPI FirstExHookW(LPCWSTR path, FINDEX_INFO_LEVELS info, LPVOID result, FINDEX_SEARCH_OPS op,
        LPVOID filter, DWORD flags)
    {
        const auto target = Redirect(path);
        if (target.empty() || !result || (info != FindExInfoStandard && info != FindExInfoBasic))
            return FirstExW(path, info, result, op, filter, flags);
        HANDLE overlay = FirstExW(target.c_str(), info, result, op, filter, flags);
        if (overlay == INVALID_HANDLE_VALUE) return FirstExW(path, info, result, op, filter, flags);

        Search search;
        search.game = FirstExW(path, info, &search.pending, op, filter, flags);
        search.hasPending = search.game != INVALID_HANDLE_VALUE;
        search.overlayDirectory = target.substr(0, target.find_last_of(L'\\') + 1);
        std::lock_guard<std::mutex> lock(searchesMutex);
        searches.emplace(overlay, std::move(search));
        return overlay;
    }

    BOOL WINAPI NextHookW(HANDLE handle, LPWIN32_FIND_DATAW data)
    {
        std::lock_guard<std::mutex> lock(searchesMutex);
        const auto found = searches.find(handle);
        if (found == searches.end()) return NextW(handle, data);
        if (NextW(handle, data)) return TRUE;
        auto& search = found->second;
        if (search.game != INVALID_HANDLE_VALUE)
        {
            while (search.hasPending || NextW(search.game, &search.pending))
            {
                search.hasPending = false;
                if (AttributesW((search.overlayDirectory + search.pending.cFileName).c_str()) == INVALID_FILE_ATTRIBUTES)
                {
                    *data = search.pending;
                    return TRUE;
                }
            }
        }
        SetLastError(ERROR_NO_MORE_FILES);
        return FALSE;
    }

    BOOL CopyFindData(const WIN32_FIND_DATAW& wide, WIN32_FIND_DATAA& ansi)
    {
        ansi = {};
        ansi.dwFileAttributes = wide.dwFileAttributes;
        ansi.ftCreationTime = wide.ftCreationTime;
        ansi.ftLastAccessTime = wide.ftLastAccessTime;
        ansi.ftLastWriteTime = wide.ftLastWriteTime;
        ansi.nFileSizeHigh = wide.nFileSizeHigh;
        ansi.nFileSizeLow = wide.nFileSizeLow;
        ansi.dwReserved0 = wide.dwReserved0;
        ansi.dwReserved1 = wide.dwReserved1;
        return WideCharToMultiByte(CP_ACP, 0, wide.cFileName, -1, ansi.cFileName, _countof(ansi.cFileName), nullptr, nullptr) &&
            WideCharToMultiByte(CP_ACP, 0, wide.cAlternateFileName, -1, ansi.cAlternateFileName,
                _countof(ansi.cAlternateFileName), nullptr, nullptr);
    }

    BOOL WINAPI CloseHook(HANDLE handle)
    {
        std::lock_guard<std::mutex> lock(searchesMutex);
        const auto found = searches.find(handle);
        if (found != searches.end())
        {
            if (found->second.game != INVALID_HANDLE_VALUE) CloseSearch(found->second.game);
            searches.erase(found);
        }
        return CloseSearch(handle);
    }

    HANDLE WINAPI FirstExHookA(LPCSTR path, FINDEX_INFO_LEVELS info, LPVOID result, FINDEX_SEARCH_OPS op,
        LPVOID filter, DWORD flags)
    {
        const auto widePath = Wide(path);
        if (Redirect(widePath.c_str()).empty() || !result || (info != FindExInfoStandard && info != FindExInfoBasic))
            return FirstExA(path, info, result, op, filter, flags);
        WIN32_FIND_DATAW wide{};
        HANDLE handle = FirstExHookW(widePath.c_str(), info, &wide, op, filter, flags);
        if (handle == INVALID_HANDLE_VALUE) return handle;
        if (CopyFindData(wide, *static_cast<WIN32_FIND_DATAA*>(result))) return handle;
        CloseHook(handle);
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return INVALID_HANDLE_VALUE;
    }

    HANDLE WINAPI FirstHookW(LPCWSTR path, LPWIN32_FIND_DATAW data)
    {
        if (Redirect(path).empty()) return FirstW(path, data);
        return FirstExHookW(path, FindExInfoStandard, data, FindExSearchNameMatch, nullptr, 0);
    }

    HANDLE WINAPI FirstHookA(LPCSTR path, LPWIN32_FIND_DATAA data)
    {
        if (Redirect(Wide(path).c_str()).empty()) return FirstA(path, data);
        return FirstExHookA(path, FindExInfoStandard, data, FindExSearchNameMatch, nullptr, 0);
    }

    BOOL WINAPI NextHookA(HANDLE handle, LPWIN32_FIND_DATAA data)
    {
        bool merged;
        {
            std::lock_guard<std::mutex> lock(searchesMutex);
            merged = searches.find(handle) != searches.end();
        }
        if (!merged) return NextA(handle, data);
        WIN32_FIND_DATAW wide{};
        return NextHookW(handle, &wide) && CopyFindData(wide, *data);
    }

    template <typename Function>
    bool Attach(Function& original, Function hook)
    {
        return DetourAttach(reinterpret_cast<PVOID*>(&original), reinterpret_cast<PVOID>(hook)) == NO_ERROR;
    }

    bool InstallHooks()
    {
        if (DetourTransactionBegin() != NO_ERROR) return false;
        if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR ||
            !Attach(OpenW, OpenHookW) || !Attach(OpenA, OpenHookA) ||
            !Attach(AttributesW, AttributesHookW) || !Attach(AttributesA, AttributesHookA) ||
            !Attach(AttributesExW, AttributesExHookW) || !Attach(AttributesExA, AttributesExHookA) ||
            !Attach(FirstExW, FirstExHookW) || !Attach(FirstExA, FirstExHookA) ||
            !Attach(FirstW, FirstHookW) || !Attach(FirstA, FirstHookA) ||
            !Attach(NextW, NextHookW) || !Attach(NextA, NextHookA) || !Attach(CloseSearch, CloseHook))
        {
            DetourTransactionAbort();
            return false;
        }
        return DetourTransactionCommit() == NO_ERROR;
    }
}

extern "C" BOOL WINAPI SDKLaunchExternalEngine(LPCWSTR engine, LPWSTR command, LPCWSTR directory,
    LPCSTR hookDll, const SDKEngineHook::Payload* payload, PROCESS_INFORMATION* process)
{
    STARTUPINFOW startup{sizeof(startup)};
    if (!DetourCreateProcessWithDllExW(engine, command, nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
            directory, &startup, process, hookDll, nullptr))
        return FALSE;
    if (DetourCopyPayloadToProcess(process->hProcess, SDKEngineHook::PayloadId,
            const_cast<SDKEngineHook::Payload*>(payload), sizeof(*payload)))
        return TRUE;
    const DWORD error = GetLastError();
    TerminateProcess(process->hProcess, error);
    CloseHandle(process->hThread);
    CloseHandle(process->hProcess);
    *process = {};
    SetLastError(error);
    return FALSE;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH || DetourIsHelperProcess()) return TRUE;
    DWORD size = 0;
    config = static_cast<const SDKEngineHook::Payload*>(DetourFindPayloadEx(SDKEngineHook::PayloadId, &size));
    if (!config) return TRUE; // The Level Editor loads this DLL to call the process launcher.
    if (size != sizeof(*config) || !DetourRestoreAfterWith()) return FALSE;
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, config->ready);
    if (!ready) return FALSE;
    const bool installed = InstallHooks();
    if (installed) SetEvent(ready);
    CloseHandle(ready);
    return installed;
}
