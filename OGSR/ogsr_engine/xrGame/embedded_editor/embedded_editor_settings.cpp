#include "stdafx.h"

#include "imgui.h"
#include "embedded_editor_settings.h"
#include <array>
#include "../../Layers/xrRenderDX10/imgui/imgui_styles.hpp"

void CImGuiSettingsWnd::Render()
{
    if (!RenderBegin())
    {
        RenderEnd();
        return;
    }

    ImGui::Separator();

    auto ShowStyleSelector = [](const char* label) {
        string_path fname;
        FS.update_path(fname, fsgame::app_data_root, "imgui.ltx");
        CInifile imgui_custom_ltx{fname, FALSE};

        u32 style_idx = READ_IF_EXISTS(reinterpret_cast<CInifile*>(&imgui_custom_ltx), r_u32, "im_style", "theme_selected", 0);

        bool ret = false;
        if (ImGui::BeginCombo(label, (style_idx >= 0 && style_idx < std::size(ogsr_imgui_style_names)) ? ogsr_imgui_style_names[style_idx] : ""))
        {
            for (u32 n = 0; n < std::size(ogsr_imgui_style_names); n++)
            {
                if (ImGui::Selectable(ogsr_imgui_style_names[n], style_idx == n, ImGuiSelectableFlags_SelectOnNav))
                {
                    style_idx = n;
                    ret = true;

                    SetupStyle(style_idx);
                    imgui_custom_ltx.w_u32("im_style", "theme_selected", style_idx);
                }
                else if (style_idx == n)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return ret;
    };

    ShowStyleSelector("Theme##Selector");

    ImGui::Separator();

    RenderEnd();
}
