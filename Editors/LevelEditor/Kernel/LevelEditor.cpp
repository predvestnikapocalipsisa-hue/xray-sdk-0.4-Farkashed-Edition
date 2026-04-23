#include "stdafx.h"

class ISE_Abstract;


int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    if (!IsDebuggerPresent())
        Debug._initialize(false);

    Core.InitCore("level", ELogCallback);
    XrSE_Factory::initialize();

    LTools = xr_new<CLevelTool>();
    Tools = LTools;

    LUI = xr_new<CLevelMain>();
    UI = LUI;
    UI->RegisterCommands();

    Scene = xr_new<EScene>();
    UIMainForm* MainForm = xr_new<UIMainForm>();

    ::MainForm = MainForm;
    UI->Push(MainForm, false);

    u32 last_autosave_time = 0;
    u32 autosave_interval = 60000;

    __try
    {
        while (MainForm->Frame())
        {

            if (last_autosave_time == 0)
            {
                last_autosave_time = EDevice.dwTimeGlobal;
                continue;
            }


            u32 current_time = EDevice.dwTimeGlobal;

            if (current_time - last_autosave_time > autosave_interval)
            {
                last_autosave_time = current_time;

                if (Scene)
                {
                    string_path fn;
                    LPCSTR map_name = ("last");
                    xr_sprintf(fn, sizeof(fn), "%s_autosave.level", map_name);

                    string_path full_path;
                    FS.update_path(full_path, _maps_, fn);

                    Msg("# [Autosave]: Saving to %s", full_path);

                    if (Core.SocSdk)
                        Scene->Save(full_path, false, false);
                    else
                        Scene->SaveLTX(full_path, false, false);
                            }
                        }
                    }
                }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
            string_path fn;
            string_path full_path;

            LPCSTR map_name = ("last_session");
            xr_sprintf(fn, sizeof(fn), "%s_fail.level", map_name);

            FS.update_path(full_path, _maps_, fn);

            Msg("! [CRASH]: Attempting emergency save to: %s", full_path);

            if (Core.SocSdk)
                Scene->Save(full_path, false, false);
            else
                Scene->SaveLTX(full_path, false, false);

            MessageBoxA(NULL, "Редактор вылетел! Сцена была сохранена в папку maps с припиской _fail.level", "Emergency Save", MB_OK | MB_ICONERROR);
    }

    while (MainForm->Frame()) 
    {
    }

    xr_delete(MainForm);
    XrSE_Factory::destroy();
    Core.DestroyCore();
    return 0;
}
