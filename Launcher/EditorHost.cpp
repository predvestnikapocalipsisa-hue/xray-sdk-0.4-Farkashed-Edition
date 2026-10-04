#include "EditorHost.h"
#include "RecentFiles.h"
#include <windows.h>
#include <shellapi.h>
#include <string>

bool RunSDKEditorHost(int& exitCode)
{
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments)
        return false;
    std::wstring editor;
    bool host = false;
    for (int i = 1; i < count; ++i)
        if (wcscmp(arguments[i], L"--sdk-editor") == 0)
        {
            host = true;
            if (i + 1 < count)
                editor = arguments[++i];
            break;
        }
    LocalFree(arguments);
    if (!host)
        return false;

    const wchar_t* moduleName = nullptr;
    if (editor == L"actor") moduleName = L"ActorEditor.dll";
    if (editor == L"level") moduleName = L"LevelEditor.dll";
    if (editor == L"particle") moduleName = L"ParticleEditor.dll";
    if (editor == L"shader") moduleName = L"ShaderEditor.dll";
    exitCode = 1;
    if (!moduleName)
    {
        MessageBoxW(nullptr, L"Unknown SDK editor.", L"SDK Launcher", MB_OK | MB_ICONERROR);
        return true;
    }
    const std::wstring directory = FarkashedLauncher::ModuleDirectory();
    SetCurrentDirectoryW(directory.c_str());
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    const std::wstring path = directory + L"\\" + moduleName;
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module)
    {
        const DWORD error = GetLastError();
        const std::wstring message = L"Could not load " + path +
            L".\nWindows error: " + std::to_wstring(error) +
            L"\n\nCheck that all DLLs belong to the same SDK build and architecture.";
        MessageBoxW(nullptr, message.c_str(), L"SDK Launcher", MB_OK | MB_ICONERROR);
        return true;
    }
    using EditorEntry = int(__cdecl*)();
    const auto entry = reinterpret_cast<EditorEntry>(GetProcAddress(module, "SDKEditorMain"));
    if (!entry)
    {
        MessageBoxW(nullptr, L"The editor module does not export SDKEditorMain.",
            L"SDK Launcher", MB_OK | MB_ICONERROR);
        return true;
    }
    exitCode = entry();
    // Keep the module loaded until process exit: SDK globals and outstanding
    // worker threads must never refer to code unloaded by the launcher.
    return true;
}
