#include "stdafx.h"

#include "../XrECore/Editor/EditorChooseEvents.h"

bool  UIMainForm::s_showSplash = true;
void* UIMainForm::g_splash_tex = nullptr;

UIMainForm *MainForm = nullptr;
UIMainForm::UIMainForm()
{
    EnableReceiveCommands();
    if (!ExecCommand(COMMAND_INITIALIZE, (u32)0, (u32)0))
    {
        FlushLog();
        exit(-1);
    }
    ExecCommand(COMMAND_UPDATE_GRID);
    ExecCommand(COMMAND_RENDER_FOCUS);
    FillChooseEvents();
    m_TopBar = xr_new<UITopBarForm>();
    m_Render = xr_new<UIRenderForm>();
    m_MainMenu = xr_new<UIMainMenuForm>();
    m_LeftBar = xr_new<UILeftBarForm>();
}

UIMainForm::~UIMainForm()
{
    ClearChooseEvents();
    xr_delete(m_LeftBar);
    xr_delete(m_MainMenu);
    xr_delete(m_Render);
    xr_delete(m_TopBar);
    ExecCommand(COMMAND_DESTROY, (u32)0, (u32)0);
}

void UIMainForm::Draw()
{

    if (s_showSplash)
    {
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

        ImGui::Begin("Splash", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);

        if (!g_splash_tex) {
            u32 mem = 0;
            g_splash_tex = RImplementation.texture_load("ui\\ui_sdk_overlay", mem);
        }

        ImVec2 screenSize = ImGui::GetIO().DisplaySize;
        if (g_splash_tex) {
            ImVec2 imgSize = { 1500, 500 };
            ImGui::SetCursorPos({ (screenSize.x - imgSize.x) * 0.5f, (screenSize.y - imgSize.y) * 0.5f });
            ImGui::Image((ImTextureID)g_splash_tex, imgSize);
        }

        if (ImGui::GetTime() > 5.0f)
            s_showSplash = false;

        ImGui::End();
        return;
    }

    m_MainMenu->Draw();
    m_TopBar->Draw();
    m_LeftBar->Draw();
    m_Render->Draw();
}

bool UIMainForm::Frame()
{
    if (UI)
        return UI->Idle();
    return false;
}
