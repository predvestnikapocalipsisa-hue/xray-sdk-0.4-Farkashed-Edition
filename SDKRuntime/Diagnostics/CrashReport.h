#pragma once
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cwchar>
#include <cstring>

// Kept independent of the SDK allocator, filesystem, logger and UI: those may
// already be damaged when an exception reaches the editor's outer boundary.
namespace SDKCrashReport
{
    struct State
    {
        wchar_t directory[32768]{};
        decltype(&MiniDumpWriteDump) dump = nullptr;
        volatile LONG writing = 0;
    };
    inline State& GetState() { static State state; return state; }

    inline bool Initialize(const wchar_t* directory = nullptr)
    {
        State& state = GetState();
        if (state.directory[0]) return true;
        wchar_t path[32768]{};
        if (directory)
        {
            if (wcslen(directory) >= _countof(path)) return false;
            wcscpy_s(path, directory);
        }
        else
        {
            const DWORD size = GetModuleFileNameW(nullptr, path, _countof(path));
            if (!size || size >= _countof(path)) return false;
            wchar_t* slash = wcsrchr(path, L'\\');
            if (!slash) return false;
            *slash = 0;
            if (wcscat_s(path, L"\\crashes")) return false;
        }
        if (!CreateDirectoryW(path, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        {
            if (directory) return false;
            const DWORD size = GetTempPathW(_countof(path), path);
            if (!size || size >= _countof(path) || wcscat_s(path, L"XRaySDK-crashes")) return false;
            if (!CreateDirectoryW(path, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
        }
        // Load before a crash, from Windows rather than a user runtime DLL.
        HMODULE library = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (library) state.dump = reinterpret_cast<decltype(state.dump)>(GetProcAddress(library, "MiniDumpWriteDump"));
        wcscpy_s(state.directory, path);
        return true;
    }

    inline void Write(EXCEPTION_POINTERS* info)
    {
        State& state = GetState();
        if (!info || !info->ExceptionRecord || !state.directory[0] ||
            InterlockedCompareExchange(&state.writing, 1, 0)) return;
        // A stack overflow has no safe in-process reporting stack.
        if (info->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) return;
        wchar_t base[2048]{};
        SYSTEMTIME time{};
        GetLocalTime(&time);
        if (_snwprintf_s(base, _countof(base), _TRUNCATE,
            L"%s\\editor-%04u%02u%02u-%02u%02u%02u-%03u-%lu-%lu",
            state.directory, time.wYear, time.wMonth, time.wDay, time.wHour,
            time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentProcessId(), GetCurrentThreadId()) < 0) return;
        wchar_t path[2048]{};
        _snwprintf_s(path, _countof(path), _TRUNCATE, L"%s.txt", base);
        HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            char report[4096]{};
            MEMORY_BASIC_INFORMATION memory{};
            void* address = info->ExceptionRecord->ExceptionAddress;
            VirtualQuery(address, &memory, sizeof(memory));
            wchar_t module[1024]{};
            if (memory.AllocationBase) GetModuleFileNameW(static_cast<HMODULE>(memory.AllocationBase), module, _countof(module));
            const int length = _snprintf_s(report, sizeof(report), _TRUNCATE,
                "Exception: 0x%08lX\r\nAddress: %p\r\nModule: %ls\r\nModule offset: 0x%llX\r\nProcess: %lu\r\nThread: %lu\r\n",
                info->ExceptionRecord->ExceptionCode, address, module,
                static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(address) - reinterpret_cast<ULONG_PTR>(memory.AllocationBase)),
                GetCurrentProcessId(), GetCurrentThreadId());
            DWORD written = 0;
            WriteFile(file, report, length < 0 ? DWORD(strlen(report)) : DWORD(length), &written, nullptr);
            FlushFileBuffers(file);
            CloseHandle(file);
        }
        if (!state.dump) return;
        _snwprintf_s(path, _countof(path), _TRUNCATE, L"%s.dmp", base);
        file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
        state.dump(GetCurrentProcess(), GetCurrentProcessId(), file,
            MINIDUMP_TYPE(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules), &exception, nullptr, nullptr);
        FlushFileBuffers(file);
        CloseHandle(file);
    }
}
