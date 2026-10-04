// ActorEditor.cpp : Определяет точку входа для приложения.

#include "stdafx.h"

extern "C" int __cdecl SDKEditorMain()
{
    if (!IsDebuggerPresent())
        Debug._initialize(false);

    Core.InitCore("actor", ELogCallback);

    ATools = xr_new<CActorTools>();
    Tools = ATools;

    UI = xr_new<CActorMain>();
    UI->RegisterCommands();

    UIMainForm *MainForm = xr_new<UIMainForm>();
    ::MainForm = MainForm;
    UI->Push(MainForm, false);
    OpenLauncherDocument();

    while (MainForm->Frame())
    {
    }

    xr_delete(MainForm);
    Core.DestroyCore();
    return 0;
}
