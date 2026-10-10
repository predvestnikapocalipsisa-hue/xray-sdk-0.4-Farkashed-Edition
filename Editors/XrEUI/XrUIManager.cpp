#include "stdafx.h"
#include "SDKMessageBox.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "imgui_internal.h"
#include "ModernUI.h"
#include "../../Launcher/DiscordPresence.h"
#include "../../Launcher/UpdateBridge.h"
#include "../../LauncherAssets/ResourceIds.h"
#include <string>
#include <vector>
#include <dwmapi.h>
#include <cmath>
#include <cstring>
#include <memory>
#include <algorithm>
#include <sstream>
#include <d3dx9.h>
#include <shellapi.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

namespace
{
    FarkashedDiscord::Presence editorPresence;
    HMODULE editorFontAssets = nullptr;
    HWND editorWindow = nullptr;
    bool effectsDirty = true;
    bool customFrame = false;
    float uiScale = 1.f, requestedUIScale = 1.f;
    bool aboutRequested = false;
    IDirect3DTexture9* aboutLogo = nullptr;
    IDirect3DDevice9* aboutDevice = nullptr;
    float aboutLogoAspect = 1.93f;
    bool aboutLogoAttempted = false;

    struct LauncherBuildInfo
    {
        std::string version = "unavailable", date = "unavailable", hash = "unknown", branch = "unknown";
        LauncherBuildInfo()
        {
            HMODULE launcher = GetModuleHandleW(nullptr);
            HRSRC resource = FindResourceW(launcher,MAKEINTRESOURCEW(IDR_SDK_BUILD_INFO),MAKEINTRESOURCEW(10));
            HGLOBAL loaded = resource ? LoadResource(launcher,resource) : nullptr;
            const char* bytes = loaded ? static_cast<const char*>(LockResource(loaded)) : nullptr;
            if (!bytes) return;
            std::istringstream stream(std::string(bytes,SizeofResource(launcher,resource)));
            std::string* fields[] = { &version,&date,&hash,&branch };
            for (std::string* field : fields)
            {
                std::string line;
                if (std::getline(stream,line) && !line.empty())
                {
                    if (line.back() == '\r') line.pop_back();
                    *field = line;
                }
            }
        }
    };

    void DrawAbout()
    {
        if (aboutRequested) { ImGui::OpenPopup("About X-Ray SDK"); aboutRequested = false; }
        const float dpi = uiScale;
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(),ImGuiCond_Appearing,ImVec2(.5f,.5f));
        ImGui::SetNextWindowSize(ImVec2(510.f*dpi,390.f*dpi));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,16.f*dpi);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(28.f*dpi,18.f*dpi));
        const bool visible = ImGui::BeginPopupModal("About X-Ray SDK",nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar(2);
        if (!visible) return;
        if (!aboutLogoAttempted)
        {
            aboutLogoAttempted = true;
            HRSRC resource = editorFontAssets ? FindResourceW(editorFontAssets,MAKEINTRESOURCEW(IDR_LAUNCHER_LOGO),MAKEINTRESOURCEW(10)) : nullptr;
            HGLOBAL loaded = resource ? LoadResource(editorFontAssets,resource) : nullptr;
            void* bytes = loaded ? LockResource(loaded) : nullptr;
            const DWORD size = resource ? SizeofResource(editorFontAssets,resource) : 0;
            D3DXIMAGE_INFO info = {};
            if (bytes && aboutDevice && SUCCEEDED(D3DXGetImageInfoFromFileInMemory(bytes,size,&info)) && info.Height)
            {
                if (SUCCEEDED(D3DXCreateTextureFromFileInMemoryEx(aboutDevice,bytes,size,info.Width,info.Height,
                    1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,D3DX_FILTER_LINEAR,D3DX_FILTER_LINEAR,0,nullptr,nullptr,&aboutLogo)))
                    aboutLogoAspect = float(info.Width)/info.Height;
            }
        }
        static const LauncherBuildInfo info;
        const float imageWidth = ImMin(410.f*dpi,ImGui::GetContentRegionAvail().x);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth()-imageWidth)*.5f);
        if (aboutLogo) ImGui::Image(aboutLogo,ImVec2(imageWidth,imageWidth/aboutLogoAspect));
        else { ImGui::TextUnformatted("X-RAY SDK / FARKASHED EDITION"); ImGui::Dummy(ImVec2(0,130.f*dpi)); }
        ImGui::Spacing();
        if (ImGui::BeginTable("About information",2,ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Build",ImGuiTableColumnFlags_WidthStretch,.62f);
            ImGui::TableSetupColumn("Links",ImGuiTableColumnFlags_WidthStretch,.38f);
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            ImGui::TextDisabled("X-Ray SDK"); ImGui::Spacing();
            ImGui::TextDisabled("VERSION: %s",info.version.c_str());
            ImGui::TextDisabled("DATE: %s",info.date.c_str());
            ImGui::TextDisabled("HASH: %s",info.hash.c_str());
            ImGui::TextDisabled("BRANCH: %s",info.branch.c_str());
            ImGui::TableNextColumn();
            if (ImGui::Button("Support")) ShellExecuteW(nullptr,L"open",L"https://t.me/+nIwAehu-AX8xMjRi",nullptr,nullptr,SW_SHOWNORMAL);
            if (ImGui::Button("GitHub")) ShellExecuteW(nullptr,L"open",L"https://github.com/predvestnikapocalipsisa-hue/xray-sdk-0.4-Farkashed-Edition",nullptr,nullptr,SW_SHOWNORMAL);
            if (ImGui::Button("Check Updates") && !SDKUpdates::Open(FarkashedLauncher::ModuleDirectory()))
                SDKDialogs::Notify("Cannot open SDK updater.", "SDK updates", MB_OK | MB_ICONERROR);
            ImGui::EndTable();
        }
        const char* credits = "Credits: TSMP, Red Panda, Lunar, Zarya";
        ImGui::SetCursorPosX((ImGui::GetWindowWidth()-ImGui::CalcTextSize(credits).x)*.5f);
        ImGui::TextDisabled("%s",credits);
        const float closeWidth = 100.f*dpi;
        ImGui::SetCursorPosY(ImMax(ImGui::GetCursorPosY(),ImGui::GetWindowHeight()-18.f*dpi-ImGui::GetFrameHeight()));
        ImGui::SetCursorPosX((ImGui::GetWindowWidth()-closeWidth)*.5f);
        if (ImGui::Button("Close",ImVec2(closeWidth,0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void ApplyUIScale()
    {
        if (uiScale == requestedUIScale) return;
        const float ratio = requestedUIScale/uiScale;
        ImGuiStyle& style = ImGui::GetStyle();
        // Float scaling avoids ScaleAllSizes' repeated floor rounding on slider drags.
        ImVec2* vectors[] = { &style.WindowPadding, &style.WindowMinSize, &style.FramePadding,
            &style.ItemSpacing, &style.ItemInnerSpacing, &style.CellPadding,
            &style.TouchExtraPadding, &style.DisplayWindowPadding, &style.DisplaySafeAreaPadding };
        for (ImVec2* value : vectors) { value->x *= ratio; value->y *= ratio; }
        float* sizes[] = { &style.WindowRounding, &style.ChildRounding, &style.PopupRounding,
            &style.FrameRounding, &style.IndentSpacing, &style.ColumnsMinSpacing,
            &style.ScrollbarSize, &style.ScrollbarRounding, &style.GrabMinSize, &style.GrabRounding,
            &style.LogSliderDeadzone, &style.TabRounding, &style.MouseCursorScale,
            &style.WindowBorderSize, &style.ChildBorderSize, &style.PopupBorderSize,
            &style.FrameBorderSize, &style.TabBorderSize };
        for (float* value : sizes) *value *= ratio;
        if (style.TabMinWidthForCloseButton != FLT_MAX) style.TabMinWidthForCloseButton *= ratio;
        uiScale = requestedUIScale;
        ImGui::GetIO().FontGlobalScale = uiScale;
    }

    struct WindowVisual
    {
        struct Layer { ImDrawList* draw; int order; };
        ImGuiID id;
        int lastFrame = -1;
        float phase = 0.f;
        std::vector<Layer> layers;
        std::vector<IDirect3DBaseTexture9*> textures;
        explicit WindowVisual(ImGuiID value) : id(value) {}
        void ClearLayers()
        {
            for (const Layer& layer : layers) IM_DELETE(layer.draw);
            layers.clear();
            for (auto* texture : textures) texture->Release();
            textures.clear();
        }
        ~WindowVisual() { ClearLayers(); }
        void Capture(ImDrawList* source, int order)
        {
            ImDrawList* copy = source->CloneOutput();
            for (ImDrawCmd& command : copy->CmdBuffer)
            {
                // Never replay callbacks belonging to controls that have already closed.
                if (command.UserCallback && command.UserCallback != ImDrawCallback_ResetRenderState)
                {
                    command.UserCallback = nullptr;
                    command.UserCallbackData = nullptr;
                    command.ElemCount = 0;
                }
                auto* texture = command.GetTexID();
                if (texture && std::find(textures.begin(),textures.end(),texture) == textures.end())
                {
                    texture->AddRef();
                    textures.push_back(texture);
                }
            }
            layers.push_back({copy,order});
        }
    };
    std::vector<std::unique_ptr<WindowVisual>> windowVisuals;

    ImGuiWindow* AnimatedRoot(ImGuiWindow* window)
    {
        while (window->ParentWindow && (window->Flags & ImGuiWindowFlags_ChildWindow) &&
            !(window->Flags & ImGuiWindowFlags_Popup)) window = window->ParentWindow;
        if (window->DockIsActive) return nullptr;
        if ((window->Flags & ImGuiWindowFlags_NoTitleBar) &&
            !(window->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip))) return nullptr;
        return window;
    }

    void TintVisual(ImDrawList* draw, float phase)
    {
        const float fade = phase*phase*(3.f-2.f*phase);
        for (ImDrawVert& vertex : draw->VtxBuffer)
        {
            const ImU32 alpha = (vertex.col & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT;
            vertex.col = (vertex.col & ~IM_COL32_A_MASK) | (ImU32(alpha*fade) << IM_COL32_A_SHIFT);
        }
    }

    void RenderAnimatedWindows()
    {
        ImDrawData* original = ImGui::GetDrawData();
        if (!original || !original->Valid) return;
        ImGuiContext& context = *ImGui::GetCurrentContext();
        const float dt = ImMax(0.f,ImGui::GetIO().DeltaTime);
        struct RenderLayer { ImDrawList* draw; int order; bool closing; };
        std::vector<RenderLayer> layers;
        std::vector<ImDrawList*> temporary;
        for (int i=0;i<original->CmdListsCount;++i)
        {
            ImDrawList* draw = original->CmdLists[i];
            ImGuiWindow* root = nullptr;
            for (ImGuiWindow* window : context.Windows)
                if (window->Active && !window->Hidden && window->DrawList == draw)
                { root = AnimatedRoot(window); break; }
            if (root)
            {
                WindowVisual* visual = nullptr;
                for (const auto& candidate : windowVisuals)
                    if (candidate->id == root->ID) { visual = candidate.get(); break; }
                if (!visual)
                {
                    windowVisuals.emplace_back(new WindowVisual(root->ID));
                    visual = windowVisuals.back().get();
                }
                if (visual->lastFrame != context.FrameCount)
                {
                    visual->ClearLayers();
                    visual->lastFrame = context.FrameCount;
                    visual->phase = ImMin(1.f,visual->phase+dt/.16f);
                }
                // Capture before tinting: closing must fade from the current opacity,
                // and children/lists must share exactly the parent's timeline.
                visual->Capture(draw,i);
                TintVisual(draw,visual->phase);
            }
            layers.push_back({draw,i,false});
        }
        for (auto it=windowVisuals.begin();it!=windowVisuals.end();)
        {
            WindowVisual& visual = **it;
            if (visual.lastFrame == context.FrameCount) { ++it; continue; }
            visual.phase = ImMax(0.f,visual.phase-dt/.12f);
            if (visual.phase <= 0.f) { it = windowVisuals.erase(it); continue; }
            for (const WindowVisual::Layer& layer : visual.layers)
            {
                ImDrawList* copy = layer.draw->CloneOutput();
                TintVisual(copy,visual.phase);
                temporary.push_back(copy);
                layers.push_back({copy,layer.order,true});
            }
            ++it;
        }
        // Keep retiring visuals at their previous depth, below newly opened popups.
        std::stable_sort(layers.begin(),layers.end(),[](const RenderLayer& a,const RenderLayer& b)
        { return a.order != b.order ? a.order < b.order : a.closing && !b.closing; });
        std::vector<ImDrawList*> lists;
        ImDrawData composed = *original;
        composed.TotalVtxCount = composed.TotalIdxCount = 0;
        for (const RenderLayer& layer : layers)
        {
            lists.push_back(layer.draw);
            composed.TotalVtxCount += layer.draw->VtxBuffer.Size;
            composed.TotalIdxCount += layer.draw->IdxBuffer.Size;
        }
        composed.CmdLists = lists.data();
        composed.CmdListsCount = int(lists.size());
        ImGui_ImplDX9_RenderDrawData(&composed);
        for (ImDrawList* draw : temporary) IM_DELETE(draw);
    }

    void DrawWindowTitleBar()
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f*uiScale,0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f,0.f));
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (ImGui::BeginViewportSideBar("SDK window title", viewport, ImGuiDir_Up, XrUIManager::GetTitleBarHeight(), flags))
        {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec2 origin = ImGui::GetWindowPos();
            const float width = ImGui::GetWindowWidth();
            const float textY = origin.y + (XrUIManager::GetTitleBarHeight() - ImGui::GetFontSize()) * .5f;
            const float controlsX = origin.x + width - 100.f*uiScale;
            draw->PushClipRect(origin, ImVec2(controlsX, origin.y+XrUIManager::GetTitleBarHeight()), true);
            draw->AddText(ImVec2(origin.x+12.f*uiScale,textY), ImGui::GetColorU32(ImGuiCol_CheckMark), "X-RAY SDK");
            char caption[256] = {};
            GetWindowTextA(editorWindow, caption, sizeof(caption));
            const xr_string name = XrUIManager::ConvertCP1251ToUTF8(caption);
            const float labelX = origin.x + 28.f*uiScale + ImGui::CalcTextSize("X-RAY SDK").x;
            draw->AddText(ImVec2(labelX,textY), ImGui::GetColorU32(ImGuiCol_TextDisabled), name.c_str());
            draw->PopClipRect();
            for (int i = 0; i < 3; ++i)
            {
                ImGui::PushID(i);
                ImGui::SetCursorScreenPos(ImVec2(controlsX + i*32.f*uiScale,origin.y+3.f*uiScale));
                const bool clicked = ImGui::InvisibleButton("Window control", ImVec2(28.f*uiScale,28.f*uiScale));
                const bool hover = ImGui::IsItemHovered() && ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax()) && !ImGui::GetIO().AppFocusLost;
                const bool active = ImGui::IsItemActive();
                const ImVec2 p = ImGui::GetItemRectMin();
                const ImVec2 end = ImGui::GetItemRectMax();
                const float fade = ModernUI::Animate(ImGui::GetStateStorage(),ImGui::GetID("control hover"),hover ? 1.f : 0.f);
                ModernUI::ButtonSurface(draw,p,end,fade,active);
                const ImU32 ink = ImGui::GetColorU32(ModernUI::Mix(ImGui::GetStyle().Colors[ImGuiCol_Text],
                    ImGui::GetStyle().Colors[ImGuiCol_CheckMark],fade*.35f));
                const ImVec2 c(p.x+14.f*uiScale,p.y+14.f*uiScale);
                if (i == 0) draw->AddLine(ImVec2(c.x-5,c.y+3),ImVec2(c.x+5,c.y+3),ink,1.5f);
                else if (i == 1)
                {
                    if (IsZoomed(editorWindow))
                    {
                        draw->AddRect(ImVec2(c.x-3,c.y-5),ImVec2(c.x+5,c.y+3),ink,0.f,0,1.5f);
                        draw->AddRectFilled(ImVec2(c.x-5,c.y-3),ImVec2(c.x+3,c.y+5),ImGui::GetColorU32(ImGuiCol_WindowBg));
                        draw->AddRect(ImVec2(c.x-5,c.y-3),ImVec2(c.x+3,c.y+5),ink,0.f,0,1.5f);
                    }
                    else draw->AddRect(ImVec2(c.x-5,c.y-5),ImVec2(c.x+5,c.y+5),ink,0.f,0,1.5f);
                }
                else
                {
                    draw->AddLine(ImVec2(c.x-4,c.y-4),ImVec2(c.x+4,c.y+4),ink,1.5f);
                    draw->AddLine(ImVec2(c.x+4,c.y-4),ImVec2(c.x-4,c.y+4),ink,1.5f);
                }
                if (hover) ImGui::SetTooltip("%s", i == 0 ? "Minimize" : i == 1 ?
                    (IsZoomed(editorWindow) ? "Restore" : "Maximize") : "Close");
                if (clicked)
                {
                    // Post through the existing window procedure, including the unsaved-scene guard.
                    if (i == 2) PostMessageW(editorWindow, WM_CLOSE, 0, 0);
                    else PostMessageW(editorWindow, WM_SYSCOMMAND,
                        i == 0 ? SC_MINIMIZE : (IsZoomed(editorWindow) ? SC_RESTORE : SC_MAXIMIZE), 0);
                }
                ImGui::PopID();
            }
            draw->AddLine(ImVec2(origin.x,origin.y+XrUIManager::GetTitleBarHeight()-.5f),
                ImVec2(origin.x+width,origin.y+XrUIManager::GetTitleBarHeight()-.5f),ImGui::GetColorU32(ImGuiCol_Border));
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
    }

    void UpdateBackdrop()
    {
        if (!effectsDirty || !editorWindow) return;
        effectsDirty = false;
        // Explicitly retire backdrop/glow even when legacy settings requested them.
        const int backdropType = 1;
        DwmSetWindowAttribute(editorWindow,38,&backdropType,sizeof(backdropType));
        struct AccentPolicy { int state; int flags; DWORD color; int animation; };
        struct CompositionData { int attribute; void* data; SIZE_T size; };
        using SetComposition = BOOL(WINAPI*)(HWND,CompositionData*);
        const auto setComposition = reinterpret_cast<SetComposition>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetWindowCompositionAttribute"));
        if (setComposition)
        {
            AccentPolicy policy = {};
            CompositionData data = {19,&policy,sizeof(policy)};
            setComposition(editorWindow,&data);
        }
        const MARGINS margins = {};
        DwmExtendFrameIntoClientArea(editorWindow,&margins);
        const COLORREF border = 0xFFFFFFFE; // DWMWA_COLOR_NONE
        DwmSetWindowAttribute(editorWindow,34,&border,sizeof(border));
    }

    void UpdateCaptionTheme()
    {
        // Resolve DWM dynamically; older Windows versions simply keep their native caption.
        typedef HRESULT (WINAPI *SetAttribute)(HWND, DWORD, LPCVOID, DWORD);
        static HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        static SetAttribute setAttribute = dwm ? reinterpret_cast<SetAttribute>(
            GetProcAddress(dwm, "DwmSetWindowAttribute")) : nullptr;
        if (!setAttribute || !editorWindow) return;
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec4 bg = style.Colors[ImGuiCol_WindowBg];
        const ImVec4 text = style.Colors[ImGuiCol_Text];
        const COLORREF caption = RGB(int(bg.x*255.f), int(bg.y*255.f), int(bg.z*255.f));
        const COLORREF foreground = RGB(int(text.x*255.f), int(text.y*255.f), int(text.z*255.f));
        static HWND appliedWindow = nullptr;
        static COLORREF appliedCaption = 0, appliedText = 0;
        if (appliedWindow == editorWindow && appliedCaption == caption && appliedText == foreground) return;
        const BOOL dark = bg.x*.2126f + bg.y*.7152f + bg.z*.0722f < .5f;
        setAttribute(editorWindow, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
        setAttribute(editorWindow, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
        setAttribute(editorWindow, 36 /* DWMWA_TEXT_COLOR */, &foreground, sizeof(foreground));
        appliedWindow = editorWindow;
        appliedCaption = caption;
        appliedText = foreground;
    }
}

#define USE_OLD_STYLE 1

static const char* GetClipboardTextFn_Custom(void* user_data)
{
    static std::string clipboard_str;
    if (!OpenClipboard(NULL)) return NULL;

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData != NULL)
    {
        wchar_t* pwszText = (wchar_t*)GlobalLock(hData);
        if (pwszText)
        {
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, pwszText, -1, NULL, 0, NULL, NULL);
            if (size_needed > 0)
            {
                std::vector<char> buf(size_needed);
                WideCharToMultiByte(CP_UTF8, 0, pwszText, -1, buf.data(), size_needed, NULL, NULL);
                clipboard_str.assign(buf.data());
            }
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return clipboard_str.c_str();
}

static void SetClipboardTextFn_Custom(void* user_data, const char* text) {
    if (!OpenClipboard(NULL)) return;

    EmptyClipboard();

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size_needed * sizeof(wchar_t));

    if (hMem) {
        wchar_t* pData = (wchar_t*)GlobalLock(hMem);
        MultiByteToWideChar(CP_UTF8, 0, text, -1, pData, size_needed);
        GlobalUnlock(hMem);

        SetClipboardData(CF_UNICODETEXT, hMem);
    }

    CloseClipboard();
}

xr_string XrUIManager::ConvertCP1251ToUTF8(const char* str)
{
    if (!str || !str[0]) return "";
    int wlen = MultiByteToWideChar(1251, 0, str, -1, NULL, 0);
    if (wlen <= 0) return "";
    xr_vector<wchar_t> wstr(wlen);
    MultiByteToWideChar(1251, 0, str, -1, wstr.data(), wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), -1, NULL, 0, NULL, NULL);
    if (ulen <= 0) return "";
    xr_vector<char> ustr(ulen);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), -1, ustr.data(), ulen, NULL, NULL);
    return xr_string(ustr.data());
}

xr_string XrUIManager::ConvertUTF8ToCP1251(const char* str)
{
    if (!str || !str[0]) return "";
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str, -1, NULL, 0);
    if (wlen <= 0) return "";
    xr_vector<wchar_t> wstr(wlen);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str, -1, wstr.data(), wlen);
    int len = WideCharToMultiByte(1251, WC_NO_BEST_FIT_CHARS, wstr.data(), -1, NULL, 0, NULL, NULL);
    if (len <= 0) return "";
    xr_vector<char> result(len);
    WideCharToMultiByte(1251, WC_NO_BEST_FIT_CHARS, wstr.data(), -1, result.data(), len, NULL, NULL);
    return xr_string(result.data());
}

XrUIManager::XrUIManager()
{}

XrUIManager::~XrUIManager()
{}

inline void Style()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    int is3D = 0;

    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.12f, 0.12f, 0.12f, 0.71f);
    colors[ImGuiCol_BorderShadow] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.42f, 0.42f, 0.42f, 0.54f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.42f, 0.42f, 0.42f, 0.40f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.67f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.17f, 0.17f, 0.17f, 0.90f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.335f, 0.335f, 0.335f, 1.000f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.24f, 0.24f, 0.24f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.52f, 0.52f, 0.52f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.76f, 0.76f, 0.76f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.65f, 0.65f, 0.65f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.52f, 0.52f, 0.52f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.64f, 0.64f, 0.64f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.54f, 0.54f, 0.54f, 0.35f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.52f, 0.52f, 0.52f, 0.59f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.76f, 0.76f, 0.76f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.47f, 0.47f, 0.47f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.76f, 0.76f, 0.76f, 0.77f);
    colors[ImGuiCol_Separator] = ImVec4(0.000f, 0.000f, 0.000f, 0.137f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.700f, 0.671f, 0.600f, 0.290f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.702f, 0.671f, 0.600f, 0.674f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.73f, 0.73f, 0.73f, 0.35f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);

    style.PopupRounding = 3;

    style.WindowPadding = ImVec2(4, 4);
    style.FramePadding = ImVec2(6, 4);
    style.ItemSpacing = ImVec2(6, 2);

    style.ScrollbarSize = 18;

    style.WindowBorderSize = 1;
    style.ChildBorderSize = 1;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = is3D;

    style.WindowRounding = 3;
    style.ChildRounding = 3;
    style.FrameRounding = 3;
    style.ScrollbarRounding = 2;
    style.GrabRounding = 3;

#ifdef IMGUI_HAS_DOCK 
    style.TabBorderSize = is3D;
    style.TabRounding = 3;

    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
    colors[ImGuiCol_DockingPreview] = ImVec4(0.85f, 0.85f, 0.85f, 0.28f);

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
#endif
}

void XrUIManager::Initialize(HWND hWnd, IDirect3DDevice9* device, const char* ini_path)
{
    editorWindow = hWnd;
    effectsDirty = true;
    editorPresence.Initialize(true);

    IMGUI_CHECKVERSION();
    aboutDevice = device;
    aboutLogoAttempted = false;
    aboutRequested = false;
    uiScale = requestedUIScale = 1.f;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    io.GetClipboardTextFn = GetClipboardTextFn_Custom;
    io.SetClipboardTextFn = SetClipboardTextFn_Custom;

    xr_strcpy(m_name_ini, ini_path);
    io.IniFilename = m_name_ini;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    // LauncherAssets embeds Launcher/assets/gui/Font.otf. Resolve beside the
    // executable so changing the SDK working directory cannot lose the font.
    wchar_t assetsPath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, assetsPath, MAX_PATH);
    wchar_t* slash = wcsrchr(assetsPath, L'\\');
    if (slash)
    {
        slash[1] = 0;
        wcscat_s(assetsPath, L"LauncherAssets.dll");
        editorFontAssets = LoadLibraryExW(assetsPath, nullptr, LOAD_LIBRARY_AS_DATAFILE);
    }
    ImFontConfig fontConfig;
    fontConfig.OversampleH = 3;
    fontConfig.OversampleV = 2;
    ImFont* font = nullptr;
    if (GetFileAttributesA("Launcher\\assets\\gui\\Font.otf") != INVALID_FILE_ATTRIBUTES)
        font = io.Fonts->AddFontFromFileTTF("Launcher\\assets\\gui\\Font.otf", 16.f,
            &fontConfig, io.Fonts->GetGlyphRangesCyrillic());
    if (!font && editorFontAssets)
    {
        HRSRC resource = FindResourceW(editorFontAssets, MAKEINTRESOURCEW(IDR_LAUNCHER_FONT), MAKEINTRESOURCEW(10));
        HGLOBAL data = resource ? LoadResource(editorFontAssets, resource) : nullptr;
        void* bytes = data ? LockResource(data) : nullptr;
        const DWORD size = resource ? SizeofResource(editorFontAssets, resource) : 0;
        if (bytes && size)
        {
            fontConfig.FontDataOwnedByAtlas = false;
            font = io.Fonts->AddFontFromMemoryTTF(bytes, int(size), 16.f, &fontConfig,
                io.Fonts->GetGlyphRangesCyrillic());
        }
    }
    fontConfig.FontDataOwnedByAtlas = true;
    // Merge missing characters only; the supplied OTF remains the primary face.
    char windowsPath[MAX_PATH] = {};
    GetWindowsDirectoryA(windowsPath, MAX_PATH);
    strcat_s(windowsPath, "\\Fonts\\segoeui.ttf");
    if (GetFileAttributesA(windowsPath) != INVALID_FILE_ATTRIBUTES)
    {
        fontConfig.MergeMode = font != nullptr;
        io.Fonts->AddFontFromFileTTF(windowsPath, 16.f, &fontConfig, io.Fonts->GetGlyphRangesCyrillic());
    }
    if (!io.Fonts->Fonts.empty()) io.FontDefault = io.Fonts->Fonts[0];

    ImGui::StyleColorsClassic();

#ifndef USE_OLD_STYLE
    ImGui::Spectrum::LoadFont();
    Style();
#endif

    ImGui::GetStyle().ItemSpacing = ImVec2(3, 3);
    ImGui::GetStyle().FramePadding = ImVec2(3, 1);
    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX9_Init(device);
    SDKDialogs::Initialize(hWnd);
    customFrame = true;
    const LONG_PTR frameStyle = GetWindowLongPtrW(hWnd, GWL_STYLE);
    SetWindowLongPtrW(hWnd, GWL_STYLE, frameStyle & ~WS_CAPTION);
    SetWindowPos(hWnd, nullptr, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void XrUIManager::Destroy()
{
    SDKDialogs::Shutdown();
    windowVisuals.clear();
    if (aboutLogo) aboutLogo->Release();
    aboutLogo = nullptr;
    aboutDevice = nullptr;
    effectsDirty = true;
    UpdateBackdrop();
    editorWindow = nullptr;
    customFrame = false;
    editorPresence.Shutdown();
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (editorFontAssets) FreeLibrary(editorFontAssets);
    editorFontAssets = nullptr;
}

void XrUIManager::ResetBegin()
{
    windowVisuals.clear();
    ImGui_ImplDX9_InvalidateDeviceObjects();
}

void XrUIManager::ResetEnd()
{
    ImGui_ImplDX9_CreateDeviceObjects();
}

void XrUIManager::OnDrawUI()
{}

void XrUIManager::ApplyShortCut(DWORD Key)
{
    if ((ImGui::GetIO().WantTextInput))
        return;
    bool IsFail = true;
    if (Key >= 'A' && Key <= 'Z')
    {
        IsFail = false;
    }
    else if (Key >= '0' && Key <= '9')
    {
        IsFail = false;
    }
    else
    {
        switch (Key)
        {
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_NUMPAD0:
        case VK_NUMPAD1:
        case VK_NUMPAD2:
        case VK_NUMPAD3:
        case VK_NUMPAD4:
        case VK_NUMPAD5:
        case VK_NUMPAD6:
        case VK_NUMPAD7:
        case VK_NUMPAD8:
        case VK_NUMPAD9:
        case VK_F1:
        case VK_F2:
        case VK_F3:
        case VK_F4:
        case VK_F5:
        case VK_F6:
        case VK_F7:
        case VK_F8:
        case VK_F9:
        case VK_F10:
        case VK_F11:
        case VK_F12:
        case VK_DELETE:
        case VK_ADD:
        case VK_SUBTRACT:
        case VK_MULTIPLY:
        case VK_DIVIDE:
        case VK_OEM_PLUS:
        case VK_OEM_MINUS:
        case VK_OEM_1:
        case VK_OEM_COMMA:
        case VK_OEM_PERIOD:
        case VK_OEM_2:
        case VK_OEM_4:
        case VK_OEM_5:
        case VK_OEM_6:
        case VK_OEM_7:
        case VK_SPACE:
        case VK_CANCEL:
        case VK_RETURN:
            IsFail = false;
            break;
        default:
            break;
        }
    }
    if (IsFail)
        return;

    int ShiftState = ssNone;

    if (ImGui::GetIO().KeyShift)
        ShiftState |= ssShift;
    if (ImGui::GetIO().KeyCtrl)
        ShiftState |= ssCtrl;
    if (ImGui::GetIO().KeyAlt)
        ShiftState |= ssAlt;

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        ShiftState |= ssLeft;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
        ShiftState |= ssRight;
    ApplyShortCut(Key, ShiftState);
}

void XrUIManager::Push(XrUI* ui, bool need_deleted)
{
    m_UIArray.push_back(ui);
    ui->Flags.set(!need_deleted, XrUI::F_NoDelete);
}

void XrUIManager::UpdateDiscordPresence()
{
    const xr_string environment = DiscordEnvironment();
    const xr_string document = DiscordDocument();
    editorPresence.Tick(environment.c_str(), FarkashedDiscord::AnsiToUtf8(document.c_str()));
}

void XrUIManager::ShowAbout() { aboutRequested = true; }

void XrUIManager::SetUIScale(float scale)
{
    requestedUIScale = std::isfinite(scale) ? (scale < .75f ? .75f : scale > 2.f ? 2.f : scale) : 1.f;
}
float XrUIManager::GetUIScale() { return uiScale; }
float XrUIManager::GetToolBarHeight() { return UIToolBarSize*uiScale; }
float XrUIManager::GetTitleBarHeight() { return UITitleBarSize*uiScale; }

void XrUIManager::SetWindowEffects(const float*, float, float, bool, bool, float)
{
    // Legacy theme files remain readable; window effects have been removed.
}

bool XrUIManager::IsBackgroundBlurAvailable() { return false; }

bool XrUIManager::HandleWindowFrame(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result)
{
    if (hwnd == editorWindow && SDKDialogs::HandleMessage(message, lParam, result)) return true;
    if (!customFrame || hwnd != editorWindow || IsIconic(hwnd)) return false;
    if (message == WM_GETMINMAXINFO)
    {
        result = DefWindowProcW(hwnd, message, wParam, lParam);
        MINMAXINFO* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        if (limits->ptMinTrackSize.x < 360) limits->ptMinTrackSize.x = 360;
        if (limits->ptMinTrackSize.y < 240) limits->ptMinTrackSize.y = 240;
        return true;
    }
    if (message == WM_NCCALCSIZE)
    {
        // Remove native decorations; maximized windows stay within the monitor work area.
        if (wParam && IsZoomed(hwnd))
        {
            MONITORINFO monitor = { sizeof(MONITORINFO) };
            if (GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST), &monitor))
                reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0] = monitor.rcWork;
        }
        result = 0;
        return true;
    }
    if (message != WM_NCHITTEST) return false;
    POINT point = { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
    ScreenToClient(hwnd,&point);
    RECT client = {};
    GetClientRect(hwnd,&client);
    const int edge = 6;
    if (!IsZoomed(hwnd))
    {
        const bool left = point.x < edge, right = point.x >= client.right-edge;
        const bool top = point.y < edge, bottom = point.y >= client.bottom-edge;
        if (top || bottom || left || right)
        {
            result = top ? (left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP) :
                bottom ? (left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM) : left ? HTLEFT : HTRIGHT;
            return true;
        }
    }
    // Keep the controls in the client area so ImGui receives their clicks.
    result = point.y < GetTitleBarHeight() && point.x < client.right-100*uiScale ? HTCAPTION : HTCLIENT;
    return true;
}

void XrUIManager::Draw()
{
    SDKDialogs::Pump();
    UpdateBackdrop();
    if (uiScale != requestedUIScale) windowVisuals.clear();
    ApplyUIScale();
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        DrawWindowTitleBar();
        m_MenuBarHeight = GetTitleBarHeight() + ImGui::GetFrameHeight();
        const float top = m_MenuBarHeight + GetToolBarHeight();
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + top));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, (viewport->Size.y > top ? viewport->Size.y - top : 1.f)));
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("Master DockSpace", NULL, window_flags | ImGuiWindowFlags_NoBackground);
        ImGuiID dockMain = ImGui::GetID("MyDockspace");

        // Docked panels share the opaque workspace background.
        ImVec4 dockBackground = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        dockBackground.w = 1.f;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, dockBackground);
        ImGui::DockSpace(dockMain, ImVec2(0,0), ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::PopStyleColor();
        ImGui::End();
        ImGui::PopStyleVar(3);
    }

    // Draw() can Push() another window and reallocate the vector.
    // New windows are drawn next frame; do not retain iterators across callbacks.
    const size_t window_count = m_UIArray.size();
    for (size_t i = 0; i < window_count; ++i)
    {
        XrUI* ui = m_UIArray[i];
        ui->Draw();
    }

    OnDrawUI();
    DrawAbout();
    UpdateCaptionTheme();
    // Docked ImGui windows normally suppress their border and rounding.
    // Add the thin panel contour after contents, keeping docking hit regions.
    const ImGuiStyle& panelStyle = ImGui::GetStyle();
    for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
    {
        if (!window->Active || window->Hidden || !window->DockIsActive ||
            !window->DockTabIsVisible || (window->Flags & ImGuiWindowFlags_ChildWindow))
            continue;
        const ImVec2 min(window->Pos.x + .5f, window->Pos.y + .5f);
        const ImVec2 max(window->Pos.x + window->Size.x - .5f,
            window->Pos.y + window->Size.y - .5f);
        window->DrawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border),
            panelStyle.ChildRounding, 0, panelStyle.ChildBorderSize);
    }
    ImGui::Render();
    RenderAnimatedWindows();

    for (size_t i = m_UIArray.size(); i > 0; i--)
    {
        if (m_UIArray[i - 1]->IsClosed())
        {
            if (!m_UIArray[i - 1]->Flags.test(XrUI::F_NoDelete))
            {
                xr_delete(m_UIArray[i - 1]);
            }
            m_UIArray.erase(m_UIArray.begin() + (i - 1));
            i = m_UIArray.size();
            if (i == 0)
                return;
        }
    }
}

IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT XrUIManager::WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT dialogResult = 0;
    if (SDKDialogs::HandleMessage(msg, lParam, dialogResult)) return dialogResult;
    if (msg == WM_DWMCOMPOSITIONCHANGED) effectsDirty = true;
    switch (msg)
    {
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        switch (wParam)
        {
        case VK_MENU:
        case VK_CONTROL:
        case VK_SHIFT:
            break;
        default:
            ApplyShortCut(wParam);
            break;
        }
        break;
    case WM_CHAR:
        if (wParam > 0 && wParam < 0x10000)
        {
            wchar_t wch = 0;
            if (wParam >= 0x80 && wParam <= 0xFF)
            {
                char ch = (char)wParam;
                if (MultiByteToWideChar(1251, 0, &ch, 1, &wch, 1) > 0)
                {
                    ImGui::GetIO().AddInputCharacter(wch);
                    return 0;
                }
            }
            ImGui::GetIO().AddInputCharacterUTF16((unsigned short)wParam);
        }
        return 0;
    default:
        break;
    }
    return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
}
