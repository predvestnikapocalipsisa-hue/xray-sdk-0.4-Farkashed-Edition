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
static float s_windowBg[4]     = { 0.10f, 0.10f, 0.12f, 0.98f };
static float s_childBg[4]      = { 0.13f, 0.13f, 0.16f, 0.70f };
static float s_popupBg[4]      = { 0.12f, 0.12f, 0.15f, 0.98f };
static float s_accent[4]       = { 0.52f, 0.38f, 0.92f, 1.00f };
static float s_text[4]         = { 0.94f, 0.95f, 0.98f, 1.00f };
static float s_textDisabled[4] = { 0.48f, 0.50f, 0.56f, 1.00f };
static float s_border[4]       = { 0.22f, 0.24f, 0.30f, 0.65f };
static float s_scrollbar[4]    = { 0.08f, 0.08f, 0.10f, 1.00f };

// ============================================================
//  Shape / Spacing
// ============================================================
static float s_rounding    = 5.0f;
static float s_framePad    = 5.0f;
static float s_itemSpacing = 6.0f;
static float s_windowPad   = 12.0f;
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
    { "Obsidian Studio", "[Obsd]",
      { 0.10f, 0.10f, 0.12f, 0.98f }, { 0.52f, 0.38f, 0.92f, 1.00f }, { 0.94f, 0.95f, 0.98f, 1.00f }, 5.0f },
    { "Unreal Amber",    "[UE5]",
      { 0.11f, 0.11f, 0.13f, 0.98f }, { 0.95f, 0.48f, 0.12f, 1.00f }, { 0.92f, 0.94f, 0.96f, 1.00f }, 4.0f },
    { "Dark Blue Pro",   "[Blue]",
      { 0.09f, 0.10f, 0.14f, 0.98f }, { 0.24f, 0.54f, 0.96f, 1.00f }, { 0.95f, 0.96f, 0.98f, 1.00f }, 5.0f },
    { "Midnight Slate",  "[Mid]",
      { 0.07f, 0.08f, 0.10f, 0.98f }, { 0.06f, 0.65f, 0.90f, 1.00f }, { 0.90f, 0.92f, 0.96f, 1.00f }, 6.0f },
    { "Nordic Frost",    "[Nord]",
      { 0.14f, 0.16f, 0.20f, 0.98f }, { 0.38f, 0.68f, 0.90f, 1.00f }, { 0.93f, 0.95f, 0.97f, 1.00f }, 5.0f },
    { "Deep Purple",     "[Purp]",
      { 0.09f, 0.07f, 0.13f, 0.98f }, { 0.68f, 0.32f, 0.92f, 1.00f }, { 0.96f, 0.93f, 1.00f, 1.00f }, 5.0f },
    { "Cyber Neon",      "[Neon]",
      { 0.06f, 0.06f, 0.09f, 0.98f }, { 0.00f, 0.85f, 0.85f, 1.00f }, { 0.96f, 0.92f, 1.00f, 1.00f }, 4.0f },
    { "Teal Dark",       "[Teal]",
      { 0.06f, 0.10f, 0.11f, 0.98f }, { 0.12f, 0.75f, 0.65f, 1.00f }, { 0.88f, 0.97f, 0.96f, 1.00f }, 5.0f },
    { "Emerald Night",   "[Emrd]",
      { 0.06f, 0.10f, 0.07f, 0.98f }, { 0.16f, 0.78f, 0.40f, 1.00f }, { 0.88f, 0.98f, 0.90f, 1.00f }, 5.0f },
    { "Warm Charcoal",   "[Warm]",
      { 0.12f, 0.11f, 0.10f, 0.98f }, { 0.92f, 0.52f, 0.15f, 1.00f }, { 0.98f, 0.94f, 0.88f, 1.00f }, 4.0f },
    { "Crimson Night",   "[Crim]",
      { 0.10f, 0.06f, 0.07f, 0.98f }, { 0.85f, 0.20f, 0.25f, 1.00f }, { 0.98f, 0.89f, 0.90f, 1.00f }, 5.0f },
    { "Rose Gold",       "[Rose]",
      { 0.11f, 0.08f, 0.10f, 0.98f }, { 0.92f, 0.42f, 0.55f, 1.00f }, { 0.99f, 0.91f, 0.93f, 1.00f }, 6.0f },
    { "Slate Gray",      "[Slate]",
      { 0.13f, 0.14f, 0.16f, 0.98f }, { 0.50f, 0.54f, 0.65f, 1.00f }, { 0.95f, 0.96f, 0.98f, 1.00f }, 4.0f },
    { "Classic Light",   "[Light]",
      { 0.92f, 0.93f, 0.95f, 1.00f }, { 0.20f, 0.45f, 0.85f, 1.00f }, { 0.08f, 0.09f, 0.12f, 1.00f }, 4.0f },
    { "Sand Storm",      "[Sand]",
      { 0.14f, 0.12f, 0.09f, 0.98f }, { 0.82f, 0.65f, 0.25f, 1.00f }, { 0.98f, 0.95f, 0.84f, 1.00f }, 4.0f },
    { "Obsidian Pure",   "[Obsd]",
      { 0.04f, 0.04f, 0.05f, 0.98f }, { 0.80f, 0.84f, 0.92f, 1.00f }, { 0.95f, 0.96f, 0.98f, 1.00f }, 3.0f },
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
    if (!ImGui::GetCurrentContext()) return;
    ImGuiStyle& st = ImGui::GetStyle();

    const float r           = s_rounding;
    st.WindowRounding       = r;
    st.ChildRounding        = ClampF(r - 1.0f, 0.0f, 12.0f);
    st.FrameRounding        = ClampF(r - 2.0f, 0.0f, 10.0f);
    st.PopupRounding        = r;
    st.ScrollbarRounding    = 8.0f;
    st.GrabRounding         = ClampF(r - 2.0f, 0.0f, 10.0f);
    st.TabRounding          = ClampF(r - 1.0f, 0.0f, 10.0f);

    st.WindowBorderSize     = s_borderSize;
    st.FrameBorderSize      = s_borderSize > 0.5f ? 1.0f : 0.0f;
    st.PopupBorderSize      = s_borderSize;
    st.ChildBorderSize      = s_borderSize;
    st.TabBorderSize        = s_borderSize > 0.5f ? 1.0f : 0.0f;

    st.FramePadding         = { s_framePad * 1.6f, s_framePad };
    st.ItemSpacing          = { s_itemSpacing * 1.4f, s_itemSpacing };
    st.ItemInnerSpacing     = { s_itemSpacing * 0.8f, s_itemSpacing * 0.8f };
    st.WindowPadding        = { s_windowPad, s_windowPad };
    st.ScrollbarSize        = 12.0f;
    st.GrabMinSize          = 10.0f;
    st.IndentSpacing        = 16.0f;
    st.WindowTitleAlign     = { 0.0f, 0.5f };
    st.Alpha                = s_alpha;

    const ImVec4 accent     = ToImVec4(s_accent);
    const ImVec4 accentHov  = Brighten(accent, 1.15f);
    const ImVec4 accentAct  = Darken(accent, 0.82f);
    const ImVec4 bg         = ToImVec4(s_windowBg);
    const ImVec4 childBg    = ToImVec4(s_childBg);
    const ImVec4 popupBg    = ToImVec4(s_popupBg);
    const ImVec4 txt        = ToImVec4(s_text);
    const ImVec4 txtDis     = ToImVec4(s_textDisabled);
    const ImVec4 border     = ToImVec4(s_border);
    const ImVec4 scrollbar  = ToImVec4(s_scrollbar);

    bool isLightTheme = (bg.x + bg.y + bg.z) > 1.8f;
    const ImVec4 frameBg    = isLightTheme ? Darken(bg, 0.94f) : Brighten(bg, 1.30f);
    const ImVec4 frameBgHov = isLightTheme ? Darken(bg, 0.88f) : Brighten(bg, 1.60f);

    const ImVec4 titleBg    = Darken(bg, 0.80f);
    const ImVec4 titleActive= Mix(bg, accent, 0.22f);
    const ImVec4 menuBg     = Darken(bg, 0.85f);
    const ImVec4 tabBg      = Mix(bg, accent, 0.12f);
    const ImVec4 tabHovered = Mix(bg, accent, 0.35f);
    const ImVec4 tabActive  = Mix(bg, accent, 0.65f);

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
    st.Colors[ImGuiCol_FrameBgActive]         = WithAlpha(accent, 0.35f);
    st.Colors[ImGuiCol_TitleBg]               = titleBg;
    st.Colors[ImGuiCol_TitleBgActive]         = titleActive;
    st.Colors[ImGuiCol_TitleBgCollapsed]      = Darken(titleBg, 0.80f);
    st.Colors[ImGuiCol_MenuBarBg]             = menuBg;
    st.Colors[ImGuiCol_ScrollbarBg]           = scrollbar;
    st.Colors[ImGuiCol_ScrollbarGrab]         = Mix(bg, accent, 0.35f);
    st.Colors[ImGuiCol_ScrollbarGrabHovered]  = accent;
    st.Colors[ImGuiCol_ScrollbarGrabActive]   = accentAct;
    st.Colors[ImGuiCol_CheckMark]             = accent;
    st.Colors[ImGuiCol_SliderGrab]            = accent;
    st.Colors[ImGuiCol_SliderGrabActive]      = accentHov;
    st.Colors[ImGuiCol_Button]                = Mix(bg, accent, 0.22f);
    st.Colors[ImGuiCol_ButtonHovered]         = Mix(bg, accent, 0.45f);
    st.Colors[ImGuiCol_ButtonActive]          = accentAct;
    st.Colors[ImGuiCol_Header]                = WithAlpha(accent, 0.25f);
    st.Colors[ImGuiCol_HeaderHovered]         = WithAlpha(accent, 0.45f);
    st.Colors[ImGuiCol_HeaderActive]          = accent;
    st.Colors[ImGuiCol_Separator]             = WithAlpha(border, 0.75f);
    st.Colors[ImGuiCol_SeparatorHovered]      = accent;
    st.Colors[ImGuiCol_SeparatorActive]       = accentAct;
    st.Colors[ImGuiCol_ResizeGrip]            = WithAlpha(accent, 0.25f);
    st.Colors[ImGuiCol_ResizeGripHovered]     = WithAlpha(accent, 0.70f);
    st.Colors[ImGuiCol_ResizeGripActive]      = accent;
    st.Colors[ImGuiCol_Tab]                   = tabBg;
    st.Colors[ImGuiCol_TabHovered]            = tabHovered;
    st.Colors[ImGuiCol_TabActive]             = tabActive;
    st.Colors[ImGuiCol_TabUnfocused]          = Darken(tabBg, 0.85f);
    st.Colors[ImGuiCol_TabUnfocusedActive]    = tabBg;
    st.Colors[ImGuiCol_DockingPreview]        = WithAlpha(accent, 0.70f);
    st.Colors[ImGuiCol_DockingEmptyBg]        = Darken(bg, 0.60f);
    st.Colors[ImGuiCol_PlotLines]             = accent;
    st.Colors[ImGuiCol_PlotLinesHovered]      = accentHov;
    st.Colors[ImGuiCol_PlotHistogram]         = accent;
    st.Colors[ImGuiCol_PlotHistogramHovered]  = accentHov;
    st.Colors[ImGuiCol_TableHeaderBg]         = Brighten(bg, 1.20f);
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
    bool isLight = (t.bg[0] + t.bg[1] + t.bg[2]) > 1.8f;
    for (int i = 0; i < 3; i++) {
        s_childBg[i]   = isLight ? ClampF(t.bg[i] - 0.04f, 0.0f, 1.0f) : ClampF(t.bg[i] + 0.03f, 0.0f, 1.0f);
        s_popupBg[i]   = isLight ? ClampF(t.bg[i] - 0.02f, 0.0f, 1.0f) : ClampF(t.bg[i] + 0.02f, 0.0f, 1.0f);
        s_border[i]    = isLight ? ClampF(t.bg[i] - 0.15f, 0.0f, 1.0f) : ClampF(t.bg[i] + 0.14f, 0.0f, 1.0f);
        s_scrollbar[i] = t.bg[i] * 0.70f;
    }
    s_childBg[3]   = 0.70f;
    s_popupBg[3]   = 0.98f;
    s_border[3]    = 0.65f;
    s_scrollbar[3] = 1.00f;

    s_textDisabled[0] = ClampF(t.text[0] * 0.55f, 0.0f, 1.0f);
    s_textDisabled[1] = ClampF(t.text[1] * 0.55f, 0.0f, 1.0f);
    s_textDisabled[2] = ClampF(t.text[2] * 0.55f, 0.0f, 1.0f);
    s_textDisabled[3] = 1.00f;

    s_rounding = t.rounding;
    UpdateImGuiStyle();
}

// ============================================================
//  Save / Load / Reset
// ============================================================
static inline void SaveSettings()
{
    std::ofstream f(s_cfgFile);
    if (!f.is_open()) return;

    f << "[Theme]\n";
    f << "activeTheme="   << s_activeTheme << "\n";
    f << "windowBg="      << s_windowBg[0] << "," << s_windowBg[1] << "," << s_windowBg[2] << "," << s_windowBg[3] << "\n";
    f << "childBg="       << s_childBg[0]  << "," << s_childBg[1]  << "," << s_childBg[2]  << "," << s_childBg[3]  << "\n";
    f << "popupBg="       << s_popupBg[0]  << "," << s_popupBg[1]  << "," << s_popupBg[2]  << "," << s_popupBg[3]  << "\n";
    f << "accent="        << s_accent[0]   << "," << s_accent[1]   << "," << s_accent[2]   << "," << s_accent[3]   << "\n";
    f << "text="          << s_text[0]     << "," << s_text[1]     << "," << s_text[2]     << "," << s_text[3]     << "\n";
    f << "textDisabled="  << s_textDisabled[0] << "," << s_textDisabled[1] << "," << s_textDisabled[2] << "," << s_textDisabled[3] << "\n";
    f << "border="        << s_border[0]   << "," << s_border[1]   << "," << s_border[2]   << "," << s_border[3]   << "\n";
    f << "scrollbar="     << s_scrollbar[0]<< "," << s_scrollbar[1]<< "," << s_scrollbar[2]<< "," << s_scrollbar[3]<< "\n";
    f << "rounding="      << s_rounding    << "\n";
    f << "framePad="      << s_framePad    << "\n";
    f << "itemSpacing="   << s_itemSpacing << "\n";
    f << "windowPad="     << s_windowPad   << "\n";
    f << "borderSize="    << s_borderSize  << "\n";
    f << "alpha="         << s_alpha       << "\n";
    f.close();
}

static inline void LoadSettings()
{
    std::ifstream f(s_cfgFile);
    if (!f.is_open())
    {
        ApplyTheme(0);
        return;
    }

    std::string line;
    auto ParseVec4 = [](const std::string& val, float out[4]) {
        sscanf(val.c_str(), "%f,%f,%f,%f", &out[0], &out[1], &out[2], &out[3]);
    };

    while (std::getline(f, line))
    {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if      (key == "activeTheme")   s_activeTheme = atoi(val.c_str());
        else if (key == "windowBg")      ParseVec4(val, s_windowBg);
        else if (key == "childBg")       ParseVec4(val, s_childBg);
        else if (key == "popupBg")       ParseVec4(val, s_popupBg);
        else if (key == "accent")        ParseVec4(val, s_accent);
        else if (key == "text")          ParseVec4(val, s_text);
        else if (key == "textDisabled")  ParseVec4(val, s_textDisabled);
        else if (key == "border")        ParseVec4(val, s_border);
        else if (key == "scrollbar")     ParseVec4(val, s_scrollbar);
        else if (key == "rounding")      s_rounding    = (float)atof(val.c_str());
        else if (key == "framePad")      s_framePad    = (float)atof(val.c_str());
        else if (key == "itemSpacing")   s_itemSpacing = (float)atof(val.c_str());
        else if (key == "windowPad")     s_windowPad   = (float)atof(val.c_str());
        else if (key == "borderSize")    s_borderSize  = (float)atof(val.c_str());
        else if (key == "alpha")         s_alpha       = (float)atof(val.c_str());
    }
    f.close();
    UpdateImGuiStyle();
}

static inline void ResetDefaults()
{
    ApplyTheme(0);
    SaveSettings();
}

// ============================================================
//  UI Component Functions
// ============================================================
static inline void Open()    { s_showGui = true; }
static inline void Close()   { s_showGui = false; }
static inline void Enable()  { s_showGui = true; }
static inline void Disable() { s_showGui = false; }
static inline bool IsOpen()  { return s_showGui; }

static inline void SectionLabel(const char* label)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(s_accent));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
}

static inline void ThinSeparator()
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

inline void Render()
{
    if (!ImGui::GetCurrentContext()) return;
    if (!s_showGui) return;

    ImGui::SetNextWindowSize({ 540.0f, 480.0f }, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({ 420.0f, 360.0f }, { 1200.0f, 900.0f });

    if (!ImGui::Begin("UI Customization", &s_showGui,
        ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("CustomTabBar"))
    {
        if (ImGui::BeginTabItem("  Presets  "))
        {
            ImGui::Spacing();
            SectionLabel("  Theme Gallery");
            ImGui::TextDisabled("Select a preset studio theme to apply across the entire editor:");
            ImGui::Spacing();

            const float itemWidth  = 150.0f;
            const float itemHeight = 65.0f;
            const float spacing    = 10.0f;
            float availW           = ImGui::GetContentRegionAvail().x;
            int   cols             = (int)((availW + spacing) / (itemWidth + spacing));
            if (cols < 1) cols = 1;

            for (int i = 0; i < s_themeCount; i++)
            {
                if (i > 0 && (i % cols) != 0) ImGui::SameLine(0.0f, spacing);

                const Theme& th = s_themes[i];
                bool isActive = (s_activeTheme == i);

                ImGui::PushID(i);
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                ImVec2 p1 = { p0.x + itemWidth, p0.y + itemHeight };

                bool clicked = ImGui::InvisibleButton(th.name, { itemWidth, itemHeight });

                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImU32 bgCol     = IM_COL32((int)(th.bg[0]*255),(int)(th.bg[1]*255),(int)(th.bg[2]*255), 245);
                ImU32 accCol    = IM_COL32((int)(th.accent[0]*255),(int)(th.accent[1]*255),(int)(th.accent[2]*255), 255);
                ImU32 txtCol    = IM_COL32((int)(th.text[0]*255),(int)(th.text[1]*255),(int)(th.text[2]*255), 255);
                ImU32 borderCol = isActive ? accCol : IM_COL32(70, 75, 90, 180);

                dl->AddRectFilled(p0, p1, bgCol, 6.0f);
                dl->AddRect(p0, p1, borderCol, 6.0f, 0, isActive ? 2.0f : 1.0f);
                dl->AddRectFilled({ p0.x + 8, p0.y + 8 }, { p0.x + itemWidth - 8, p0.y + 20 }, accCol, 3.0f);

                char badge[64];
                _snprintf(badge, sizeof(badge), "%s %s", th.icon, th.name);
                dl->AddText({ p0.x + 10, p0.y + 26 }, txtCol, badge);

                if (isActive)
                {
                    dl->AddText({ p0.x + itemWidth - 28, p0.y + 44 }, accCol, "[v]");
                }

                if (clicked) ApplyTheme(i);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Apply theme '%s'", th.name);

                ImGui::PopID();
            }

            ImGui::Spacing();
            ImGui::EndTabItem();
        }

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

            if (ImGui::CollapsingHeader("  Text & Labels", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PushItemWidth(-1);
                ImGui::Spacing();
                Row("Primary text##txt",      "Base text color",        s_text);
                Row("Disabled text##dis",     "Hints and disabled labels color", s_textDisabled);
                ImGui::PopItemWidth();
                ImGui::Spacing();
            }

            if (ImGui::CollapsingHeader("  Accent Colors", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PushItemWidth(-1);
                ImGui::Spacing();
                Row("Accent color##acc", "Buttons, selection, headers, sliders", s_accent);
                ImGui::PopItemWidth();
                ImGui::Spacing();
            }

            if (changed) UpdateImGuiStyle();
            ImGui::EndTabItem();
        }

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

            if (changed) UpdateImGuiStyle();
            ImGui::Spacing();
            ImGui::EndTabItem();
        }

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

            ImGui::Spacing();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace Colors
