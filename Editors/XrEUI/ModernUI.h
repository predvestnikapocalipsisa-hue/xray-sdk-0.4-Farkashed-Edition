#pragma once
#include "imgui.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

// Small, embedded SVG path subset (absolute M/L/Z, viewBox 0 0 24 24).
// Paths stay vector data all the way to ImDrawList; no DDS atlas or SVG runtime.
namespace ModernUI
{
inline bool ClassicCustomization(const char* windowName)
{
    return windowName && std::strstr(windowName, "XrEUI Style & Color Manager") != nullptr;
}

inline ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t)
{
    return ImVec4(a.x + (b.x-a.x)*t, a.y + (b.y-a.y)*t,
        a.z + (b.z-a.z)*t, a.w + (b.w-a.w)*t);
}

inline const char* IconPath(const char* name)
{
    struct Icon { const char* name; const char* path; };
    static const Icon icons[] = {
        {"Undo", "M 9 4 L 4 9 L 9 14 M 4 9 L 14 9 L 18 11 L 20 15 L 19 19"},
        {"Redo", "M 15 4 L 20 9 L 15 14 M 20 9 L 10 9 L 6 11 L 4 15 L 5 19"},
        {"Select", "M 5 3 L 5 19 L 10 15 L 14 21 L 17 19 L 13 13 L 20 12 Z"},
        {"Add", "M 12 3 L 21 8 L 21 17 L 12 22 L 3 17 L 3 8 Z M 3 8 L 12 13 L 21 8 M 12 13 L 12 22 M 17 2 L 17 8 M 14 5 L 20 5"},
        {"Move", "M 12 2 L 12 22 M 2 12 L 22 12 M 8 6 L 12 2 L 16 6 M 8 18 L 12 22 L 16 18 M 6 8 L 2 12 L 6 16 M 18 8 L 22 12 L 18 16"},
        {"Rotate", "M 20 9 L 18 5 L 12 3 L 6 5 L 3 11 L 5 18 L 11 21 L 18 19 L 21 14 M 16 9 L 21 9 L 21 4"},
        {"Scale", "M 4 10 L 4 20 L 14 20 M 4 20 L 20 4 M 13 4 L 20 4 L 20 11"},
        {"CsLocal", "M 12 3 L 21 8 L 21 17 L 12 22 L 3 17 L 3 8 Z M 3 8 L 12 13 L 21 8 M 12 13 L 12 22"},
        {"NuScale", "M 3 5 L 21 5 L 21 19 L 3 19 Z M 7 9 L 17 15 M 13 15 L 17 15 L 17 11"},
        {"GSnap", "M 4 4 L 20 4 L 20 20 L 4 20 Z M 4 12 L 20 12 M 12 4 L 12 20"},
        {"OSnap", "M 4 4 L 14 4 L 14 14 L 4 14 Z M 10 10 L 20 10 L 20 20 L 10 20 Z"},
        {"MoveToSnap", "M 3 5 L 3 19 L 17 19 M 7 15 L 20 2 M 14 2 L 20 2 L 20 8"},
        {"NSnap", "M 3 19 L 21 19 M 12 19 L 12 3 M 8 7 L 12 3 L 16 7"},
        {"VSnap", "M 3 20 L 12 4 L 21 20 Z M 9 4 L 15 4 M 12 1 L 12 7"},
        {"ASnap", "M 3 4 L 3 20 L 21 20 M 3 20 L 19 4 M 8 15 L 10 16 L 11 20"},
        {"MSnap", "M 4 3 L 4 14 L 6 19 L 12 21 L 18 19 L 20 14 L 20 3 L 15 3 L 15 14 L 12 16 L 9 14 L 9 3 Z M 4 8 L 9 8 M 15 8 L 20 8"},
        {"Zoom", "M 3 9 L 3 3 L 9 3 M 15 3 L 21 3 L 21 9 M 21 15 L 21 21 L 15 21 M 9 21 L 3 21 L 3 15"},
        {"ZoomSel", "M 8 8 L 16 8 L 16 16 L 8 16 Z M 3 7 L 3 3 L 7 3 M 17 3 L 21 3 L 21 7 M 21 17 L 21 21 L 17 21 M 7 21 L 3 21 L 3 17"},
        {"RunInGame", "M 7 3 L 21 12 L 7 21 Z"},
        {"Panel", "M 3 4 L 21 4 L 21 20 L 3 20 Z M 3 9 L 21 9 M 9 9 L 9 20"},
        {"Render", "M 3 4 L 21 4 L 21 17 L 3 17 Z M 8 21 L 16 21 M 12 17 L 12 21 M 10 8 L 16 11 L 10 14 Z"},
        {"Settings", "M 4 6 L 20 6 M 4 12 L 20 12 M 4 18 L 20 18 M 8 3 L 8 9 M 16 9 L 16 15 M 10 15 L 10 21"},
        {"Log", "M 4 3 L 20 3 L 20 21 L 4 21 Z M 8 8 L 16 8 M 8 12 L 16 12 M 8 16 L 13 16"},
        {"Scene", "M 9 3 L 15 3 L 15 9 L 9 9 Z M 12 9 L 12 13 M 5 13 L 19 13 M 5 13 L 5 16 M 19 13 L 19 16 M 2 16 L 8 16 L 8 22 L 2 22 Z M 16 16 L 22 16 L 22 22 L 16 22 Z"},
        {"Folder", "M 3 7 L 3 20 L 21 20 L 21 7 L 12 7 L 9 4 L 3 4 Z"},
        {"Save", "M 4 3 L 17 3 L 21 7 L 21 21 L 3 21 L 3 3 Z M 7 3 L 7 9 L 16 9 L 16 3 M 7 21 L 7 14 L 17 14 L 17 21"},
        {"Copy", "M 8 8 L 21 8 L 21 21 L 8 21 Z M 16 8 L 16 3 L 3 3 L 3 16 L 8 16"},
        {"Delete", "M 3 6 L 21 6 M 8 6 L 8 3 L 16 3 L 16 6 M 5 6 L 6 21 L 18 21 L 19 6 M 10 10 L 10 17 M 14 10 L 14 17"},
        {"Filter", "M 3 4 L 21 4 L 14 12 L 14 20 L 10 18 L 10 12 Z"},
        {"Plus", "M 4 12 L 20 12 M 12 4 L 12 20"},
        {"Minus", "M 4 12 L 20 12"},
        {"Check", "M 4 12 L 9 17 L 20 6"},
        {"Close", "M 5 5 L 19 19 M 19 5 L 5 19"},
        {"Action", "M 4 12 L 20 12 M 14 6 L 20 12 L 14 18"},
        {"Support", "M 12 21 L 3 12 L 2 7 L 5 3 L 9 3 L 12 6 L 15 3 L 19 3 L 22 7 L 21 12 Z"},
        {"GitHub", "M 5 7 L 4 2 L 9 4 L 15 4 L 20 2 L 19 7 L 22 11 L 21 15 L 17 18 L 15 18 L 15 22 M 9 22 L 9 18 L 6 17 L 3 14 L 2 10 L 5 7 M 3 18 L 6 21 L 9 21"},
        {"Wiki", "M 12 5 L 8 3 L 2 3 L 2 20 L 8 20 L 12 22 L 16 20 L 22 20 L 22 3 L 16 3 Z M 12 5 L 12 22"}
    };
    for (const Icon& icon : icons)
        if (std::strcmp(name, icon.name) == 0) return icon.path;
    return nullptr;
}

inline void DrawPath(const char* path, ImVec2 origin, float size, ImU32 color, ImDrawList* draw = nullptr)
{
    if (!draw) draw = ImGui::GetWindowDrawList();
    const ImDrawListFlags savedFlags = draw->Flags;
    draw->Flags |= ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill;
    const float scale = size / 24.f;
    while (*path)
    {
        const char command = *path++;
        if (command == 'Z') { draw->PathStroke(color, ImDrawFlags_Closed, 1.5f); continue; }
        if (command != 'M' && command != 'L') continue;
        char* end = nullptr;
        float x = std::strtof(path, &end); path = end;
        float y = std::strtof(path, &end); path = end;
        ImVec2 point(origin.x + x*scale, origin.y + y*scale);
        // Stroke each connected contour together so joins share an anti-aliased fringe.
        if (command == 'M') draw->PathStroke(color, 0, 1.5f);
        draw->PathLineTo(point);
    }
    draw->PathStroke(color, 0, 1.5f);
    draw->Flags = savedFlags;
}

inline bool HasWord(const char* text, const char* word)
{
    for (; *text && !(text[0] == '#' && text[1] == '#'); ++text)
    {
        const char* a = text;
        const char* b = word;
        while (*a && *b && ((*a >= 'A' && *a <= 'Z') ? *a + ('a'-'A') : *a) == *b) { ++a; ++b; }
        if (!*b) return true;
    }
    return false;
}

inline const char* CaptionIcon(const char* label, bool window)
{
    if (!label || !*label || (label[0] == '#' && label[1] == '#')) return nullptr;
    if (!window && HasWord(label,"support")) return IconPath("Support");
    if (!window && HasWord(label,"github")) return IconPath("GitHub");
    if (!window && HasWord(label,"wiki")) return IconPath("Wiki");
    if (window)
    {
        if (HasWord(label,"render") || HasWord(label,"preview")) return IconPath("Render");
        if (HasWord(label,"propert") || HasWord(label,"pref") || HasWord(label,"style") || HasWord(label,"option")) return IconPath("Settings");
        if (HasWord(label,"log")) return IconPath("Log");
        if (HasWord(label,"scene") || HasWord(label,"object")) return IconPath("Scene");
        if (HasWord(label,"choose") || HasWord(label,"library") || HasWord(label,"browser")) return IconPath("Folder");
        return IconPath("Panel");
    }
    if (!std::strcmp(label,"+")) return IconPath("Plus");
    if (!std::strcmp(label,"-")) return IconPath("Minus");
    if (HasWord(label,"clear") || HasWord(label,"delete") || HasWord(label,"remove")) return IconPath("Delete");
    if (HasWord(label,"save")) return IconPath("Save");
    if (HasWord(label,"copy") || HasWord(label,"clone")) return IconPath("Copy");
    if (HasWord(label,"filter")) return IconPath("Filter");
    if (HasWord(label,"undo")) return IconPath("Undo");
    if (HasWord(label,"redo") || HasWord(label,"refresh") || HasWord(label,"flush")) return IconPath("Redo");
    if (HasWord(label,"close") || HasWord(label,"cancel") || HasWord(label,"hide")) return IconPath("Close");
    if (HasWord(label,"ok") || HasWord(label,"apply") || HasWord(label,"done")) return IconPath("Check");
    if (HasWord(label,"open") || HasWord(label,"load") || HasWord(label,"browse")) return IconPath("Folder");
    if (HasWord(label,"prop") || HasWord(label,"config") || HasWord(label,"setting") || HasWord(label,"...")) return IconPath("Settings");
    if (HasWord(label,"add") || HasWord(label,"create") || HasWord(label,"generate")) return IconPath("Plus");
    return IconPath("Action");
}

inline float Animate(ImGuiStorage* storage, ImGuiID id, float target)
{
    const ImGuiID key = id ^ 0xA715AD31u;
    float value = storage->GetFloat(key, target);
    value += (target-value) * (1.f-std::exp(-18.f*ImGui::GetIO().DeltaTime));
    if (std::fabs(target-value) < .005f) value = target;
    storage->SetFloat(key,value);
    return value;
}

inline void ButtonSurface(ImDrawList* draw, ImVec2 min, ImVec2 max, float fade, bool held)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec4 bg = Mix(style.Colors[ImGuiCol_WindowBg],
        style.Colors[held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered], fade);
    draw->AddRectFilled(min,max,ImGui::GetColorU32(bg),style.FrameRounding);
    ImVec4 outline = style.Colors[ImGuiCol_CheckMark]; outline.w *= fade*.65f;
    draw->AddRect(ImVec2(min.x+.5f,min.y+.5f),ImVec2(max.x-.5f,max.y-.5f),
        ImGui::GetColorU32(outline),style.FrameRounding);
}

inline void Disclosure(ImDrawList* draw, ImVec2 pos, ImU32 color, float open, float scale)
{
    const float size = ImGui::GetFontSize()*scale;
    const ImVec2 center(pos.x+size*.5f,pos.y+size*.5f);
    const float angle = open*1.5707963f, c = std::cos(angle), s = std::sin(angle);
    const ImVec2 points[] = { ImVec2(-.18f,-.28f), ImVec2(.24f,0), ImVec2(-.18f,.28f) };
    ImVec2 p[3];
    for (int i=0;i<3;++i) p[i] = ImVec2(center.x+(points[i].x*c-points[i].y*s)*size,
        center.y+(points[i].x*s+points[i].y*c)*size);
    draw->AddTriangleFilled(p[0],p[1],p[2],color);
}

inline bool ToolButton(const char* id, bool selected = false, const char* label = nullptr)
{
    const float dpi = ImGui::GetIO().FontGlobalScale;
    const float buttonSize = 28.f*dpi;
    ImGui::PushID(id);
    // A real Button retains ImGui keyboard/gamepad activation and focus semantics.
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0,0,0,0));
    const bool pressed = ImGui::Button("##tool", ImVec2(buttonSize,buttonSize));
    ImGui::PopStyleColor(3);
    // A click leaves navigation focus on the button; it must not keep mouse hover lit.
    const bool hovered = ImGui::IsItemHovered() &&
        ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const bool active = ImGui::IsItemActive();
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID animation = ImGui::GetID("hover");
    float fade = storage->GetFloat(animation, 0.f);
    fade += ((hovered ? 1.f : 0.f) - fade) *
        (1.f - std::exp(-14.f * ImGui::GetIO().DeltaTime));
    if (!hovered && fade < .005f) fade = 0.f;
    storage->SetFloat(animation, fade);
    const ImVec2 p = ImGui::GetItemRectMin();
    const ImVec2 end = ImGui::GetItemRectMax();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec4 base = selected ? style.Colors[ImGuiCol_Header] : style.Colors[ImGuiCol_WindowBg];
    ImVec4 bg = Mix(base, style.Colors[active ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered], fade);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(p, end, ImGui::GetColorU32(bg), 6.f*dpi);
    ImVec4 outline = style.Colors[ImGuiCol_CheckMark];
    outline.w *= fade * .65f;
    draw->AddRect(ImVec2(p.x+.5f,p.y+.5f), ImVec2(end.x-.5f,end.y-.5f),
        ImGui::GetColorU32(outline), 6.f*dpi);
    if (selected)
        draw->AddLine(ImVec2(p.x+8*dpi,end.y-2*dpi), ImVec2(end.x-8*dpi,end.y-2*dpi),
            ImGui::GetColorU32(ImGuiCol_CheckMark), 2.f);
    const ImU32 color = ImGui::GetColorU32(Mix(style.Colors[ImGuiCol_Text],
        style.Colors[ImGuiCol_CheckMark], fade * .35f));
    if (const char* path = IconPath(id))
        DrawPath(path, ImVec2(p.x+5*dpi,p.y+5*dpi), 18.f*dpi, color);
    else
    {
        const char* text = label ? label : id;
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        draw->AddText(ImVec2(p.x+(buttonSize-textSize.x)*.5f, p.y+(buttonSize-textSize.y)*.5f), color, text);
    }
    // Redraw the navigation focus ring after the custom background.
    if (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible)
        draw->AddRect(p,end,ImGui::GetColorU32(ImGuiCol_NavHighlight),6.f*dpi);
    ImGui::PopID();
    return pressed;
}

inline void ToolDivider()
{
    const float dpi = ImGui::GetIO().FontGlobalScale;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x+3*dpi,p.y+6*dpi),ImVec2(p.x+3*dpi,p.y+22*dpi),
        ImGui::GetColorU32(ImGuiCol_Border));
    ImGui::Dummy(ImVec2(7*dpi,28*dpi));
    ImGui::SameLine();
}
}
