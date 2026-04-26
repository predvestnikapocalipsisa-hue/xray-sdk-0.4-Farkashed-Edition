#include "stdafx.h"
#include "../XrECore/Editor/EditorChooseEvents.h"
#include "UISceneTabBar.h"

UIMainForm* MainForm = nullptr;

bool                UIMainForm::s_showSplash = true;
void* UIMainForm::g_splash_tex = nullptr;
float               UIMainForm::s_splashAlpha = 1.0f;
SplashAnimCallback UIMainForm::g_animCallback = nullptr;

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
    m_Properties = xr_new<UILPropertiesFrom>();

    m_Render->SetContextMenuEvent(TOnRenderContextMenu(this, &UIMainForm::DrawContextMenu));

    if (dynamic_cast<CLevelPreferences*>(EPrefs)->OpenObjectList)
        UIObjectList::Show();

    if (!dynamic_cast<CLevelPreferences*>(EPrefs)->OpenProperties)
        m_Properties->Close();
}

UIMainForm::~UIMainForm()
{
    dynamic_cast<CLevelPreferences*>(EPrefs)->OpenProperties = !m_Properties->IsClosed();
    dynamic_cast<CLevelPreferences*>(EPrefs)->OpenObjectList = UIObjectList::IsOpen();

    ClearChooseEvents();
    xr_delete(m_Properties);
    xr_delete(m_LeftBar);
    xr_delete(m_MainMenu);
    xr_delete(m_Render);
    xr_delete(m_TopBar);

    ExecCommand(COMMAND_DESTROY, (u32)0, (u32)0);
}

void UIMainForm::DrawSplash()
{
    const float  kFadeStart = 4.5f;
    const float  kFadeDur = 2.5f;
    const double now = ImGui::GetTime();

    if (now < kFadeStart)
        s_splashAlpha = 1.0f;
    else
    {
        float t = (float)((now - kFadeStart) / kFadeDur);
        s_splashAlpha = 1.0f - t;
        if (s_splashAlpha <= 0.f)
        {
            s_splashAlpha = 0.f;
            s_showSplash = false;
            return;
        }
    }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({ 0.f, 0.f });
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.f, 0.f });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##splash_overlay", nullptr, flags);
    ImGui::PopStyleVar(2);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = { 0.f, 0.f };
    const ImVec2 p1 = io.DisplaySize;
    const float  alpha = s_splashAlpha;

    dl->AddRectFilled(p0, p1, IM_COL32(5, 7, 12, (int)(alpha * 245)));

    dl->AddRectFilledMultiColor(p0, p1,
        IM_COL32(0, 0, 0, (int)(alpha * 160)), IM_COL32(0, 0, 0, (int)(alpha * 160)),
        IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));

    dl->AddRectFilledMultiColor(p0, p1,
        IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
        IM_COL32(0, 0, 0, (int)(alpha * 160)), IM_COL32(0, 0, 0, (int)(alpha * 160)));

    {
        const float lineY = p1.y * 0.72f;
        const ImU32 glowA = IM_COL32(64, 128, 255, (int)(alpha * 60));
        const ImU32 glowB = IM_COL32(64, 128, 255, 0);
        dl->AddRectFilledMultiColor({ p0.x, lineY - 1.f }, { p1.x, lineY + 1.f }, glowB, glowB, glowA, glowA);
        dl->AddRectFilledMultiColor({ p0.x, lineY + 1.f }, { p1.x, lineY + 18.f }, glowA, glowA, glowB, glowB);
    }

    if (!g_splash_tex)
    {
        u32 mem = 0;
        g_splash_tex = RImplementation.texture_load("ui\\ui_sdk_overlay", mem);
    }

    if (g_splash_tex)
    {
        const ImVec2 imgSz = { 1500.f, 500.f };
        const ImVec2 imgPos = { (p1.x - imgSz.x) * 0.5f, (p1.y - imgSz.y) * 0.5f };
        dl->AddImage((ImTextureID)g_splash_tex, imgPos, { imgPos.x + imgSz.x, imgPos.y + imgSz.y }, { 0.f, 0.f }, { 1.f, 1.f }, IM_COL32(255, 255, 255, (int)(alpha * 255)));
    }

    {
        static const char* kDots[] = { "Loading.", "Loading..", "Loading..." };
        const char* label = kDots[(int)(now * 2.5) % 3];
        ImVec2 tsz = ImGui::CalcTextSize(label);
        ImVec2 tp = { p1.x - tsz.x - 30.f, p1.y - tsz.y - 24.f };
        dl->AddText(tp, IM_COL32(180, 200, 240, (int)(alpha * 200)), label);
    }

    if (g_animCallback)
        g_animCallback(dl, p0, p1, (float)now);

    ImGui::End();
}

void UIMainForm::Draw()
{
    m_MainMenu->Draw(); 

    m_TopBar->Draw();
    m_LeftBar->Draw();
    m_Properties->Draw();

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

        if (ImGui::GetTime() > 8.0f)
            s_showSplash = false;

        ImGui::End();
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(400, 100), ImGuiCond_FirstUseEver); 
    
    if (ImGui::Begin("Scene Manager")) 
    {
        UISceneTabBar::Draw(); 
    }
    ImGui::End();

    m_Render->Draw();
}

bool UIMainForm::Frame()
{
    if (UI)
        return UI->Idle();
    return false;
}

void UIMainForm::DrawContextMenu()
{
    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem("Copy"))    ExecCommand(COMMAND_COPY);
        if (ImGui::MenuItem("Paste"))   ExecCommand(COMMAND_PASTE);
        ImGui::Separator();
        if (ImGui::MenuItem("Cut"))     ExecCommand(COMMAND_CUT);
        ImGui::Separator();
        if (ImGui::MenuItem("Delete"))  ExecCommand(COMMAND_DELETE_SELECTION);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Visiblity"))
    {
        if (ImGui::MenuItem("Hide Selected"))   ExecCommand(COMMAND_HIDE_SEL, FALSE);
        if (ImGui::MenuItem("Hide Unselected")) ExecCommand(COMMAND_HIDE_UNSEL);
        if (ImGui::MenuItem("Hide All"))        ExecCommand(COMMAND_HIDE_ALL, FALSE);
        ImGui::Separator();
        if (ImGui::MenuItem("Unhide All"))      ExecCommand(COMMAND_HIDE_ALL, TRUE);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Properties"))
        ExecCommand(COMMAND_SHOW_PROPERTIES);
}