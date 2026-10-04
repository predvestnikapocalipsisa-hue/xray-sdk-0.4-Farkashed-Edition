// ShaderEditor.cpp : Определяет точку входа для приложения.

#include "stdafx.h"

extern "C" int __cdecl SDKEditorMain()
{
    if (!IsDebuggerPresent())
        Debug._initialize(false);

    Core.InitCore("shader", ELogCallback);
    STools = xr_new<CShaderTool>();
    Tools = STools;

    UI = xr_new<CShaderMain>();
    UI->RegisterCommands();

    UIMainForm *MainForm = xr_new<UIMainForm>();
    ::MainForm = MainForm;
    UI->Push(MainForm, false);

    while (MainForm->Frame())
    {
    }
    
    xr_delete(MainForm);
    Core.DestroyCore();
    return 0;
}
