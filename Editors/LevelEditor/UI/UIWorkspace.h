#pragma once
#include "../../XrEUI/imgui_internal.h"

namespace UIWorkspace
{
inline bool& ResetRequested() { static bool reset = false; return reset; }

inline void ApplyLayout()
{
    ImGuiWindow* host = ImGui::FindWindowByName("Master DockSpace");
    if (!host) return;
    const ImGuiID dock = host->GetID("MyDockspace");
    ImGuiDockNode* node = ImGui::DockBuilderGetNode(dock);
    static bool initialized = false;
    const bool reset = ResetRequested();
    if (!reset && initialized) return;
    initialized = true;
    ResetRequested() = false;
    // Keep existing saved layouts; seed a workspace when there are no splits.
    if (!reset && node && node->IsSplitNode()) return;
    const ImVec2 size = host->Size;
    if (size.x < 640.f || size.y < 400.f) { initialized = false; return; }
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(dock, host->Pos);
    ImGui::DockBuilderSetNodeSize(dock, size);
    ImGuiID center = dock;
    const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, .18f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, .22f, nullptr, &center);
    const ImGuiID log = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, .27f, nullptr, &right);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, .22f, nullptr, &center);
    ImGui::DockBuilderDockWindow("LeftBar", left);
    ImGui::DockBuilderDockWindow("Object List", left);
    ImGui::DockBuilderDockWindow("Render", center);
    ImGui::DockBuilderDockWindow("Properties", right);
    ImGui::DockBuilderDockWindow("Log", log);
    ImGui::DockBuilderDockWindow("Content Browser", bottom);
    ImGui::DockBuilderDockWindow("Scene Manager", bottom);
    ImGui::DockBuilderFinish(dock);
}
}
