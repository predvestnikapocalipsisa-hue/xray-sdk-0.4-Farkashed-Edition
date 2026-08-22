#pragma once
#include <d3d9.h>
#include <fstream>
#include <string>
#include <windows.h>
#include <stdio.h>
#include "../XrEUI/imgui.h"

namespace Colors {

// ============================================================
//  State
// ============================================================
static bool  s_showGui       = false;
static int   s_activeTheme   = 0;   // Current theme index (-1 = custom)

// ============================================================
//  Palette (User-editable)
// ============================================================
static float s_windowBg[4]     = { 0.09f, 0.10f, 0.15f, 0.97f };
static float s_childBg[4]      = { 0.12f, 0.13f, 0.18f, 0.60f };
static float s_popupBg[4]      = { 0.10f, 0.11f, 0.15f, 0.96f };
static float s_accent[4]       = { 0.28f, 0.56f, 1.00f, 1.00f };
static float s_text[4]         = { 0.94f, 0.95f, 0.98f, 1.00f };
static float s_textDisabled[4] = { 0.48f, 0.50f, 0.56f, 1.00f };
static float s_border[4]       = { 0.22f, 0.26f, 0.35f, 0.65f };
static float s_scrollbar[4]    = { 0.07f, 0.08f, 0.11f, 1.00f };

// ============================================================
//  Shape / Spacing
// ============================================================
static float s_rounding    = 6.0f;
static float s_framePad    = 5.0f;
static float s_itemSpacing = 7.0f;
static float s_windowPad   = 14.0f;
static float s_borderSize  = 1.0f;
static float s_alpha       = 0.98f;

// ============================================================
//  Config
// ============================================================
static const char* s_cfgFile = "ui_settings.cfg";

// ============================================================
//  Built-in themes
// ============================================================
struct Theme {
    const char* name;
    const char* icon;       // Clean text badge identifier for font safety
    float bg[4];
    float accent[4];
    float text[4];
    float rounding;
};

static const Theme s_themes[] = {
    { "Dark Blue Pro",  "[Blue]",
      { 0.09f, 0.10f, 0.15f, 0.97f }, { 0.28f, 0.56f, 1.00f, 1.00f }, { 0.95f, 0.96f, 0.98f, 1.00f }, 6.0f },
    { "Midnight Slate", "[Mid]",
      { 0.06f, 0.07f, 0.10f, 0.98f }, { 0.32f, 0.62f, 0.92f, 1.00f }, { 0.90f, 0.92f, 0.96f, 1.00f }, 7.0f },
    { "Nord Dark",      "[Nord]",
      { 0.16f, 0.18f, 0.23f, 0.97f }, { 0.53f, 0.75f, 0.82f, 1.00f }, { 0.93f, 0.95f, 0.97f, 1.00f }, 6.0f },
    { "Deep Purple",    "[Purp]",
      { 0.10f, 0.07f, 0.16f, 0.97f }, { 0.62f, 0.35f, 0.95f, 1.00f }, { 0.96f, 0.93f, 1.00f, 1.00f }, 6.0f },
    { "Cyber Neon",     "[Neon]",
      { 0.08f, 0.05f, 0.13f, 0.98f }, { 0.00f, 0.85f, 0.95f, 1.00f }, { 0.96f, 0.92f, 1.00f, 1.00f }, 5.0f },
    { "Teal Dark",      "[Teal]",
      { 0.05f, 0.11f, 0.12f, 0.97f }, { 0.10f, 0.78f, 0.72f, 1.00f }, { 0.88f, 0.97f, 0.96f, 1.00f }, 6.0f },
    { "Emerald Night",  "[Emrd]",
      { 0.05f, 0.11f, 0.07f, 0.97f }, { 0.18f, 0.82f, 0.44f, 1.00f }, { 0.88f, 0.98f, 0.90f, 1.00f }, 6.0f },
    { "Warm Charcoal",  "[Warm]",
      { 0.13f, 0.11f, 0.09f, 0.97f }, { 0.92f, 0.55f, 0.18f, 1.00f }, { 0.98f, 0.94f, 0.88f, 1.00f }, 5.0f },
    { "Crimson Night",  "[Crim]",
      { 0.11f, 0.06f, 0.07f, 0.97f }, { 0.88f, 0.22f, 0.26f, 1.00f }, { 0.98f, 0.89f, 0.90f, 1.00f }, 6.0f },
    { "Rose Gold",      "[Rose]",
      { 0.12f, 0.08f, 0.10f, 0.97f }, { 0.95f, 0.48f, 0.58f, 1.00f }, { 0.99f, 0.91f, 0.93f, 1.00f }, 7.0f },
    { "Slate Gray",     "[Slate]",
      { 0.14f, 0.15f, 0.17f, 0.97f }, { 0.52f, 0.58f, 0.68f, 1.00f }, { 0.95f, 0.96f, 0.98f, 1.00f }, 5.0f },
    { "Classic Light",  "[Light]",
      { 0.93f, 0.94f, 0.96f, 1.00f }, { 0.18f, 0.46f, 0.88f, 1.00f }, { 0.08f, 0.09f, 0.13f, 1.00f }, 5.0f },
    { "Sand Storm",     "[Sand]",
      { 0.17f, 0.15f, 0.11f, 0.97f }, { 0.85f, 0.70f, 0.32f, 1.00f }, { 0.98f, 0.95f, 0.84f, 1.00f }, 5.0f },
    { "Obsidian Pure",  "[Obsd]",
      { 0.04f, 0.04f, 0.05f, 0.98f }, { 0.85f, 0.88f, 0.95f, 1.00f }, { 0.95f, 0.96f, 0.98f, 1.00f }, 4.0f },
};
static const int s_themeCount = (int)(sizeof(s_themes) / sizeof(s_themes[0]));

// ============================================================
//  Helpers
// ============================================================
static inline ImVec4 ToImVec4(const float c[4])  { return { c[0], c[1], c[2], c[3] }; }

static inline float  ClampF(float val, float minV, float maxV)
{
    return val < minV ? minV : (val > maxV ? maxV : val);
}

static inline ImVec4 Brighten(const ImVec4& c, float f)
{
    return { ClampF(c.x * f, 0.f, 1.f), ClampF(c.y * f, 0.f, 1.f), ClampF(c.z * f, 0.f, 1.f), c.w };
}

static inline ImVec4 Darken(const ImVec4& c, float f)
{
    return { ClampF(c.x * f, 0.f, 1.f), ClampF(c.y * f, 0.f, 1.f), ClampF(c.z * f, 0.f, 1.f), c.w };
}

static inline ImVec4 WithAlpha(const ImVec4& c, float a)
{
    return { c.x, c.y, c.z, ClampF(a, 0.f, 1.f) };
}

static inline ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t)
{
    float tc = ClampF(t, 0.f, 1.f);
    return { a.x + (b.x - a.x) * tc, a.y + (b.y - a.y) * tc, a.z + (b.z - a.z) * tc, a.w + (b.w - a.w) * tc };
}

// ============================================================
//  Apply all settings to ImGuiStyle
// ============================================================
inline void UpdateImGuiStyle()
{
    ImGuiStyle& st = ImGui::GetStyle();

    const float r           = s_rounding;
    st.WindowRounding       = r;
    st.ChildRounding        = ClampF(r - 2.0f, 0.0f, 12.0f);
    st.FrameRounding        = ClampF(r - 3.0f, 0.0f, 10.0f);
    st.PopupRounding        = r;
    st.ScrollbarRounding    = 10.0f;
    st.GrabRounding         = ClampF(r - 3.0f, 0.0f, 10.0f);
    st.TabRounding          = ClampF(r - 2.0f, 0.0f, 10.0f);

    st.WindowBorderSize     = s_borderSize;
    st.FrameBorderSize      = s_borderSize > 0.5f ? 1.0f : 0.0f;
    st.PopupBorderSize      = s_borderSize;
    st.ChildBorderSize      = s_borderSize;
    st.TabBorderSize        = s_borderSize > 0.5f ? 1.0f : 0.0f;

    st.FramePadding         = { s_framePad * 1.8f, s_framePad };
    st.ItemSpacing          = { s_itemSpacing * 1.6f, s_itemSpacing };
    st.ItemInnerSpacing     = { s_itemSpacing * 0.9f, s_itemSpacing * 0.9f };
    st.WindowPadding        = { s_windowPad, s_windowPad };
    st.ScrollbarSize        = 13.0f;
    st.GrabMinSize          = 11.0f;
    st.IndentSpacing        = 18.0f;
    st.WindowTitleAlign     = { 0.0f, 0.5f };
    st.Alpha                = s_alpha;

    const ImVec4 accent     = ToImVec4(s_accent);
    const ImVec4 accentHov  = Brighten(accent, 1.18f);
    const ImVec4 accentAct  = Darken(accent, 0.78f);
    const ImVec4 accentDim  = Darken(accent, 0.52f);
    const ImVec4 accentFade = WithAlpha(accent, 0.30f);
    const ImVec4 bg         = ToImVec4(s_windowBg);
    const ImVec4 childBg    = ToImVec4(s_childBg);
    const ImVec4 popupBg    = ToImVec4(s_popupBg);
    const ImVec4 txt        = ToImVec4(s_text);
    const ImVec4 txtDis     = ToImVec4(s_textDisabled);
    const ImVec4 border     = ToImVec4(s_border);
    const ImVec4 scrollbar  = ToImVec4(s_scrollbar);

    const ImVec4 frameBg    = Brighten(bg, 1.35f);
    const ImVec4 frameBgHov = Brighten(bg, 1.65f);

    const ImVec4 titleBg    = Darken(bg, 0.75f);
    const ImVec4 titleActive= Darken(Mix(bg, accent, 0.30f), 0.90f);
    const ImVec4 menuBg     = Darken(bg, 0.85f);
    const ImVec4 tabBg      = Darken(accent, 0.40f);

    // Apply colors to all ImGui slots
    st.Colors[ImGuiCol_Text]                  = txt;
    st.Colors[ImGuiCol_TextDisabled]          = txtDis;
    st.Colors[ImGuiCol_WindowBg]              = bg;
    st.Colors[ImGuiCol_ChildBg]               = childBg;
    st.Colors[ImGuiCol_PopupBg]               = popupBg;
    st.Colors[ImGuiCol_Border]                = border;
    st.Colors[ImGuiCol_BorderShadow]          = { 0.f, 0.f, 0.f, 0.f };
    st.Colors[ImGuiCol_FrameBg]               = frameBg;
    st.Colors[ImGuiCol_FrameBgHovered]        = frameBgHov;
    st.Colors[ImGuiCol_FrameBgActive]         = WithAlpha(accent, 0.38f);
    st.Colors[ImGuiCol_TitleBg]               = titleBg;
    st.Colors[ImGuiCol_TitleBgActive]         = titleActive;
    st.Colors[ImGuiCol_TitleBgCollapsed]      = Darken(titleBg, 0.75f);
    st.Colors[ImGuiCol_MenuBarBg]             = menuBg;
    st.Colors[ImGuiCol_ScrollbarBg]           = scrollbar;
    st.Colors[ImGuiCol_ScrollbarGrab]         = accentDim;
    st.Colors[ImGuiCol_ScrollbarGrabHovered]  = accent;
    st.Colors[ImGuiCol_ScrollbarGrabActive]   = accentAct;
    st.Colors[ImGuiCol_CheckMark]             = accent;
    st.Colors[ImGuiCol_SliderGrab]            = accent;
    st.Colors[ImGuiCol_SliderGrabActive]      = accentHov;
    st.Colors[ImGuiCol_Button]                = accentDim;
    st.Colors[ImGuiCol_ButtonHovered]         = accent;
    st.Colors[ImGuiCol_ButtonActive]          = accentAct;
    st.Colors[ImGuiCol_Header]                = accentFade;
    st.Colors[ImGuiCol_HeaderHovered]         = WithAlpha(accent, 0.55f);
    st.Colors[ImGuiCol_HeaderActive]          = accent;
    st.Colors[ImGuiCol_Separator]             = WithAlpha(border, 0.70f);
    st.Colors[ImGuiCol_SeparatorHovered]      = accent;
    st.Colors[ImGuiCol_SeparatorActive]       = accentAct;
    st.Colors[ImGuiCol_ResizeGrip]            = WithAlpha(accent, 0.25f);
    st.Colors[ImGuiCol_ResizeGripHovered]     = WithAlpha(accent, 0.70f);
    st.Colors[ImGuiCol_ResizeGripActive]      = accent;
    st.Colors[ImGuiCol_Tab]                   = tabBg;
    st.Colors[ImGuiCol_TabHovered]            = accentHov;
    st.Colors[ImGuiCol_TabActive]             = accent;
    st.Colors[ImGuiCol_TabUnfocused]          = Darken(tabBg, 0.75f);
    st.Colors[ImGuiCol_TabUnfocusedActive]    = tabBg;
    st.Colors[ImGuiCol_DockingPreview]        = WithAlpha(accent, 0.70f);
    st.Colors[ImGuiCol_DockingEmptyBg]        = Darken(bg, 0.60f);
    st.Colors[ImGuiCol_PlotLines]             = accent;
    st.Colors[ImGuiCol_PlotLinesHovered]      = accentHov;
    st.Colors[ImGuiCol_PlotHistogram]         = accent;
    st.Colors[ImGuiCol_PlotHistogramHovered]  = accentHov;
    st.Colors[ImGuiCol_TableHeaderBg]         = Brighten(bg, 1.25f);
    st.Colors[ImGuiCol_TableBorderStrong]     = WithAlpha(border, 0.85f);
    st.Colors[ImGuiCol_TableBorderLight]      = WithAlpha(border, 0.45f);
    st.Colors[ImGuiCol_TableRowBg]            = { 0.f, 0.f, 0.f, 0.f };
    st.Colors[ImGuiCol_TableRowBgAlt]         = WithAlpha(txt, 0.03f);
    st.Colors[ImGuiCol_TextSelectedBg]        = WithAlpha(accent, 0.38f);
    st.Colors[ImGuiCol_DragDropTarget]        = accentHov;
    st.Colors[ImGuiCol_NavHighlight]          = accent;
    st.Colors[ImGuiCol_NavWindowingHighlight] = WithAlpha(accent, 0.75f);
    st.Colors[ImGuiCol_NavWindowingDimBg]     = { 0.15f, 0.15f, 0.18f, 0.40f };
    st.Colors[ImGuiCol_ModalWindowDimBg]      = { 0.04f, 0.04f, 0.07f, 0.60f };
}

// ============================================================
//  Apply a built-in theme
// ============================================================
static inline void ApplyTheme(int idx)
{
    if (idx < 0 || idx >= s_themeCount) return;
    const Theme& t = s_themes[idx];
    s_activeTheme = idx;
    for (int i = 0; i < 4; i++) {
        s_windowBg[i] = t.bg[i];
        s_accent[i]   = t.accent[i];
        s_text[i]     = t.text[i];
    }
    for (int i = 0; i < 3; i++) {
        s_childBg[i]   = ClampF(t.bg[i] + 0.04f, 0.0f, 1.0f);
        s_popupBg[i]   = ClampF(t.bg[i] + 0.02f, 0.0f, 1.0f);
        s_border[i]    = ClampF(t.bg[i] + 0.18f, 0.0f, 1.0f);
        s_scrollbar[i] = t.bg[i] * 0.65f;
    }
    s_childBg[3]   = 0.60f;
    s_popupBg[3]   = 0.96f;
    s_border[3]    = 0.65f;
    s_scrollbar[3] = 1.00f;

    s_textDisabled[0] = ClampF(t.text[0] * 0.52f, 0.0f, 1.0f);
    s_textDisabled[1] = ClampF(t.text[1] * 0.52f, 0.0f, 1.0f);
    s_textDisabled[2] = ClampF(t.text[2] * 0.54f, 0.0f, 1.0f);
    s_textDisabled[3] = 1.00f;

    s_rounding     = t.rounding;
    UpdateImGuiStyle();
}

// ============================================================
//  Save / Load
// ============================================================
inline void SaveSettings()
{
    std::ofstream f(s_cfgFile);
    if (!f.is_open()) return;
    f << "v3\n";
    auto w4 = [&](const float c[4]) { f << c[0] << " " << c[1] << " " << c[2] << " " << c[3] << "\n"; };
    w4(s_windowBg); w4(s_childBg); w4(s_popupBg);
    w4(s_accent); w4(s_text); w4(s_textDisabled); w4(s_border); w4(s_scrollbar);
    f << s_rounding    << "\n" << s_framePad   << "\n" << s_itemSpacing << "\n"
      << s_windowPad   << "\n" << s_borderSize << "\n" << s_alpha      << "\n";
    f << s_activeTheme << "\n";
    f.close();
}

inline void LoadSettings()
{
    std::ifstream f(s_cfgFile);
    if (!f.is_open()) return;
    std::string tag; f >> tag;
    if (tag != "v3") { f.close(); return; }
    auto r4 = [&](float c[4]) { f >> c[0] >> c[1] >> c[2] >> c[3]; };
    r4(s_windowBg); r4(s_childBg); r4(s_popupBg);
    r4(s_accent); r4(s_text); r4(s_textDisabled); r4(s_border); r4(s_scrollbar);
    f >> s_rounding >> s_framePad >> s_itemSpacing >> s_windowPad >> s_borderSize >> s_alpha;
    f >> s_activeTheme;
    f.close();
    UpdateImGuiStyle();
}

inline void ResetDefaults() { ApplyTheme(0); }

// ============================================================
//  Public API
// ============================================================
inline void Enable()  { s_showGui = true;  }
inline void Disable() { s_showGui = false; }
inline bool IsOpen()  { return s_showGui;  }

// ============================================================
//  Helper: Thin separator with spacing
// ============================================================
static inline void ThinSeparator()
{
    ImGui::Spacing();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 0 });
    ImGui::Separator();
    ImGui::PopStyleVar();
    ImGui::Spacing();
}

// ============================================================
//  Helper: Section label
// ============================================================
static inline void SectionLabel(const char* txt)
{
    ImVec4 tc = ToImVec4(s_accent);
    ImGui::PushStyleColor(ImGuiCol_Text, Brighten(tc, 1.10f));
    ImGui::TextUnformatted(txt);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

// ============================================================
//  Render
// ============================================================
inline void Render()
{
    if (GetAsyncKeyState(VK_INSERT) & 1)
        s_showGui = !s_showGui;

    if (!s_showGui) return;

    // ---- Window sizing ----
    ImGui::SetNextWindowSize({ 480, 0 }, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({ 400, 280 }, { 680, 960 });

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   { s_windowPad, s_windowPad });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  s_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    { 8.0f, 5.0f });

    const bool open = ImGui::Begin("XrEUI Style & Color Manager", &s_showGui,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::PopStyleVar(3);

    if (!open) { ImGui::End(); return; }

    // ---- Header panel ----
    {
        ImVec4 accent4 = ToImVec4(s_accent);
        ImGui::PushStyleColor(ImGuiCol_Button,        Darken(accent4, 0.52f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent4);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Darken(accent4, 0.75f));
        if (ImGui::Button("  Hide [Insert]  ")) { Disable(); ImGui::PopStyleColor(3); ImGui::End(); return; }
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, 12);
        ImGui::PushStyleColor(ImGuiCol_Text, WithAlpha(ToImVec4(s_textDisabled), 0.85f));
        ImGui::PopStyleColor();

        // Right-aligned active theme badge
        if (s_activeTheme >= 0 && s_activeTheme < s_themeCount)
        {
            const char* tname = s_themes[s_activeTheme].name;
            float tw = ImGui::CalcTextSize(tname).x + 12;
            ImGui::SameLine(ImGui::GetContentRegionMax().x - tw);
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(s_accent));
            ImGui::TextUnformatted(tname);
            ImGui::PopStyleColor();
        }
        else
        {
            const char* tname = "Custom Theme";
            float tw = ImGui::CalcTextSize(tname).x + 12;
            ImGui::SameLine(ImGui::GetContentRegionMax().x - tw);
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(s_textDisabled));
            ImGui::TextUnformatted(tname);
            ImGui::PopStyleColor();
        }
    }

    ThinSeparator();

    // ================================================================
    //  TAB BAR
    // ================================================================
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, s_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 12.f, 5.f });
    if (ImGui::BeginTabBar("##main_tabs", ImGuiTabBarFlags_None))
    {
        ImGui::PopStyleVar(2);

        // =========================================================
        //  TAB: Themes
        // =========================================================
        if (ImGui::BeginTabItem("  Presets  "))
        {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(s_textDisabled));
            ImGui::TextUnformatted("Select a theme preset - changes apply instantly:");
            ImGui::PopStyleColor();
            ImGui::Spacing();

            const int cols = 3;
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const float bw  = (ImGui::GetContentRegionAvail().x - gap * (cols - 1)) / cols;

            for (int i = 0; i < s_themeCount; i++)
            {
                if (i % cols != 0) ImGui::SameLine(0, gap);

                const Theme& th = s_themes[i];
                const bool   active = (i == s_activeTheme);

                ImVec4 bc = { th.accent[0]*0.45f, th.accent[1]*0.45f, th.accent[2]*0.45f, 1.0f };
                ImVec4 bh = { th.accent[0]*0.75f, th.accent[1]*0.75f, th.accent[2]*0.75f, 1.0f };
                ImVec4 ba = { th.accent[0],        th.accent[1],        th.accent[2],        1.0f };

                if (active)
                {
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                    ImGui::PushStyleColor(ImGuiCol_Border, ba);
                }

                ImGui::PushStyleColor(ImGuiCol_Button,        bc);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bh);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ba);
                ImGui::PushStyleColor(ImGuiCol_Text,          { th.text[0], th.text[1], th.text[2], 1.0f });

                char bid[64];
                _snprintf(bid, sizeof(bid), "%s %s##th%d", th.icon, th.name, i);
                if (ImGui::Button(bid, { bw, 32.f }))
                    ApplyTheme(i);

                ImGui::PopStyleColor(4);
                if (active)
                {
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                }
            }

            ImGui::Spacing();
            ThinSeparator();

            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(s_textDisabled));
            ImGui::TextUnformatted("Accent tone preview:");
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 10);

            ImVec4 a = ToImVec4(s_accent);
            ImVec4 swatchColors[5] = {
                a,
                Brighten(a, 1.18f),
                Darken(a, 0.78f),
                Darken(a, 0.52f),
                WithAlpha(a, 0.30f)
            };
            const char* swatchTips[5] = { "Base", "Hovered", "Active", "Dim", "Fade" };
            for (int s = 0; s < 5; s++)
            {
                char sid[16]; _snprintf(sid, sizeof(sid), "##sw%d", s);
                if (s) ImGui::SameLine(0, 5);
                ImGui::ColorButton(sid, swatchColors[s],
                    ImGuiColorEditFlags_NoTooltip | (s == 4 ? ImGuiColorEditFlags_AlphaPreview : 0),
                    { 42, 18 });
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", swatchTips[s]);
            }

            ImGui::Spacing();
            ImGui::EndTabItem();
        }

        // =========================================================
        //  TAB: Colors
        // =========================================================
        if (ImGui::BeginTabItem("  Palette  "))
        {
            ImGui::Spacing();
            bool changed = false;

            auto Row = [&](const char* label, const char* tip, float c[4]) {
                if (ImGui::ColorEdit4(label, c,
                    ImGuiColorEditFlags_AlphaBar |
                    ImGuiColorEditFlags_AlphaPreviewHalf |
                    ImGuiColorEditFlags_PickerHueWheel))
                {
                    changed = true;
                    s_activeTheme = -1; 
                }
                if (tip && ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", tip);
            };

            // --- Window ---
            if (ImGui::CollapsingHeader("  Window & Backgrounds", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PushItemWidth(-1);
                ImGui::Spacing();
                Row("Background##win",       "Primary window background",       s_windowBg);
                Row("Child panels##child",   "Background for child panels",      s_childBg);
                Row("Popup background##pop", "Background for popups & tooltips", s_popupBg);
                Row("Border color##border",  "Color for windows & frame borders",s_border);
                Row("Scrollbar track##scr",  "Scrollbar track background",      s_scrollbar);
                ImGui::PopItemWidth();
                ImGui::Spacing();
            }

            // --- Text ---
            if (ImGui::CollapsingHeader("  Text & Labels", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PushItemWidth(-1);
                ImGui::Spacing();
                Row("Primary text##txt",      "Base text color",        s_text);
                Row("Disabled text##dis",     "Hints and disabled labels color", s_textDisabled);
                ImGui::PopItemWidth();
                ImGui::Spacing();
            }

            // --- Accent ---
            if (ImGui::CollapsingHeader("  Accent Colors", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PushItemWidth(-1);
                ImGui::Spacing();
                Row("Accent color##acc", "Buttons, selection, headers, sliders", s_accent);
                ImGui::PopItemWidth();
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(s_textDisabled));
                ImGui::TextUnformatted("Derived state tones:");
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 10);
                ImVec4 av = ToImVec4(s_accent);
                struct { ImVec4 c; const char* lbl; } tones[] = {
                    { av,                   "Base"    },
                    { Brighten(av, 1.18f),  "Hovered" },
                    { Darken(av, 0.78f),    "Active"  },
                    { Darken(av, 0.52f),    "Dim"     },
                    { WithAlpha(av,0.30f),  "Fade"    },
                };
                for (int i = 0; i < 5; i++)
                {
                    if (i) ImGui::SameLine(0, 5);
                    char id[16]; _snprintf(id, sizeof(id), "##t%d", i);
                    ImGui::ColorButton(id, tones[i].c,
                        ImGuiColorEditFlags_NoTooltip | (i==4 ? ImGuiColorEditFlags_AlphaPreview : 0),
                        { 40, 18 });
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tones[i].lbl);
                }
                ImGui::Spacing();
            }

            if (changed) UpdateImGuiStyle();
            ImGui::EndTabItem();
        }

        // =========================================================
        //  TAB: Geometry
        // =========================================================
        if (ImGui::BeginTabItem("  Geometry  "))
        {
            ImGui::Spacing();
            bool changed = false;

            SectionLabel("  Rounding & Spacing");

            ImGui::PushItemWidth(-150);
            if (ImGui::SliderFloat("Rounding##r",      &s_rounding,    0.0f, 14.0f, "%.1f px")) { changed = true; s_activeTheme = -1; }
            if (ImGui::SliderFloat("Frame Pad##fp",    &s_framePad,    1.0f, 10.0f, "%.1f px")) { changed = true; }
            if (ImGui::SliderFloat("Item Spacing##is", &s_itemSpacing, 1.0f, 10.0f, "%.1f px")) { changed = true; }
            if (ImGui::SliderFloat("Window Pad##wp",   &s_windowPad,   4.0f, 24.0f, "%.1f px")) { changed = true; }
            if (ImGui::SliderFloat("Border Size##bs",  &s_borderSize,  0.0f,  2.0f, "%.1f px")) { changed = true; }
            ImGui::Spacing();
            if (ImGui::SliderFloat("Opacity##op",      &s_alpha,       0.2f,  1.0f, "%.2f"))    { changed = true; }
            ImGui::PopItemWidth();

            ThinSeparator();

            SectionLabel("  Shape Presets");
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 10.f, 4.f });
            struct ShapePreset { const char* label; float round; float border; };
            static const ShapePreset shapes[] = {
                { "Flat",      0.0f,  1.0f },
                { "Soft",      5.0f,  1.0f },
                { "Rounded",   8.0f,  1.0f },
                { "Capsule",  12.0f,  0.0f },
                { "Modern",    6.0f,  0.0f },
            };
            for (int i = 0; i < 5; i++)
            {
                if (i) ImGui::SameLine();
                if (ImGui::SmallButton(shapes[i].label))
                {
                    s_rounding   = shapes[i].round;
                    s_borderSize = shapes[i].border;
                    UpdateImGuiStyle();
                }
            }
            ImGui::PopStyleVar();

            if (changed) UpdateImGuiStyle();
            ImGui::Spacing();
            ImGui::EndTabItem();
        }

        // =========================================================
        //  TAB: Config
        // =========================================================
        if (ImGui::BeginTabItem("  Config  "))
        {
            ImGui::Spacing();
            SectionLabel("  Persistence");

            const float hw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

            ImVec4 acc = ToImVec4(s_accent);
            ImGui::PushStyleColor(ImGuiCol_Button,        Darken(acc, 0.52f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, acc);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Darken(acc, 0.75f));
            if (ImGui::Button("Save Settings", { hw, 34.f })) SaveSettings();
            ImGui::PopStyleColor(3);

            ImGui::SameLine();

            ImVec4 acc2 = Darken(acc, 0.75f);
            ImGui::PushStyleColor(ImGuiCol_Button,        Darken(acc2, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, acc2);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Darken(acc2, 0.65f));
            if (ImGui::Button("Load Settings", { hw, 34.f })) LoadSettings();
            ImGui::PopStyleColor(3);

            ThinSeparator();
            SectionLabel("  Industrial Reset");

            ImGui::PushStyleColor(ImGuiCol_Button,        { 0.55f, 0.10f, 0.10f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.78f, 0.18f, 0.18f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  { 0.38f, 0.06f, 0.06f, 1.0f });
            if (ImGui::Button("Reset to Defaults", { -1.f, 30.f })) ResetDefaults();
            ImGui::PopStyleColor(3);

            ThinSeparator();

            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(s_textDisabled));
            ImGui::Text("Config file: %s", s_cfgFile);
            ImGui::Spacing();
            ImGui::TextWrapped("Tip: Style adjustments apply immediately. Click 'Save Settings' to preserve theme changes across editor sessions.");
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    else
    {
        ImGui::PopStyleVar(2);
    }

    ImGui::End();
}

} // namespace Colors