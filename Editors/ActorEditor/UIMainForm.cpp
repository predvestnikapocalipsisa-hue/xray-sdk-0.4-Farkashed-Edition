#include "stdafx.h"
#include "../LevelEditor/color_editor.h"
#include "../XrEUI/imgui_internal.h"
#include "../XrECore/Editor/EditorChooseEvents.h"

namespace
{
    bool resetActorWorkspace = false;
    void ApplyActorWorkspace()
    {
        ImGuiWindow* host = ImGui::FindWindowByName("Master DockSpace");
        if (!host) return;
        const ImGuiID dock = host->GetID("MyDockspace");
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dock);
        static bool initialized = false;
        if (initialized && !resetActorWorkspace) return;
        if (host->Size.x < 640.f || host->Size.y < 400.f) return;
        ImGuiWindow* keys = ImGui::FindWindowByName("KeyForm");
        ImGuiWindow* tools = ImGui::FindWindowByName("LeftBar");
        // Saved windows are instantiated on their first Begin; inspect them next frame.
        if (!resetActorWorkspace && node && node->IsSplitNode() && (!keys || !tools)) return;
        // Migrate the old half-screen timeline and bottom tool strip once.
        const bool legacy = keys && tools && keys->DockId && tools->DockId &&
            keys->Size.y > host->Size.y*.65f && tools->Size.x > host->Size.x*.4f;
        initialized = true;
        if (!resetActorWorkspace && !legacy && node && node->IsSplitNode()) return;
        resetActorWorkspace = false;
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock,ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(dock,host->Pos);
        ImGui::DockBuilderSetNodeSize(dock,host->Size);
        ImGuiID center = dock;
        ImGuiID left = ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,.26f,nullptr,&center);
        const ImGuiID log = ImGui::DockBuilderSplitNode(left,ImGuiDir_Down,.24f,nullptr,&left);
        const ImGuiID timeline = ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,.24f,nullptr,&center);
        ImGui::DockBuilderDockWindow("LeftBar",left);
        ImGui::DockBuilderDockWindow("Render",center);
        ImGui::DockBuilderDockWindow("KeyForm",timeline);
        ImGui::DockBuilderDockWindow("Log",log);
        ImGui::DockBuilderFinish(dock);
    }
}
void UIMainForm::ResetWorkspace() { resetActorWorkspace = true; }

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
    m_KeyForm = xr_new<UIKeyForm>();
}

UIMainForm::~UIMainForm()
{
    ClearChooseEvents();
    xr_delete(m_KeyForm);
    xr_delete(m_LeftBar);
    xr_delete(m_MainMenu);
    xr_delete(m_Render);
    xr_delete(m_TopBar);
    ExecCommand(COMMAND_DESTROY, (u32)0, (u32)0);
}

void UIMainForm::Draw()
{

    ApplyActorWorkspace();
    m_MainMenu->Draw();
    m_TopBar->Draw();
    m_LeftBar->Draw();
    m_KeyForm->Draw();
    // ImGui::ShowDemoWindow(&bOpen);
    m_Render->Draw();
}

bool UIMainForm::Frame()
{
    if (UI)
        return UI->Idle();
    return false;
}
