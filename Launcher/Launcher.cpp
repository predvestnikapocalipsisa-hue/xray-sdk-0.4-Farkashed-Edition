#include "Platform.h"
#include "EditorHost.h"
#include "DiscordPresence.h"
#include "../LauncherAssets/ResourceIds.h"
#include "RecentFiles.h"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include <array>
#include <cfloat>
#include <cmath>
#include <cwctype>
#include <functional>
#include <unordered_map>
#include <utility>

namespace
{
    struct Editor
    {
        const wchar_t* id;
        const wchar_t* executable;
        unsigned icon;
        const char* label;
        const char* description;
    };

    const std::array<Editor, 4> Editors = {{
        { L"actor", L"ActorEditor.dll", IDR_LAUNCHER_ACTOR, "ACTOR EDITOR", "Objects, actors and animations" },
        { L"level", L"LevelEditor.dll", IDR_LAUNCHER_LEVEL, "LEVEL EDITOR", "Levels and scenes" },
        { L"particle", L"ParticleEditor.dll", IDR_LAUNCHER_PARTICLE, "PARTICLE EDITOR", "Particle effects and libraries" },
        { L"shader", L"ShaderEditor.dll", IDR_LAUNCHER_SHADER, "SHADER EDITOR", "Shaders and SDK material libraries" }
    }};

    std::string Utf8(const std::wstring& value)
    {
        if (value.empty())
            return {};
        const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), int(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), int(value.size()), &result[0], length, nullptr, nullptr);
        return result;
    }

    std::wstring FileName(const std::wstring& path)
    {
        return path.substr(path.find_last_of(L"\\/") + 1);
    }

    std::wstring EditorForFile(const std::wstring& path)
    {
        std::wstring name = FileName(path);
        std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return wchar_t(towlower(c)); });
        const auto dot = name.find_last_of(L'.');
        const std::wstring extension = dot == std::wstring::npos ? L"" : name.substr(dot);
        if (extension == L".level")
            return L"level";
        if (extension == L".object")
            return L"actor";
        // These .xr files belong to other SDK subsystems, not to the particle loader.
        if (extension == L".xr" && name != L"shaders.xr" && name != L"shaders_xrlc.xr" &&
            name != L"gamemtl.xr" && name != L"lanims.xr" && name != L"senvironment.xr")
            return L"particle";
        return {};
    }

    const Editor* FindEditor(const std::wstring& id)
    {
        for (const auto& editor : Editors)
            if (id == editor.id)
                return &editor;
        return nullptr;
    }

    void ImportLegacyHistory(const std::wstring& directory)
    {
        // Import the existing default SDK preference files once per installation.
        unsigned hash = 2166136261u;
        for (wchar_t c : directory)
            hash = (hash ^ unsigned(towlower(c))) * 16777619u;
        const std::wstring key = L"ImportedLegacy_" + std::to_wstring(hash);
        if (FarkashedLauncher::Setting(key.c_str()) == L"1")
            return;
        bool found = false;
        for (const wchar_t* editor : { L"level", L"actor" })
        {
            for (const auto& parent : { directory + L"\\editor", directory })
            {
                const std::wstring ini = parent + L"\\" + editor + L".ini";
                WIN32_FILE_ATTRIBUTE_DATA attributes;
                if (!GetFileAttributesExW(ini.c_str(), GetFileExInfoStandard, &attributes))
                    continue;
                found = true;
                const ULONGLONG modified = (ULONGLONG(attributes.ftLastWriteTime.dwHighDateTime) << 32) |
                    attributes.ftLastWriteTime.dwLowDateTime;
                for (int index = 24; index >= 0; --index)
                {
                    const std::wstring recentKey = L"recent_files_" + std::to_wstring(index);
                    std::wstring path = FarkashedLauncher::ReadValue(ini, L"editor_prefs", recentKey.c_str());
                    if (path.empty())
                        continue;
                    if (path.front() != L'\\' && path.front() != L'/' && (path.size() < 2 || path[1] != L':'))
                        path = directory + L"\\" + path;
                    FarkashedLauncher::Remember(path, editor, directory,
                        modified > ULONGLONG(index) ? modified - index : 1);
                }
            }
        }
        if (found)
            FarkashedLauncher::SetSetting(key.c_str(), L"1");
    }

    struct ButtonAnimation
    {
        float hover = 0.f;
        float pressed = 0.f;
    };

    class LauncherUI
    {
        LauncherPlatform& platform;
        std::wstring sdkDirectory;
        std::array<std::unique_ptr<Texture>, 4> icons;
        std::unique_ptr<Texture> logo;
        std::unique_ptr<Texture> closeIcon;
        std::unique_ptr<Texture> minimizeIcon;
        std::vector<FarkashedLauncher::RecentFile> recentFiles;
        std::vector<bool> missingFiles;
        std::array<bool, 4> available = {};
        std::unordered_map<ImGuiID, ButtonAnimation> animations;
        std::function<void()> pendingAction;
        std::string error;
        std::string status;
        bool showError = false;
        bool showSettings = false;
        bool useBlur = true;
        bool minimizeOnLaunch = false;
        bool reduceMotion = false;
        bool historyDirty = true;
        bool assetWarning = false;
        float opacity = 0.f;
        float darkening = 0.76f;
        double nextRefresh = 0.;
        double statusExpires = 0.;

        ImVec2 Position(float x, float y) const { return ImVec2(x * platform.dpiScale, y * platform.dpiScale); }

        ImU32 Color(float r, float g, float b, float a = 1.f) const
        {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a * opacity));
        }

        float Approach(float value, float target, float speed) const
        {
            if (reduceMotion)
                return target;
            return value + (target - value) * (1.f - std::exp(-speed * (std::min)(ImGui::GetIO().DeltaTime, 0.05f)));
        }

        void Image(ImDrawList* draw, const Texture* texture, const ImVec2& topLeft,
            const ImVec2& bottomRight, float alpha = 1.f) const
        {
            if (texture)
                draw->AddImage(reinterpret_cast<ImTextureID>(texture->handle), topLeft, bottomRight,
                    ImVec2(0, 0), ImVec2(1, 1), Color(1, 1, 1, alpha));
        }

        void Text(ImDrawList* draw, float x, float y, const std::string& value,
            float alpha = 1.f, float size = 15.f, float maximumWidth = 1000.f) const
        {
            const float fontSize = size * platform.dpiScale;
            std::string text = value;
            const float limit = maximumWidth * platform.dpiScale;
            ImFont* font = ImGui::GetFont();
            if (font->CalcTextSizeA(fontSize, FLT_MAX, 0, text.c_str()).x > limit)
            {
                while (!text.empty() && font->CalcTextSizeA(fontSize, FLT_MAX, 0, (text + "...").c_str()).x > limit)
                {
                    size_t last = text.size() - 1;
                    while (last && (static_cast<unsigned char>(text[last]) & 0xc0) == 0x80)
                        --last;
                    text.resize(last);
                }
                text += "...";
            }
            draw->AddText(font, fontSize, Position(x, y), Color(0.94f, 0.94f, 0.95f, alpha), text.c_str());
        }

        bool AnimatedButton(const char* id, float x, float y, float width, float height,
            const char* label, const Texture* icon = nullptr, bool enabled = true, bool compact = false)
        {
            ImGui::SetCursorPos(Position(x, y));
            ImGui::BeginDisabled(!enabled);
            const bool clicked = ImGui::InvisibleButton(id, Position(width, height));
            const bool hovered = !platform.moving && ImGui::IsItemHovered();
            const bool focused = ImGui::GetIO().NavVisible && ImGui::IsItemFocused();
            const bool pressed = ImGui::IsItemActive();
            auto& animation = animations[ImGui::GetID(id)];
            animation.hover = Approach(animation.hover, hovered && enabled ? 1.f : 0.f, 15.f);
            animation.pressed = Approach(animation.pressed, pressed && enabled ? 1.f : 0.f, 24.f);
            const float shift = animation.pressed * 1.5f;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(Position(x, y), Position(x + width, y + height),
                Color(0.9f, 0.1f, 0.12f, animation.hover * 0.14f + animation.pressed * 0.17f), 5.f * platform.dpiScale);
            if ((hovered || focused) && enabled)
                draw->AddRect(Position(x, y), Position(x + width, y + height),
                    Color(0.95f, 0.25f, 0.27f, focused ? 0.65f : animation.hover * 0.35f), 5.f * platform.dpiScale);
            const float alpha = enabled ? 0.82f + animation.hover * 0.18f : 0.32f;
            if (icon)
            {
                const float centerX = compact ? x + width * 0.5f : x + 20.5f;
                const float centerY = y + height * 0.5f;
                // ResizeImage already filters at the physical drawing size. Keep a
                // 1:1 texel/pixel mapping instead of filtering again at half pixels.
                const ImVec2 center = Position(centerX + shift, centerY + shift);
                const ImVec2 topLeft(std::round(center.x - float(icon->width) * 0.5f),
                    std::round(center.y - float(icon->height) * 0.5f));
                Image(draw, icon, topLeft,
                    ImVec2(topLeft.x + float(icon->width), topLeft.y + float(icon->height)), alpha);
            }
            if (label && *label)
            {
                ImFont* font = ImGui::GetFont();
                const float fontSize = 15.f * platform.dpiScale;
                const float scale = fontSize / font->FontSize;
                float top = FLT_MAX, bottom = -FLT_MAX;
                // Launcher button captions are ASCII. Center their visible glyphs,
                // not the font's line box (which includes ascender/descender padding).
                for (const unsigned char* c = reinterpret_cast<const unsigned char*>(label); *c; ++c)
                    if (const ImFontGlyph* glyph = font->FindGlyph(*c))
                        if (glyph->Visible)
                        {
                            top = (std::min)(top, glyph->Y0);
                            bottom = (std::max)(bottom, glyph->Y1);
                        }
                const float textWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0, label).x / platform.dpiScale;
                const float textX = icon ? x + 42.f : x + (width - textWidth) * 0.5f;
                const float textY = top <= bottom ?
                    y + height * 0.5f - (top + bottom) * scale / (2.f * platform.dpiScale) :
                    y + (height - 15.f) * 0.5f;
                Text(draw, textX + shift, textY + shift, label, alpha, 15.f,
                    width - (icon ? 48.f : 16.f));
            }
            ImGui::EndDisabled();
            return clicked && enabled;
        }

        void RefreshHistory()
        {
            recentFiles = FarkashedLauncher::ReadHistory();
            missingFiles.clear();
            for (const auto& entry : recentFiles)
                missingFiles.push_back(!FarkashedLauncher::FileExists(entry.path));
            for (size_t i = 0; i < Editors.size(); ++i)
                available[i] = FarkashedLauncher::FileExists(sdkDirectory + L"\\" + Editors[i].executable);
            nextRefresh = ImGui::GetTime() + 2.;
            historyDirty = false;
        }

        void Launch(const std::wstring& editorId, const std::wstring& file, const std::wstring& directory)
        {
            const Editor* editor = FindEditor(editorId);
            if (!editor)
                return;
            std::wstring launchError;
            if (!platform.StartEditor(directory + L"\\" + editor->executable, file, launchError))
            {
                error = Utf8(launchError);
                showError = true;
                return;
            }
            status = std::string("Starting ") + editor->label + "...";
            statusExpires = ImGui::GetTime() + 5.;
            if (minimizeOnLaunch)
                ShowWindow(platform.window, SW_MINIMIZE);
            // Editors add the file to history only after loading it successfully.
            historyDirty = true;
        }

        void DrawRecentFiles()
        {
            ImDrawList* rootDraw = ImGui::GetWindowDrawList();
            Text(rootDraw, 304.f, 242.f, "Recently opened", 0.65f, 14.f);
            ImGui::SetCursorPos(Position(300.f, 263.f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::BeginChild("RecentFiles", Position(247.f, 126.f), false,
                ImGuiWindowFlags_NoBackground);
            ImGui::PopStyleVar();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            if (recentFiles.empty())
            {
                ImGui::TextDisabled("No recent projects yet.");
                ImGui::TextDisabled("Open a file or drop it here.");
            }
            for (size_t index = 0; index < recentFiles.size(); ++index)
            {
                const auto& entry = recentFiles[index];
                const std::string id = Utf8(entry.path + L"|" + entry.sdkDirectory + L"|" + entry.editor);
                ImGui::PushID(id.c_str());
                const ImVec2 start = ImGui::GetCursorScreenPos();
                const bool clicked = ImGui::InvisibleButton("Recent", Position(229.f, 43.f));
                const bool hovered = !platform.moving && ImGui::IsItemHovered();
                auto& animation = animations[ImGui::GetID("Recent")];
                animation.hover = Approach(animation.hover, hovered ? 1.f : 0.f, 15.f);
                animation.pressed = Approach(animation.pressed, ImGui::IsItemActive() ? 1.f : 0.f, 24.f);
                const ImVec2 end(start.x + 229.f * platform.dpiScale, start.y + 43.f * platform.dpiScale);
                draw->AddRectFilled(start, end, Color(0.9f, 0.1f, 0.12f,
                    animation.hover * 0.14f + animation.pressed * 0.12f), 4.f * platform.dpiScale);
                const float shift = animation.pressed * platform.dpiScale;
                const ImVec2 name(start.x + 7.f * platform.dpiScale + shift, start.y + 4.f * platform.dpiScale + shift);
                const ImVec2 folder(start.x + 7.f * platform.dpiScale + shift, start.y + 23.f * platform.dpiScale + shift);
                const ImVec4 clip(start.x, start.y, end.x - 6.f * platform.dpiScale, end.y);
                const std::string filename = (missingFiles[index] ? "! " : "") + Utf8(FileName(entry.path));
                draw->AddText(ImGui::GetFont(), 14.f * platform.dpiScale, name,
                    missingFiles[index] ? Color(1.f, 0.55f, 0.4f, 0.85f) : Color(0.95f, 0.95f, 0.97f),
                    filename.c_str(), nullptr, 0.f, &clip);
                const std::string path = Utf8(entry.path);
                draw->AddText(ImGui::GetFont(), 11.f * platform.dpiScale, folder,
                    Color(0.8f, 0.8f, 0.84f, 0.55f), path.c_str(), nullptr, 0.f, &clip);
                if (hovered)
                {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(path.c_str());
                    ImGui::Text("Editor: %s", Utf8(entry.editor).c_str());
                    ImGui::Text("SDK: %s", Utf8(entry.sdkDirectory).c_str());
                    if (missingFiles[index])
                        ImGui::TextUnformatted("File is missing. Remove it from history or restore it.");
                    ImGui::EndTooltip();
                }
                if (clicked)
                {
                    const auto copy = entry;
                    pendingAction = [this, copy] { Launch(copy.editor, copy.path, copy.sdkDirectory); };
                }
                if (ImGui::BeginPopupContextItem("RecentActions"))
                {
                    if (ImGui::MenuItem("Show in Explorer", nullptr, false, !missingFiles[index]))
                    {
                        const auto pathCopy = entry.path;
                        pendingAction = [this, pathCopy] { platform.RevealFile(pathCopy); };
                    }
                    if (ImGui::MenuItem("Remove from recent files"))
                    {
                        const auto copy = entry;
                        pendingAction = [this, copy]
                        {
                            if (!FarkashedLauncher::Forget(copy))
                            {
                                error = "Could not update recent files. Try again.";
                                showError = true;
                            }
                            historyDirty = true;
                        };
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }

        void DrawSettings()
        {
            if (showSettings)
            {
                ImGui::OpenPopup("Launcher settings");
                showSettings = false;
            }
            ImGui::SetNextWindowPos(Position(288.f, 218.f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(Position(450.f, 0.f), ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal("Launcher settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("SDK editor folder");
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 412.f * platform.dpiScale);
                ImGui::TextDisabled("%s", Utf8(sdkDirectory).c_str());
                ImGui::PopTextWrapPos();
                if (ImGui::Button("Choose folder..."))
                    pendingAction = [this]
                    {
                        const std::wstring selected = platform.PickSDKDirectory();
                        if (selected.empty())
                            return;
                        bool containsEditor = false;
                        for (const auto& editor : Editors)
                            containsEditor = containsEditor || FarkashedLauncher::FileExists(selected + L"\\" + editor.executable);
                        if (!containsEditor || !FarkashedLauncher::FileExists(selected + L"\\Launcher.exe") ||
                            !FarkashedLauncher::FileExists(selected + L"\\SDKRuntime.dll"))
                        {
                            error = "Choose an SDK folder containing Launcher.exe, SDKRuntime.dll and editor modules.";
                            showError = true;
                            return;
                        }
                        sdkDirectory = selected;
                        FarkashedLauncher::SetSetting(L"SDKDirectory", selected);
                        ImportLegacyHistory(selected);
                        historyDirty = true;
                    };
                ImGui::Separator();
                if (ImGui::Checkbox("Procedural background blur", &useBlur))
                {
                    platform.SetBlur(useBlur);
                    FarkashedLauncher::SetSetting(L"UseBlur", useBlur ? L"1" : L"0");
                }
                ImGui::TextDisabled("Blur updates live as the window moves.");
                if (useBlur && !platform.nativeBlur)
                    ImGui::TextDisabled("Background blur is unavailable on this Windows setup.");
                ImGui::SliderFloat("Background shade", &darkening, 0.45f, 0.95f, "%.2f");
                if (ImGui::IsItemDeactivatedAfterEdit())
                    FarkashedLauncher::SetSetting(L"BackgroundShade", std::to_wstring(int(darkening * 100.f)));
                if (ImGui::Checkbox("Minimize after starting an editor", &minimizeOnLaunch))
                    FarkashedLauncher::SetSetting(L"MinimizeOnLaunch", minimizeOnLaunch ? L"1" : L"0");
                if (ImGui::Checkbox("Reduce animation", &reduceMotion))
                    FarkashedLauncher::SetSetting(L"ReduceMotion", reduceMotion ? L"1" : L"0");
                ImGui::Separator();
                if (ImGui::Button("Done", Position(100.f, 0.f)))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }

    public:
        explicit LauncherUI(LauncherPlatform& platform) : platform(platform)
        {
            sdkDirectory = FarkashedLauncher::Setting(L"SDKDirectory");
            if (sdkDirectory.empty())
                sdkDirectory = FarkashedLauncher::ModuleDirectory();
            useBlur = FarkashedLauncher::Setting(L"UseBlur") != L"0";
            minimizeOnLaunch = FarkashedLauncher::Setting(L"MinimizeOnLaunch") == L"1";
            reduceMotion = FarkashedLauncher::Setting(L"ReduceMotion") == L"1";
            const std::wstring shade = FarkashedLauncher::Setting(L"BackgroundShade");
            if (!shade.empty())
                darkening = (std::max)(0.45f, (std::min)(0.95f, float(_wtoi(shade.c_str())) / 100.f));
            platform.SetBlur(useBlur);
            ImportLegacyHistory(sdkDirectory);
        }

        void LoadFonts()
        {
            ImGui_ImplDX9_InvalidateDeviceObjects();
            ImGuiIO& io = ImGui::GetIO();
            io.Fonts->Clear();
            ImFontConfig configuration;
            configuration.OversampleH = 2;
            configuration.OversampleV = 2;
            const float size = 16.f * platform.dpiScale;
            ImFont* font = nullptr;
            const LauncherAssetData customFont = platform.Asset(IDR_LAUNCHER_FONT);
            if (customFont.data && customFont.size)
            {
                configuration.FontDataOwnedByAtlas = false;
                font = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(customFont.data),
                    int(customFont.size), size, &configuration, io.Fonts->GetGlyphRangesCyrillic());
                configuration.FontDataOwnedByAtlas = true;
            }
            wchar_t windowsDirectory[MAX_PATH] = {};
            GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
            const std::wstring fallback = std::wstring(windowsDirectory) + L"\\Fonts\\segoeui.ttf";
            if (FarkashedLauncher::FileExists(fallback))
            {
                configuration.MergeMode = font != nullptr;
                font = io.Fonts->AddFontFromFileTTF(Utf8(fallback).c_str(), size, &configuration, io.Fonts->GetGlyphRangesCyrillic());
            }
            if (io.Fonts->Fonts.empty())
            {
                configuration.MergeMode = false;
                configuration.SizePixels = size;
                io.Fonts->AddFontDefault(&configuration);
            }
            io.Fonts->Build();
            ImGui::StyleColorsDark();
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 7.f;
            style.FrameRounding = 4.f;
            style.PopupRounding = 6.f;
            style.WindowPadding = ImVec2(12.f, 12.f);
            style.ItemSpacing = ImVec2(8.f, 6.f);
            style.ScrollbarSize = 6.f;
            style.Colors[ImGuiCol_Button] = ImVec4(0.36f, 0.09f, 0.11f, 1.f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.61f, 0.12f, 0.16f, 1.f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.79f, 0.17f, 0.19f, 1.f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(1.f, 0.23f, 0.26f, 1.f);
            style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.82f, 0.18f, 0.22f, 1.f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.61f, 0.12f, 0.16f, 0.6f);
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(1.f, 0.24f, 0.26f, 1.f);
            style.ScaleAllSizes(platform.dpiScale);
            // Decode/crop once per DPI change, then area-filter at the physical
            // drawing size. Icons are not resampled from 256 px by two GPU taps.
            logo = platform.LoadTexture(IDR_LAUNCHER_LOGO, unsigned(std::round(502.f * platform.dpiScale)),
                unsigned(std::round(258.f * platform.dpiScale)));
            closeIcon = platform.LoadTexture(IDR_LAUNCHER_CLOSE, unsigned(std::round(18.f * platform.dpiScale)));
            minimizeIcon = platform.LoadTexture(IDR_LAUNCHER_MINIMIZE, unsigned(std::round(18.f * platform.dpiScale)));
            for (size_t i = 0; i < Editors.size(); ++i)
                icons[i] = platform.LoadTexture(Editors[i].icon,
                    unsigned(std::round(23.f * platform.dpiScale)));
            assetWarning = !logo || !closeIcon || !minimizeIcon;
            for (const auto& icon : icons)
                assetWarning = assetWarning || !icon;

            platform.fontsDirty = false;
        }

        void OpenPath(const std::wstring& path)
        {
            const std::wstring fullPath = FarkashedLauncher::FullPath(path);
            const std::wstring editor = EditorForFile(fullPath);
            if (editor.empty())
            {
                error = "Open a .level scene, .object model or particle .xr library.\n"
                    "Shader and material libraries are managed inside Shader Editor.";
                showError = true;
                return;
            }
            Launch(editor, fullPath, sdkDirectory);
        }

        void ExecutePending()
        {
            auto action = std::move(pendingAction);
            pendingAction = {};
            if (action)
                action();
        }

        void Draw()
        {
            if (!status.empty() && ImGui::GetTime() >= statusExpires)
                status.clear();
            if (!platform.moving && (historyDirty || ImGui::GetTime() >= nextRefresh))
                RefreshHistory();
            opacity = Approach(opacity, platform.closeRequested ? 0.f : 1.f, 18.f);
            if (platform.closeRequested && opacity < 0.015f)
                PostQuitMessage(0);
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
            ImGui::Begin("##Launcher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
            ImGui::PopStyleVar(2);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize,
                Color(0.02f, 0.02f, 0.025f, platform.nativeBlur ? darkening : 1.f));
            // Stroke at the actual client boundary; the PNG contains outer padding.
            // Half a stroke inset keeps every pixel inside the HWND without a gap.
            const float thickness = (std::max)(1.f, 2.f * platform.dpiScale);
            const float halfStroke = thickness * 0.5f;
            draw->AddRect(ImVec2(halfStroke, halfStroke),
                ImVec2(ImGui::GetIO().DisplaySize.x - halfStroke, ImGui::GetIO().DisplaySize.y - halfStroke),
                Color(0.9f, 0.08f, 0.1f), 0.f, 0, thickness);
            Text(draw, 249.f, 16.f, "Welcome", 0.62f, 14.f);
            if (AnimatedButton("Minimize", 493.f, 9.f, 30.f, 29.f, "", minimizeIcon.get(), true, true))
                pendingAction = [this] { ShowWindow(platform.window, SW_MINIMIZE); };
            if (AnimatedButton("Close", 526.f, 9.f, 30.f, 29.f, "", closeIcon.get(), true, true))
                platform.closeRequested = true;
            if (!minimizeIcon)
                Text(draw, 506.f, 12.f, "-", 1.f, 18.f);
            if (!closeIcon)
                Text(draw, 537.f, 12.f, "x", 1.f, 18.f);
            const float entrance = (1.f - opacity) * 5.f;
            if (logo)
            {
                const ImVec2 position = Position(37.f, 35.f + entrance);
                const ImVec2 topLeft(std::round(position.x), std::round(position.y));
                Image(draw, logo.get(), topLeft,
                    ImVec2(topLeft.x + float(logo->width), topLeft.y + float(logo->height)));
            }
            else
                Text(draw, 76.f, 116.f, "FARKASHED EDITION", 1.f, 32.f);
            for (size_t i = 0; i < Editors.size(); ++i)
            {
                const auto& editor = Editors[i];
                if (AnimatedButton(editor.label, 39.f, 261.f + float(i) * 31.f, 211.f, 29.f,
                    editor.label, icons[i].get(), available[i]))
                {
                    const std::wstring id = editor.id;
                    pendingAction = [this, id] { Launch(id, L"", sdkDirectory); };
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(editor.description);
                    if (!available[i])
                        ImGui::Text("Missing: %s", Utf8(sdkDirectory + L"\\" + editor.executable).c_str());
                    ImGui::EndTooltip();
                }
            }
            draw->AddRectFilledMultiColor(Position(275.f, 252.f), Position(277.f, 311.f),
                Color(1, 1, 1, 0.f), Color(1, 1, 1, 0.f), Color(1, 1, 1, 0.45f), Color(1, 1, 1, 0.45f));
            draw->AddRectFilledMultiColor(Position(275.f, 311.f), Position(277.f, 373.f),
                Color(1, 1, 1, 0.45f), Color(1, 1, 1, 0.45f), Color(1, 1, 1, 0.f), Color(1, 1, 1, 0.f));
            DrawRecentFiles();
            if (AnimatedButton("OpenFile", 39.f, 393.f, 116.f, 25.f, "Open file..."))
                pendingAction = [this]
                {
                    const std::wstring file = platform.PickFile();
                    if (!file.empty())
                        OpenPath(file);
                };
            if (AnimatedButton("Settings", 160.f, 393.f, 90.f, 25.f, "Settings"))
                showSettings = true;
            Text(draw, 303.f, 399.f, assetWarning ? "Some artwork is missing" : status.empty() ?
                "Drop a project here to open it" : status, 0.43f, 11.f, 239.f);
            DrawSettings();
            if (showError)
            {
                ImGui::OpenPopup("Cannot open file or editor");
                showError = false;
            }
            ImGui::SetNextWindowPos(Position(288.f, 218.f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(Position(450.f, 0.f), ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal("Cannot open file or editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 414.f * platform.dpiScale);
                ImGui::TextUnformatted(error.c_str());
                ImGui::PopTextWrapPos();
                if (ImGui::Button("OK", Position(100.f, 0.f)))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && ImGui::IsKeyPressed(ImGuiKey_Escape))
                platform.closeRequested = true;
            ImGui::End();
        }
    };
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    int editorExitCode = 0;
    if (RunSDKEditorHost(editorExitCode))
        return editorExitCode;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com))
        return 1;
    LauncherPlatform platform;
    if (!platform.Initialize(instance))
    {
        MessageBoxW(nullptr, L"Could not initialize the launcher. Check DirectX 9 and LauncherAssets.dll beside Launcher.exe.",
            L"X-Ray SDK Launcher", MB_OK | MB_ICONERROR);
        platform.Shutdown();
        CoUninitialize();
        return 1;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplWin32_Init(platform.window);
    ImGui_ImplDX9_Init(platform.device);
    {
        LauncherUI ui(platform);
        ui.LoadFonts();
        ShowWindow(platform.window, showCommand);
        UpdateWindow(platform.window);
        FarkashedDiscord::Presence discord;
        discord.Initialize();
        bool drawing = false;
        const auto drawFrame = [&]() -> bool
        {
            if (drawing || IsIconic(platform.window) || !platform.PrepareFrame())
                return false;
            drawing = true;
            if (platform.fontsDirty)
                ui.LoadFonts();
            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            ui.Draw();
            ImGui::Render();
            const bool rendered = platform.Render();
            drawing = false;
            return rendered;
        };
        platform.redrawDuringMove = drawFrame;
        while (platform.PumpMessages())
        {
            discord.Tick("In Launcher", "");
            if (IsIconic(platform.window))
            {
                if (platform.closeRequested)
                    break;
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
                continue;
            }
            auto dropped = std::move(platform.droppedFiles);
            platform.droppedFiles.clear();
            for (const auto& path : dropped)
                ui.OpenPath(path);
            const bool rendered = drawFrame();
            ui.ExecutePending();
            if (!rendered)
                Sleep(20);
        }
        platform.redrawDuringMove = {};

    } // Release artwork while the D3D device and ImGui context still exist.
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    platform.Shutdown();
    CoUninitialize();
    return 0;
}
