// Build as both x86 and x64. Run either host binary with the target binary,
// the host-bitness SDKInGameHook DLL and a fresh, empty scratch directory.
#include "../EngineHookProtocol.h"
#include "../ExternalEngine.h"
#include <cstdio>
#include <set>
#include <stdexcept>

namespace
{
    void Require(bool ok, const char* operation)
    {
        if (!ok) throw std::runtime_error(operation);
    }

    void Put(const std::wstring& path, char value)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        Require(file != INVALID_HANDLE_VALUE, "create file");
        DWORD count = 0;
        const BOOL written = WriteFile(file, &value, 1, &count, nullptr);
        CloseHandle(file);
        Require(written && count == 1, "write file");
    }

    char Read(HANDLE file)
    {
        Require(file != INVALID_HANDLE_VALUE, "open file");
        char value = 0;
        DWORD count = 0;
        const BOOL read = ReadFile(file, &value, 1, &count, nullptr);
        CloseHandle(file);
        Require(read && count == 1, "read file");
        return value;
    }

    char Get(const std::wstring& path)
    {
        return Read(CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    }

    void VerifyChild()
    {
        Require(Get(L"gamedata\\same.txt") == 's', "SDK override");
        Require(Get(L"gamedata\\stock.txt") == 'g', "game fallback");
        Require(Get(L"gamedata\\added.txt") == 'n', "new SDK file");
        Require(Read(CreateFileA("gamedata\\same.txt", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr))
            == 's', "ANSI SDK override");
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        Require(GetFileAttributesExA("gamedata\\added.txt", GetFileExInfoStandard, &attributes) &&
            attributes.nFileSizeLow == 1, "ANSI SDK file attributes");
        WIN32_FIND_DATAA data{};
        HANDLE find = FindFirstFileA("gamedata\\*.txt", &data);
        Require(find != INVALID_HANDLE_VALUE, "merged enumeration");
        std::set<std::string> entries;
        do { Require(entries.insert(data.cFileName).second, "duplicate enumeration entry"); }
        while (FindNextFileA(find, &data));
        Require(GetLastError() == ERROR_NO_MORE_FILES && FindClose(find), "close merged enumeration");
        Require(entries == std::set<std::string>{"same.txt", "stock.txt", "added.txt"}, "enumeration entries");
        WIN32_FIND_DATAW nested{};
        find = FindFirstFileW(L"gamedata\\new_folder\\*.ltx", &nested);
        Require(find != INVALID_HANDLE_VALUE && wcscmp(nested.cFileName, L"new.ltx") == 0,
            "virtual directory enumeration");
        Require(FindClose(find) != FALSE, "close virtual directory enumeration");
        Put(L"gamedata\\same.txt", 'w');
        Require(Get(L"gamedata\\same.txt") == 's', "SDK override after game write");
    }
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        if (argc == 2 && wcscmp(argv[1], L"child") == 0) { VerifyChild(); return 0; }
        if (argc == 3 && wcscmp(argv[1], L"identify-ix") == 0)
        {
            Require(SDKExternalEngine::IsIXRay(argv[2]), "recognize IX-Ray binary");
            Require(wcscmp(SDKExternalEngine::IXRayMessage(),
                L"\u041D\u0435 \u043F\u043E\u0437\u043E\u0440\u044C\u0441\u044F, \u0431\u0435\u0437\u0434\u0430\u0440\u044C.") == 0,
                "exact IX-Ray error text");
            return 0;
        }
        Require(argc == 4, "expected target.exe hook.dll scratch-dir");
        const std::wstring scratch = argv[3];
        Require(CreateDirectoryW(scratch.c_str(), nullptr) != FALSE, "create scratch directory");
        const auto game = scratch + L"\\gamedata";
        const auto overlay = scratch + L"\\overlay";
        Require(CreateDirectoryW(game.c_str(), nullptr) && CreateDirectoryW(overlay.c_str(), nullptr) &&
            CreateDirectoryW((overlay + L"\\new_folder").c_str(), nullptr), "create gamedata directories");
        Put(game + L"\\same.txt", 'g');
        Put(game + L"\\stock.txt", 'g');
        Put(overlay + L"\\same.txt", 's');
        Put(overlay + L"\\added.txt", 'n');
        Put(overlay + L"\\new_folder\\new.ltx", 'x');

        HMODULE module = LoadLibraryW(argv[2]);
        Require(module != nullptr, "load hook DLL");
        const auto start = reinterpret_cast<SDKEngineHook::Launch>(GetProcAddress(module, "SDKLaunchExternalEngine"));
        Require(start != nullptr, "find launch entry");
        SDKEngineHook::Payload payload{};
        wcscpy_s(payload.gameData, game.c_str());
        wcscpy_s(payload.resources, overlay.c_str());
        swprintf_s(payload.ready, L"Local\\XRaySDK.HookTest.%lu", GetCurrentProcessId());
        HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, payload.ready);
        Require(ready != nullptr, "create readiness event");
        PROCESS_INFORMATION process{};
        std::wstring command = L"\"" + std::wstring(argv[1]) + L"\" child";
        const auto dll = SDKInGameSetup::EnginePath(argv[2]);
        Require(start(argv[1], &command[0], scratch.c_str(), dll.c_str(), &payload, &process) != FALSE,
            "launch hooked test process");
        ResumeThread(process.hThread);
        CloseHandle(process.hThread);
        const DWORD wait = WaitForSingleObject(process.hProcess, 10000);
        if (wait != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1);
        DWORD code = 0;
        GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hProcess);
        const bool initialized = WaitForSingleObject(ready, 0) == WAIT_OBJECT_0;
        CloseHandle(ready);
        FreeLibrary(module);
        if (code) fprintf(stderr, "Hook target exit code: 0x%lx\n", code);
        Require(initialized && wait == WAIT_OBJECT_0 && code == 0, "verify hook target");
        Require(Get(game + L"\\same.txt") == 'w', "game receives writes");
        Require(Get(overlay + L"\\same.txt") == 's', "SDK resources stay read-only");
        puts("External engine hook verified.");
        return 0;
    }
    catch (const std::exception& error)
    {
        fprintf(stderr, "%s (Win32 error %lu)\n", error.what(), GetLastError());
        return 1;
    }
}
