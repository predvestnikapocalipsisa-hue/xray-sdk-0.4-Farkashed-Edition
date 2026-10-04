#include "stdafx.h"
#include "../UI/UISceneTabBar.h"

namespace
{
    bool scene_ready = false;
    DWORD crash_code = 0;
    void* crash_address = nullptr;

    int CrashFilter(EXCEPTION_POINTERS* info)
    {
        const DWORD code = info->ExceptionRecord->ExceptionCode;
        // Do not serialize with an exhausted stack or corrupt heap.
        if (IsDebuggerPresent() || code == EXCEPTION_STACK_OVERFLOW ||
            code == 0xC0000374 || code == 0xC0000409)
            return EXCEPTION_CONTINUE_SEARCH;
        crash_code = code;
        crash_address = info->ExceptionRecord->ExceptionAddress;
        return EXCEPTION_EXECUTE_HANDLER;
    }

    bool TryEmergencySave(string_path& path)
    {
        path[0] = '\0';
        __try
        {
            if (!scene_ready || !Scene || Scene->locked())
                return false;
            SYSTEMTIME now;
            GetLocalTime(&now);
            string_path name;
            xr_sprintf(name, sizeof(name), "last_session_%04u%02u%02u_%02u%02u%02u_%lu_fail.level",
                now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
                GetCurrentProcessId());
            FS.update_path(path, _maps_, name);
            Msg("! [CRASH] code=0x%08lX address=%p; attempting backup: %s",
                crash_code, crash_address, path);
            Scene->SaveBackup(path);
            FlushLog();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    int RunEditor()
    {
        if (!IsDebuggerPresent())
            Debug._initialize(false);
        Core.InitCore("level", ELogCallback);
        const bool sdkFactoryInitialized = !Core.SDKFallback;
        if (sdkFactoryInitialized)
            XrSE_Factory::initialize();
        LTools = xr_new<CLevelTool>();
        Tools = LTools;
        LUI = xr_new<CLevelMain>();
        UI = LUI;
        UI->RegisterCommands();
        Scene = xr_new<EScene>();
        UIMainForm* main_form = xr_new<UIMainForm>();
        ::MainForm = main_form;
        UI->Push(main_form, false);
        if (OpenLauncherDocument())
            UISceneTabBar::OnSceneLoaded(LTools->m_LastFileName.c_str(), nullptr);
        scene_ready = true;

        DWORD last_autosave_time = GetTickCount();
        const DWORD autosave_interval = 600000;
        while (main_form->Frame())
        {
            const DWORD current_time = GetTickCount();
            if (current_time - last_autosave_time >= autosave_interval)
            {
                last_autosave_time = current_time;
                if (Scene && !Scene->locked() && Scene->IsUnsaved())
                {
                    string_path path;
                    FS.update_path(path, _maps_, "last_autosave.level");
                    Msg("# [Autosave] Saving backup to %s", path);
                    Scene->SaveBackup(path);
                }
            }
        }
        scene_ready = false;
        xr_delete(main_form);
        if (sdkFactoryInitialized)
            XrSE_Factory::destroy();
        Core.DestroyCore();
        return 0;
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    __try
    {
        return RunEditor();
    }
    __except (CrashFilter(GetExceptionInformation()))
    {
        string_path path;
        const bool saved = TryEmergencySave(path);
        char message[2048];
        sprintf_s(message, sizeof(message),
            "Unrecoverable editor error (0x%08lX at %p).\n\n%s\n%s\n\n"
            "The editor will close. Use the last autosave if this recovery copy is incomplete.",
            crash_code, crash_address,
            saved ? "Emergency serialization completed. Check the recovery copy:" :
                "Emergency serialization failed or was unavailable. The copy may be incomplete:",
            path[0] ? path : "No recovery file was created.");
        MessageBoxA(NULL, message, "SDK recovery", MB_OK | MB_ICONERROR | MB_TASKMODAL);
        // Do not run destructors or another frame against potentially corrupt state.
        ExitProcess(1);
    }
    return 1;
}
