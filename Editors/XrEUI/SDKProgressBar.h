#pragma once
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace SDKUI
{
    // Shared by the launcher's updater and synchronous editor operations.
    inline void ProgressBar(float fraction, bool measured, float scale)
    {
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x, height = 6.f * scale;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(min, ImVec2(min.x + width, min.y + height),
            IM_COL32(35, 35, 39, 255), height * .5f);
        if (measured)
        {
            fraction = (std::max)(0.f, (std::min)(1.f, fraction));
            if (fraction > 0.f) draw->AddRectFilled(min,
                ImVec2(min.x + width * fraction, min.y + height),
                ImGui::GetColorU32(ImGuiCol_CheckMark), height * .5f);
        }
        else
        {
            const float travel = float(std::fmod(ImGui::GetTime() * .65, 1.0));
            const float left = (std::max)(0.f, travel * 1.3f - .3f);
            const float right = (std::min)(1.f, travel * 1.3f);
            draw->AddRectFilled(ImVec2(min.x + width * left, min.y),
                ImVec2(min.x + width * right, min.y + height),
                ImGui::GetColorU32(ImGuiCol_CheckMark), height * .5f);
        }
        ImGui::Dummy(ImVec2(width, height));
    }
}
