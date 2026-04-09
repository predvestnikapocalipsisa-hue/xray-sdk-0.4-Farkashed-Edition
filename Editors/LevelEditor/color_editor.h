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
static float s_windowBg[4]     = { 0.09f, 0.09f, 0.11f, 0.97f };
static float s_childBg[4]      = { 0.12f, 0.12f, 0.15f, 0.60f };
static float s_popupBg[4]      = { 0.10f, 0.10f, 0.13f, 0.96f };
static float s_accent[4]       = { 0.25f, 0.52f, 0.95f, 1.00f };
static float s_text[4]         = { 0.93f, 0.93f, 0.96f, 1.00f };
static float s_textDisabled[4] = { 0.45f, 0.45f, 0.50f, 1.00f };
static float s_border[4]       = { 0.28f, 0.28f, 0.35f, 0.60f };
static float s_scrollbar[4]    = { 0.08f, 0.08f, 0.10f, 1.00f };

// ============================================================
//  Shape / Spacing
// ============================================================
static float s_rounding    = 8.0f;
static float s_framePad    = 6.0f;
static float s_itemSpacing = 8.0f;
static float s_windowPad   = 16.0f;
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
    const char* icon;       // Emoji/Symbol for the button
    float bg[4];
    float accent[4];
    float text[4];
    float rounding;
};

static const Theme s_themes[] = {
    { "Dark Blue",      "\xF0\x9F\x94\xB5", // ??
      { 0.09f, 0.10f, 0.15f, 0.97f }, { 0.28f, 0.56f, 1.00f, 1.00f }, { 0.95f, 0.96f, 0.98f, 1.00f }, 8.0f },
    { "Midnight",       "\xF0\x9F\x8C\x99", // ??
      { 0.05f, 0.05f, 0.09f, 0.98f }, { 0.40f, 0.65f, 1.00f, 1.00f }, { 0.88f, 0.90f, 1.00f, 1.00f }, 10.0f },
    { "Deep Purple",    "\xF0\x9F\x92\x9C", // ??
      { 0.10f, 0.07f, 0.16f, 0.97f }, { 0.62f, 0.30f, 0.95f, 1.00f }, { 0.96f, 0.92f, 1.00f, 1.00f }, 8.0f },
    { "Neon Violet",    "\xE2\x9A\xA1",      // ?
      { 0.08f, 0.05f, 0.14f, 0.97f }, { 0.78f, 0.20f, 1.00f, 1.00f }, { 0.96f, 0.90f, 1.00f, 1.00f }, 6.0f },
    { "Teal Dark",      "\xF0\x9F\x8C\x8A", // ??
      { 0.05f, 0.11f, 0.12f, 0.97f }, { 0.08f, 0.80f, 0.72f, 1.00f }, { 0.86f, 0.97f, 0.96f, 1.00f }, 6.0f },
    { "Emerald",        "\xF0\x9F\x92\x9A", // ??
      { 0.05f, 0.11f, 0.07f, 0.97f }, { 0.15f, 0.82f, 0.42f, 1.00f }, { 0.85f, 0.98f, 0.88f, 1.00f }, 6.0f },
    { "Warm Charcoal",  "\xF0\x9F\x94\xA5", // ??
      { 0.13f, 0.10f, 0.08f, 0.97f }, { 0.92f, 0.55f, 0.18f, 1.00f }, { 0.98f, 0.94f, 0.88f, 1.00f }, 5.0f },
    { "Crimson Night",  "\xF0\x9F\x94\xB4", // ??
      { 0.10f, 0.06f, 0.06f, 0.97f }, { 0.86f, 0.18f, 0.22f, 1.00f }, { 0.98f, 0.88f, 0.88f, 1.00f }, 6.0f },
    { "Rose Gold",      "\xF0\x9F\x8C\xB9", // ??
      { 0.12f, 0.08f, 0.09f, 0.97f }, { 0.95f, 0.45f, 0.55f, 1.00f }, { 0.99f, 0.90f, 0.92f, 1.00f }, 8.0f },
    { "Slate Gray",     "\xF0\x9F\x94\xB7", // ??
      { 0.14f, 0.14f, 0.16f, 0.97f }, { 0.55f, 0.58f, 0.68f, 1.00f }, { 0.95f, 0.95f, 0.96f, 1.00f }, 4.0f },
    { "Classic Light",  "\xE2\x98\x80",     // ?
      { 0.93f, 0.93f, 0.95f, 1.00f }, { 0.18f, 0.44f, 0.86f, 1.00f }, { 0.08f, 0.08f, 0.12f, 1.00f }, 5.0f },
    { "Sand Storm",     "\xF0\x9F\x8C\x90", // ??
      { 0.18f, 0.15f, 0.10f, 0.97f }, { 0.85f, 0.72f, 0.35f, 1.00f }, { 0.98f, 0.95f, 0.83f, 1.00f }, 4.0f },
};
static const int s_themeCount = (int)(sizeof(s_themes) / sizeof(s_themes[0]));

// ============================================================
//  Helpers
// ============================================================
static inline ImVec4 ToImVec4(const float c[4])  { return { c[0], c[1], c[2], c[3] }; }
static inline ImVec4 Brighten(const ImVec4& c, float f)
{
    auto _m = [](float a, float b) { return a < b ? a : b; };
    return { _m(c.x * f, 1.f), _m(c.y * f, 1.f), _m(c.z * f, 1.f), c.w };
}
static inline ImVec4 Darken  (const ImVec4& c, float f) { return { c.x*f, c.y*f, c.z*f, c.w }; }
static inline ImVec4 WithAlpha(const ImVec4& c, float a){ return { c.x, c.y, c.z, a }; }
static inline ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t)
{
    return { a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t, a.w+(b.w-a.w)*t };
}

// ============================================================
//  Apply all settings to ImGuiStyle
// ============================================================
inline void UpdateImGuiStyle()
{
    ImGuiStyle& st = ImGui::GetStyle();

    const float r           = s_rounding;
    st.WindowRounding       = r;
    st.ChildRounding        = r - 2.0f > 0.0f ? r - 2.0f : 0.0f;
    st.FrameRounding        = r - 4.0f > 0.0f ? r - 4.0f : 2.0f;
    st.PopupRounding        = r;
    st.ScrollbarRounding    = 12.0f;
    st.GrabRounding         = r - 4.0f > 0.0f ? r - 4.0f : 2.0f;
    st.TabRounding          = r - 2.0f > 0.0f ? r - 2.0f : 2.0f;
    st.WindowBorderSize     = s_borderSize;
    st.FrameBorderSize      = s_borderSize > 0.5f ? 1.0f : 0.0f;
    st.PopupBorderSize      = s_borderSize;
    st.FramePadding         = { s_framePad * 2.0f, s_framePad };
    st.ItemSpacing          = { s_itemSpacing * 2.0f, s_itemSpacing };
    st.ItemInnerSpacing     = { s_itemSpacing, s_itemSpacing };
    st.WindowPadding        = { s_windowPad, s_windowPad };
    st.ScrollbarSize        = 14.0f;
    st.GrabMinSize          = 10.0f;
    st.IndentSpacing        = 20.0f;
    st.Alpha                = s_alpha;

    const ImVec4 accent     = ToImVec4(s_accent);
    const ImVec4 accentHov  = Brighten(accent, 1.18f);
    const ImVec4 accentAct  = Darken  (accent, 0.78f);
    const ImVec4 accentDim  = Darken  (accent, 0.55f);
    const ImVec4 accentFade = WithAlpha(accent, 0.32f);
    const ImVec4 bg         = ToImVec4(s_windowBg);
    const ImVec4 childBg    = ToImVec4(s_childBg);
    const ImVec4 popupBg    = ToImVec4(s_popupBg);
    const ImVec4 txt        = ToImVec4(s_text);
    const ImVec4 txtDis     = ToImVec4(s_textDisabled);
    const ImVec4 border     = ToImVec4(s_border);
    const ImVec4 scrollbar  = ToImVec4(s_scrollbar);

    const ImVec4 frameBg    = { bg.x+0.06f, bg.y+0.06f, bg.z+0.09f, 0.85f };
    const ImVec4 frameBgHov = { bg.x+0.11f, bg.y+0.11f, bg.z+0.14f, 0.90f };

    const ImVec4 titleBg    = Darken(bg, 0.75f);
    const ImVec4 titleActive= Darken(Mix(bg, accent, 0.35f), 0.90f);

    const ImVec4 menuBg     = { bg.x+0.02f, bg.y+0.02f, bg.z+0.04f, 1.00f };

    const ImVec4 tabBg      = Darken(accent, 0.38f);

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
    st.Colors[ImGuiCol_TitleBgCollapsed]      = Darken(titleBg, 0.78f);
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
    st.Colors[ImGuiCol_Separator]             = WithAlpha(border, 0.65f);
    st.Colors[ImGuiCol_SeparatorHovered]      = accent;
    st.Colors[ImGuiCol_SeparatorActive]       = accentAct;
    st.Colors[ImGuiCol_ResizeGrip]            = WithAlpha(accent, 0.22f);
    st.Colors[ImGuiCol_ResizeGripHovered]     = WithAlpha(accent, 0.65f);
    st.Colors[ImGuiCol_ResizeGripActive]      = accent;
    st.Colors[ImGuiCol_Tab]                   = tabBg;
    st.Colors[ImGuiCol_TabHovered]            = accentHov;
    st.Colors[ImGuiCol_TabActive]             = accent;
    st.Colors[ImGuiCol_TabUnfocused]          = Darken(tabBg, 0.78f);
    st.Colors[ImGuiCol_TabUnfocusedActive]    = tabBg;
    st.Colors[ImGuiCol_PlotLines]             = accent;
    st.Colors[ImGuiCol_PlotLinesHovered]      = accentHov;
    st.Colors[ImGuiCol_PlotHistogram]         = accent;
    st.Colors[ImGuiCol_PlotHistogramHovered]  = accentHov;
    st.Colors[ImGuiCol_TextSelectedBg]        = WithAlpha(accent, 0.38f);
    st.Colors[ImGuiCol_DragDropTarget]        = accentHov;
    st.Colors[ImGuiCol_NavHighlight]          = accent;
    st.Colors[ImGuiCol_NavWindowingHighlight] = WithAlpha(accent, 0.72f);
    st.Colors[ImGuiCol_NavWindowingDimBg]     = { 0.20f, 0.20f, 0.22f, 0.35f };
    st.Colors[ImGuiCol_ModalWindowDimBg]      = { 0.04f, 0.04f, 0.07f, 0.58f };
}

// ============================================================
//  Apply a built-in theme
// ============================================================
static inline void ApplyTheme(int idx)
{
    if (idx < 0 || idx >= s_themeCount) return;
    const Theme& t = s_themes[idx];
    s_activeTheme = idx;
    for (int i = 0; i < 4; i++) { s_windowBg[i] = t.bg[i]; s_accent[i] = t.accent[i]; s_text[i] = t.text[i]; }
    for (int i = 0; i < 3; i++) {
        auto _m = [](float a, float b) { return a < b ? a : b; };
        s_childBg[i]   = _m(t.bg[i] + 0.03f, 1.0f);
        s_popupBg[i]   = _m(t.bg[i] + 0.01f, 1.0f);
        s_border[i]    = _m(t.bg[i] + 0.20f, 1.0f);
        s_scrollbar[i] = t.bg[i] * 0.65f;
    }
    s_childBg[3] = 0.60f; s_popupBg[3] = 0.96f; s_border[3] = 0.60f; s_scrollbar[3] = 1.00f;
    s_rounding   = t.rounding;
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
    ImGui::PushStyleColor(ImGuiCol_Text, Brighten(tc, 1.05f));
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
    ImGui::SetNextWindowSize({ 460, 0 }, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({ 380, 250 }, { 640, 960 });

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   { s_windowPad, s_windowPad });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  s_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    { 8.0f, 5.0f });

    const bool open = ImGui::Begin("  \xF0\x9F\x8E\xA8  XrEUI Style Editor", &s_showGui,
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
        ImGui::PushStyleColor(ImGuiCol_Text, WithAlpha(ToImVec4(s_textDisabled), 0.75f));
        ImGui::TextUnformatted("XrEUI Style Editor  v3");
        ImGui::PopStyleColor();

        // Right-aligned active theme
        if (s_activeTheme >= 0 && s_activeTheme < s_themeCount)
        {
            const char* tname = s_themes[s_activeTheme].name;
            float tw = ImGui::CalcTextSize(tname).x + 8;
            ImGui::SameLine(ImGui::GetContentRegionMax().x - tw);
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(s_accent));
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
        if (ImGui::BeginTabItem("  Themes  "))
        {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(s_textDisabled));
            ImGui::TextUnformatted("Select a preset - changes apply instantly");
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
                ImVec4 bh = { th.accent[0]*0.72f, th.accent[1]*0.72f, th.accent[2]*0.72f, 1.0f };
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
                if (ImGui::Button(bid, { bw, 30.f }))
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
            ImGui::TextUnformatted("Accent preview:");
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 8);

            ImVec4 a = ToImVec4(s_accent);
            ImVec4 swatchColors[5] = {
                a,
                Brighten(a, 1.18f),
                Darken(a, 0.80f),
                Darken(a, 0.55f),
                WithAlpha(a, 0.40f)
            };
            const char* swatchTips[5] = { "Base", "Hovered", "Active", "Dim", "Fade" };
            for (int s = 0; s < 5; s++)
            {
                char sid[16]; _snprintf(sid, sizeof(sid), "##sw%d", s);
                if (s) ImGui::SameLine(0, 4);
                if (ImGui::ColorButton(sid, swatchColors[s],
                    ImGuiColorEditFlags_NoTooltip | (s == 4 ? ImGuiColorEditFlags_AlphaPreview : 0),
                    { 40, 16 }))
                {}
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", swatchTips[s]);
            }

            ImGui::Spacing();
            ImGui::EndTabItem();
        }

        // =========================================================
        //  TAB: Colors
        // =========================================================
        if (ImGui::BeginTabItem("  Colors  "))
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
            if (ImGui::CollapsingHeader("  Window", ImGuiTreeNodeFlags_DefaultOpen))
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
            if (ImGui::CollapsingHeader("  Text", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PushItemWidth(-1);
                ImGui::Spacing();
                Row("Primary text##txt",      "Base text color",        s_text);
                Row("Disabled text##dis",     "Hints and labels color",   s_textDisabled);
                ImGui::PopItemWidth();
                ImGui::Spacing();
            }

            // --- Accent ---
            if (ImGui::CollapsingHeader("  Accent", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PushItemWidth(-1);
                ImGui::Spacing();
                Row("Accent color##acc", "Buttons, selection, sliders", s_accent);
                ImGui::PopItemWidth();
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(s_textDisabled));
                ImGui::TextUnformatted("Derived tones:");
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 8);
                ImVec4 av = ToImVec4(s_accent);
                struct { ImVec4 c; const char* lbl; } tones[] = {
                    { av,                   "Base"    },
                    { Brighten(av, 1.18f),  "Hovered" },
                    { Darken(av, 0.78f),    "Active"  },
                    { Darken(av, 0.55f),    "Dim"     },
                    { WithAlpha(av,0.32f),  "Fade"    },
                };
                for (int i = 0; i < 5; i++)
                {
                    if (i) ImGui::SameLine(0, 4);
                    char id[16]; _snprintf(id, sizeof(id), "##t%d", i);
                    ImGui::ColorButton(id, tones[i].c,
                        ImGuiColorEditFlags_NoTooltip | (i==4 ? ImGuiColorEditFlags_AlphaPreview : 0),
                        { 38, 16 });
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
            if (ImGui::SliderFloat("Frame Pad##fp",    &s_framePad,    1.0f, 10.0f, "%.1f"))    { changed = true; }
            if (ImGui::SliderFloat("Item Spacing##is", &s_itemSpacing, 1.0f, 10.0f, "%.1f"))    { changed = true; }
            if (ImGui::SliderFloat("Window Pad##wp",   &s_windowPad,   4.0f, 24.0f, "%.1f"))    { changed = true; }
            if (ImGui::SliderFloat("Border Size##bs",  &s_borderSize,  0.0f,  2.0f, "%.1f"))    { changed = true; }
            ImGui::Spacing();
            if (ImGui::SliderFloat("Opacity##op",      &s_alpha,       0.2f,  1.0f, "%.2f"))    { changed = true; }
            ImGui::PopItemWidth();

            ThinSeparator();

            SectionLabel("  Quick Presets");
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 10.f, 4.f });
            struct ShapePreset { const char* label; float round; float border; };
            static const ShapePreset shapes[] = {
                { "Flat",      0.0f,  1.0f },
                { "Soft",      5.0f,  1.0f },
                { "Rounded",   8.0f,  1.0f },
                { "Capsule",  12.0f,  0.0f },
                { "Modern",    7.0f,  0.0f },
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
            if (ImGui::Button("  \xF0\x9F\x92\xBE  Save Settings", { hw, 32.f })) SaveSettings();
            ImGui::PopStyleColor(3);

            ImGui::SameLine();

            ImVec4 acc2 = Darken(acc, 0.75f);
            ImGui::PushStyleColor(ImGuiCol_Button,        Darken(acc2, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, acc2);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Darken(acc2, 0.65f));
            if (ImGui::Button("  \xF0\x9F\x93\x82  Load Settings", { hw, 32.f })) LoadSettings();
            ImGui::PopStyleColor(3);

            ThinSeparator();
            SectionLabel("  Industrial Reset");

            ImGui::PushStyleColor(ImGuiCol_Button,        { 0.55f, 0.10f, 0.10f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.78f, 0.18f, 0.18f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  { 0.38f, 0.06f, 0.06f, 1.0f });
            if (ImGui::Button("  Reset to Defaults  ", { -1.f, 28.f })) ResetDefaults();
            ImGui::PopStyleColor(3);

            ThinSeparator();

            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(s_textDisabled));
            ImGui::Text("Config file: %s", s_cfgFile);
            ImGui::Spacing();
            ImGui::TextWrapped("Tip: Changes apply instantly. Click 'Save' to persist across sessions.");
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