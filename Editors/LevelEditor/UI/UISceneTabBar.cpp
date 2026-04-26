#include "stdafx.h"
#include "UISceneTabBar.h"
#include "../color_editor.h"
#include "../Kernel/UI_LevelMain.h"

xr_vector<SceneTab> UISceneTabBar::s_tabs;
int                 UISceneTabBar::s_activeIdx  = -1;
float               UISceneTabBar::s_tabBarHeight = 0.f;

xr_string UISceneTabBar::NameFromPath(const char* path)
{
    if (!path || !path[0])
        return xr_string("Untitled");

    xr_string s(path);

    size_t slash = s.find_last_of("/\\");
    if (slash != xr_string::npos)
        s = s.substr(slash + 1);

    size_t dot = s.rfind('.');
    if (dot != xr_string::npos)
        s = s.substr(0, dot);

    return s.empty() ? xr_string("Untitled") : s;
}

void UISceneTabBar::OnSceneLoaded(const char* filePath, const char* displayName)
{

    if (filePath && filePath[0])
    {
        for (int i = 0; i < (int)s_tabs.size(); i++)
        {
            if (s_tabs[i].filePath == filePath)
            {
                s_activeIdx = i;
                return;
            }
        }
    }


    SceneTab tab;
    tab.filePath    = filePath ? filePath : "";
    tab.displayName = displayName ? displayName : NameFromPath(filePath).c_str();
    tab.isModified  = false;
    tab.wantClose   = false;

    s_tabs.push_back(tab);
    s_activeIdx = (int)s_tabs.size() - 1;
}

void UISceneTabBar::OnSceneSaved(const char* filePath)
{
    if (s_activeIdx < 0 || s_activeIdx >= (int)s_tabs.size())
        return;

    SceneTab& tab = s_tabs[s_activeIdx];
    if (filePath && filePath[0])
    {
        tab.filePath    = filePath;
        tab.displayName = NameFromPath(filePath).c_str();
    }
    tab.isModified = false;
}

void UISceneTabBar::OnSceneModified(bool modified)
{
    if (s_activeIdx >= 0 && s_activeIdx < (int)s_tabs.size())
        s_tabs[s_activeIdx].isModified = modified;
}

void UISceneTabBar::SwitchToTab(int idx)
{
    if (idx < 0 || idx >= (int)s_tabs.size())
        return;
    if (idx == s_activeIdx)
        return;

    if (s_activeIdx >= 0)
    {

        if (!Scene->IfModified())
            return;   

        if (!s_tabs[s_activeIdx].filePath.empty())
            ExecCommand(COMMAND_SAVE);
    }

    const xr_string& path = s_tabs[idx].filePath;
    if (!path.empty())
    {
        ExecCommand(COMMAND_LOAD, path);
    }
    else
    {
        ExecCommand(COMMAND_CLEAR);
    }

    s_activeIdx = idx;
}
bool UISceneTabBar::CloseTab(int idx)
{
    if (idx < 0 || idx >= (int)s_tabs.size())
        return false;

    bool isActive = (idx == s_activeIdx);

    if (isActive)
    {
        if (!Scene->IfModified())
            return false;
    }

    s_tabs.erase(s_tabs.begin() + idx);

    if (s_tabs.empty())
    {
        s_activeIdx = -1;
        ExecCommand(COMMAND_CLEAR);
        return true;
    }

    // Вычисляем новый активный индекс
    int newActive = s_activeIdx;
    if (idx < s_activeIdx)
        newActive--;
    else if (idx == s_activeIdx)
        newActive = (idx < (int)s_tabs.size()) ? idx : (int)s_tabs.size() - 1;

    if (isActive)
    {
        // Принудительно загружаем нужную сцену, минуя проверку idx == s_activeIdx
        s_activeIdx = -1; // сбрасываем чтобы SwitchToTab не вернулся сразу
        SwitchToTab(newActive);
    }
    else
    {
        s_activeIdx = newActive;
    }

    return true;
}

bool UISceneTabBar::Draw()
{
    if (s_tabs.empty())
    {
        s_tabBarHeight = 0.f;
        return false;
    }

    bool switched = false;

    const ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_TabActive];
    const ImVec4 accentHov = ImGui::GetStyle().Colors[ImGuiCol_TabHovered];
    const float  rounding = Colors::s_rounding;

    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, rounding - 2.f > 0.f ? rounding - 2.f : 2.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.f, 5.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f, 0.f));

    float barStartY = ImGui::GetCursorScreenPos().y;

    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_Tab]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accentHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent);
    if (ImGui::SmallButton("  +  "))
    {
        if (Scene->IfModified())
        {
            ExecCommand(COMMAND_CLEAR);
            SceneTab newTab;
            newTab.displayName = "Untitled";
            s_tabs.push_back(newTab);
            s_activeIdx = (int)s_tabs.size() - 1;
            switched = true;
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("New Scene");
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0.f, 8.f);

    ImGuiTabBarFlags tabFlags = ImGuiTabBarFlags_Reorderable
        | ImGuiTabBarFlags_FittingPolicyScroll
        | ImGuiTabBarFlags_TabListPopupButton;

    if (ImGui::BeginTabBar("##scene_tabs", tabFlags))
    {
        for (int i = 0; i < (int)s_tabs.size(); i++)
        {
            SceneTab& tab = s_tabs[i];

            char label[256];
            if (tab.isModified)
                _snprintf(label, sizeof(label), "\xE2\x97\x8F %s##stab%d", tab.displayName.c_str(), i);
            else
                _snprintf(label, sizeof(label), "%s##stab%d", tab.displayName.c_str(), i);

            ImGuiTabItemFlags itemFlags = ImGuiTabItemFlags_None;
            if (i == s_activeIdx)
                itemFlags |= ImGuiTabItemFlags_SetSelected;

            bool tabOpen = true;
            bool tabVisible = ImGui::BeginTabItem(label, &tabOpen, itemFlags);

            if (ImGui::IsItemHovered() && !tab.filePath.empty())
                ImGui::SetTooltip("%s", tab.filePath.c_str());

            if (tabVisible)
                ImGui::EndTabItem();

            if (ImGui::IsItemActivated() && i != s_activeIdx)
            {
                SwitchToTab(i);
                switched = true;
            }

            if (!tabOpen)
                tab.wantClose = true;
        }

        ImGui::EndTabBar();
    }

    for (int i = (int)s_tabs.size() - 1; i >= 0; i--)
    {
        if (s_tabs[i].wantClose)
        {
            CloseTab(i);
            break;
        }
    }

    s_tabBarHeight = ImGui::GetCursorScreenPos().y - barStartY + ImGui::GetStyle().ItemSpacing.y;

    ImGui::PopStyleVar(3);

    return switched;
}
