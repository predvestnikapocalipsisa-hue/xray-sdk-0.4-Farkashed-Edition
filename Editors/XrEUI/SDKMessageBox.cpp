#ifdef XR_IMGUI_STANDALONE
#include <windows.h>
#include <d3d9.h>
#include "imgui.h"
#else
#include "stdafx.h"
#endif
#include "SDKMessageBox.h"
#include "SDKProgressBar.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "../../LauncherAssets/ResourceIds.h"
#include <mmsystem.h>
#include <commdlg.h>
#include <cmath>
#pragma comment(lib, "comdlg32.lib")
#include <algorithm>
#include <string>
#include <vector>
#include <mutex>
#include <deque>
#include <cstdint>
#include <memory>
#pragma comment(lib, "winmm.lib")

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{
    constexpr UINT dialogMessage = WM_APP + 0x3A1;
    HWND editorOwner = nullptr;
    bool soundEnabled = true;
    float soundVolume = 1.f;
    std::string customSoundPath; // UTF-8, including paths outside the SDK directory
    std::string soundSettingsError;
    std::wstring WideUTF8(const std::string& text)
    {
        if (text.empty()) return {};
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, nullptr, 0);
        if (!count) return {};
        std::vector<wchar_t> result(count);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, result.data(), count);
        return result.data();
    }
    struct Request
    {
        HWND owner;
        const char* text;
        const char* title;
        UINT flags;
        const char* const* captions;
        bool utf8 = false;
        bool fourChoice = false;
    };
    struct Notification { std::string text, title; UINT flags; };
    std::mutex notificationsMutex;
    std::deque<Notification> notifications;
    struct Dialog
    {
        ImGuiContext* context = nullptr;
        int result = 0;
        int closeResult = IDCANCEL;
        int dragHeight = 28;
        int closeLeft = 460;
        bool closing = false;
        float opacity = 0.0f;
        float closeOpacity = 0.0f;
        ULONGLONG transitionStart = 0;

        void Close(int answer)
        {
            if (closing) return;
            result = answer;
            closing = true;
            closeOpacity = opacity;
            transitionStart = GetTickCount64();
        }
    };
    std::string UTF8(const char* text)
    {
        if (!text || !*text) return {};
        const int length = MultiByteToWideChar(1251, 0, text, -1, nullptr, 0);
        std::vector<wchar_t> wide(length);
        MultiByteToWideChar(1251, 0, text, -1, wide.data(), length);
        const int count = WideCharToMultiByte(CP_UTF8, 0, wide.data(), -1, nullptr, 0, nullptr, nullptr);
        std::vector<char> result(count);
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), -1, result.data(), count, nullptr, nullptr);
        return result.data();
    }
    std::string UTF8(const wchar_t* text)
    {
        if (!text || !*text) return {};
        const int count = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        std::vector<char> result(count);
        WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), count, nullptr, nullptr);
        return result.data();
    }
    LRESULT CALLBACK DialogProc(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        auto* dialog = reinterpret_cast<Dialog*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            dialog = static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dialog));
        }
        if (dialog && message == WM_CLOSE)
        {
            dialog->Close(dialog->closeResult);
            return 0;
        }
        if (message == WM_ERASEBKGND) return 1;
        // Activating an unfocused dialog must preserve the same mouse click.
        // The first click should reach ImGui, rather than only focus the HWND.
        if (dialog && message == WM_MOUSEACTIVATE)
        {
            SetFocus(window);
            return MA_ACTIVATE;
        }
        if (dialog && dialog->context)
        {
            ImGuiContext* saved = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(dialog->context);
            const LRESULT handled = ImGui_ImplWin32_WndProcHandler(window, message, w, l);
            ImGui::SetCurrentContext(saved);
            if (handled) return handled;
        }
        // The ImGui title bar can be dragged like an ordinary separate window.
        if (message == WM_NCHITTEST)
        {
            POINT point{ static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l)) };
            ScreenToClient(window, &point);
            if (dialog && point.y >= 0 && point.y < dialog->dragHeight && point.x < dialog->closeLeft)
                return HTCAPTION;
        }
        return DefWindowProcW(window, message, w, l);
    }
    struct Sound
    {
        HMODULE assets = nullptr;
        wchar_t path[MAX_PATH]{};
        std::wstring alias;
        bool Open(const wchar_t* filename)
        {
            if (!filename || !*filename) return false;
            const std::wstring candidate = L"xr_dialog_" + std::to_wstring(reinterpret_cast<uintptr_t>(this));
            const std::wstring command = L"open \"" + std::wstring(filename) + L"\" type mpegvideo alias " + candidate;
            if (mciSendStringW(command.c_str(), nullptr, 0, nullptr)) return false;
            alias = candidate;
            return true;
        }
        void Prepare()
        {
            wchar_t modulePath[MAX_PATH]{};
            GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
            wchar_t* separator = wcsrchr(modulePath, L'\\');
            if (!separator) return;
            *(separator + 1) = 0;
            const std::wstring dll = std::wstring(modulePath) + L"LauncherAssets.dll";
            assets = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
            // Assets also contain the dialog font, so load them even when muted
            // or using custom audio. Custom audio does not require this DLL.
            if (!soundEnabled) return;
            if (!customSoundPath.empty())
            {
                const auto filename = WideUTF8(customSoundPath);
                Open(filename.c_str());
                return; // Missing/unplayable custom files stay silent.
            }
            if (!assets) return;
            HRSRC resource = FindResourceW(assets, MAKEINTRESOURCEW(IDR_SDK_ERROR_SOUND), MAKEINTRESOURCEW(10));
            HGLOBAL data = resource ? LoadResource(assets, resource) : nullptr;
            const DWORD size = resource ? SizeofResource(assets, resource) : 0;
            const void* bytes = data ? LockResource(data) : nullptr;
            wchar_t directory[MAX_PATH]{};
            if (!bytes || !size || !GetTempPathW(MAX_PATH, directory) ||
                !GetTempFileNameW(directory, L"xrs", 0, path)) return;
            HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                FILE_ATTRIBUTE_TEMPORARY, nullptr);
            if (file == INVALID_HANDLE_VALUE) return;
            DWORD written = 0;
            const bool success = WriteFile(file, bytes, size, &written, nullptr) && written == size;
            CloseHandle(file);
            if (!success) return;
            Open(path);
        }
        void Play()
        {
            if (!alias.empty())
            {
                const std::wstring volume = L"setaudio " + alias + L" volume to " + std::to_wstring(int(soundVolume * 1000.f));
                mciSendStringW(volume.c_str(), nullptr, 0, nullptr);
                if (soundVolume > 0.f) mciSendStringW((L"play " + alias + L" from 0").c_str(), nullptr, 0, nullptr);
            }
        }
        ~Sound()
        {
            if (!alias.empty()) mciSendStringW((L"close " + alias).c_str(), nullptr, 0, nullptr);
            if (path[0]) DeleteFileW(path);
            if (assets) FreeLibrary(assets);
        }
    };
    std::unique_ptr<Sound> soundPreview;

    int ShowDialog(const Request& request)
    {
        ImGuiContext* saved = ImGui::GetCurrentContext();
        // Each popup has its own device/context: calling from inside an editor frame
        // must not end that frame or overwrite its renderer resources.
        ImGuiStyle style;
        float fontSize = 16.0f;
        if (saved) { style = ImGui::GetStyle(); fontSize = (std::max)(14.0f, ImGui::GetFontSize()); }
        else ImGui::StyleColorsDark(&style);
        const float scale = (std::max)(1.0f, fontSize / 16.0f);
        const int width = static_cast<int>(540 * scale), height = static_cast<int>(300 * scale);
        const float buttonHeight = (std::max)(30 * scale, fontSize + 2 * style.FramePadding.y);
        RECT work{};
        MONITORINFO monitor{ sizeof(monitor) };
        GetMonitorInfoW(MonitorFromWindow(request.owner, MONITOR_DEFAULTTONEAREST), &monitor);
        work = monitor.rcWork;
        Dialog dialog;
        dialog.dragHeight = static_cast<int>(fontSize + 2 * style.WindowPadding.y);
        dialog.closeLeft = width - static_cast<int>(60 * scale);
        WNDCLASSW cls{};
        cls.lpfnWndProc = DialogProc;
        // Launcher and SDKRuntime each link ImGui; their window procedures must
        // remain associated with their own module/backend instance.
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&DialogProc), &cls.hInstance);
        cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
        cls.lpszClassName = L"XRaySDKImGuiMessage";
        RegisterClassW(&cls);
        HWND window = CreateWindowExW(WS_EX_LAYERED, cls.lpszClassName, L"X-Ray SDK",
            WS_POPUP, work.left + (work.right-work.left-width)/2,
            work.top + (work.bottom-work.top-height)/2, width, height,
            request.owner, nullptr, cls.hInstance, &dialog);
        IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
        IDirect3DDevice9* device = nullptr;
        D3DPRESENT_PARAMETERS parameters{};
        parameters.Windowed = TRUE;
        parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        parameters.BackBufferFormat = D3DFMT_UNKNOWN;
        parameters.hDeviceWindow = window;
        parameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
        if (!window || !d3d || FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
            window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &parameters, &device)))
        {
            if (device) device->Release();
            if (d3d) d3d->Release();
            if (window) DestroyWindow(window);
            UnregisterClassW(cls.lpszClassName, cls.hInstance);
            if (!request.utf8)
                return MessageBoxA(request.owner, request.text, request.title,
                    request.fourChoice ? (request.flags & ~SDKDialogs::FourChoice) : request.flags);
            const auto wide = [](const char* value)
            {
                const int count = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
                std::vector<wchar_t> result(count);
                MultiByteToWideChar(CP_UTF8, 0, value, -1, result.data(), count);
                return std::wstring(result.data());
            };
            return MessageBoxW(request.owner, wide(request.text).c_str(), wide(request.title).c_str(),
                request.fourChoice ? (request.flags & ~SDKDialogs::FourChoice) : request.flags);
        }
        dialog.context = ImGui::CreateContext();
        ImGui::SetCurrentContext(dialog.context);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::GetStyle() = style;
        ImGui::GetStyle().WindowBorderSize = 0.0f;
        ImGui::GetStyle().ChildBorderSize = 0.0f;
        ImGui::GetStyle().WindowPadding.y = (std::min)(style.WindowPadding.y, 6 * scale);
        Sound sound;
        // The supplied MP3 and the same SDK font come from the portable asset DLL.
        sound.Prepare();
        HRSRC fontResource = sound.assets ? FindResourceW(sound.assets,
            MAKEINTRESOURCEW(IDR_LAUNCHER_FONT), MAKEINTRESOURCEW(10)) : nullptr;
        HGLOBAL fontData = fontResource ? LoadResource(sound.assets, fontResource) : nullptr;
        if (fontData)
        {
            ImFontConfig config;
            config.FontDataOwnedByAtlas = false;
            io.Fonts->AddFontFromMemoryTTF(LockResource(fontData),
                static_cast<int>(SizeofResource(sound.assets, fontResource)), fontSize,
                &config, io.Fonts->GetGlyphRangesCyrillic());
        }
        else io.Fonts->AddFontDefault();
        ImGui_ImplWin32_Init(window);
        ImGui_ImplDX9_Init(device);
        const std::string text = request.utf8 ? request.text : UTF8(request.text);
        const std::string title = request.utf8 ? request.title : UTF8(request.title);
        std::vector<int> buttons;
        if (request.fourChoice)
            buttons = { IDYES, IDNO, SDKDialogs::YesToAll, SDKDialogs::NoToAll };
        else switch (request.flags & MB_TYPEMASK)
        {
        case MB_OKCANCEL: buttons = { IDOK, IDCANCEL }; break;
        case MB_YESNO: buttons = { IDYES, IDNO }; break;
        case MB_YESNOCANCEL: buttons = { IDYES, IDNO, IDCANCEL }; break;
        case MB_ABORTRETRYIGNORE: buttons = { IDABORT, IDRETRY, IDIGNORE }; break;
        case MB_RETRYCANCEL: buttons = { IDRETRY, IDCANCEL }; break;
        default: buttons = { IDOK }; break;
        }
        dialog.closeResult = (request.flags & MB_TYPEMASK) == MB_ABORTRETRYIGNORE ? IDABORT : buttons.back();
        int defaultButton = static_cast<int>((request.flags & MB_DEFMASK) >> 8);
        defaultButton = (std::min)(defaultButton, static_cast<int>(buttons.size())-1);
        const bool enabled = request.owner && IsWindowEnabled(request.owner);
        if (enabled) EnableWindow(request.owner, FALSE);
        SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA);
        dialog.transitionStart = GetTickCount64();
        ShowWindow(window, SW_SHOW);
        SetForegroundWindow(window);
        SetFocus(window);
        bool first = true;
        bool quitting = false;
        while (true)
        {
            MSG message;
            // Pump this window only; the suspended editor frame must not reenter.
            while (PeekMessageW(&message, window, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                {
                    PostQuitMessage(static_cast<int>(message.wParam));
                    dialog.result = dialog.closeResult;
                    quitting = true;
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (quitting) break;
            const float duration = dialog.closing ? 140.0f : 180.0f;
            const float progress = (std::min)(1.0f,
                static_cast<float>(GetTickCount64() - dialog.transitionStart) / duration);
            const float eased = progress * progress * (3.0f - 2.0f * progress);
            dialog.opacity = dialog.closing ? dialog.closeOpacity * (1.0f - eased) : eased;
            SetLayeredWindowAttributes(window, 0, static_cast<BYTE>(dialog.opacity * 255.0f), LWA_ALPHA);
            // Preserve the selected answer, but keep rendering until the fade finishes.
            if (dialog.closing && progress >= 1.0f) break;
            const HRESULT status = device->TestCooperativeLevel();
            if (status == D3DERR_DEVICELOST) { Sleep(16); continue; }
            if (status == D3DERR_DEVICENOTRESET)
            {
                ImGui_ImplDX9_InvalidateDeviceObjects();
                if (FAILED(device->Reset(&parameters))) { Sleep(16); continue; }
            }
            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::Begin("##SDKMessage", nullptr, ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
            ImGui::BeginDisabled(dialog.closing);
            ImGui::TextColored(style.Colors[ImGuiCol_CheckMark], "X-RAY SDK");
            ImGui::SameLine();
            ImGui::TextUnformatted(title.c_str());
            // An explicit text-only width suppresses the automatic caption icon
            // in the SDK's ButtonEx while retaining its themed button surface.
            const float closeWidth = ImGui::CalcTextSize("X").x + 2 * style.FramePadding.x;
            dialog.closeLeft = static_cast<int>(ImGui::GetWindowWidth() - closeWidth - style.WindowPadding.x);
            ImGui::SameLine(static_cast<float>(dialog.closeLeft));
            if (ImGui::Button("X", ImVec2(closeWidth, 0))) dialog.Close(dialog.closeResult);
            ImGui::Separator();
            const float footerY = ImGui::GetWindowHeight() - buttonHeight - 6 * scale;
            const float messageHeight = (std::max)(1.0f, footerY - ImGui::GetCursorPosY() - 8 * scale);
            ImGui::BeginChild("Message", ImVec2(0, messageHeight), false);
            const UINT icon = request.flags & MB_ICONMASK;
            const char* kind = icon == MB_ICONERROR ? "Error" : icon == MB_ICONWARNING ? "Warning" :
                icon == MB_ICONINFORMATION ? "Information" : "Confirmation";
            const ImVec4 accent = icon == MB_ICONERROR ? ImVec4(1, .27f, .32f, 1) :
                icon == MB_ICONWARNING ? ImVec4(1, .72f, .23f, 1) : style.Colors[ImGuiCol_CheckMark];
            ImGui::TextColored(accent, "%s", kind);
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
            ImGui::SetCursorPosY(footerY);
            for (size_t i = 0; i < buttons.size(); ++i)
            {
                const int id = buttons[i];
                const char* caption = id == IDYES ? "Yes" : id == IDNO ? "No" :
                    id == IDCANCEL ? "Cancel" : id == IDABORT ? "Abort" :
                    id == IDRETRY ? "Retry" : id == IDIGNORE ? "Ignore" :
                    id == SDKDialogs::YesToAll ? "Yes to all" : id == SDKDialogs::NoToAll ? "No to all" : "OK";
                const int custom = id == IDYES ? 0 : id == IDNO ? 1 : id == IDCANCEL ? 2 :
                    id == SDKDialogs::YesToAll ? 3 : id == SDKDialogs::NoToAll ? 4 : -1;
                if (custom >= 0 && request.captions && request.captions[custom])
                    caption = request.captions[custom];
                if (i) ImGui::SameLine();
                ImGui::PushID(id);
                if (first && i == defaultButton) ImGui::SetKeyboardFocusHere();
                const bool clicked = ImGui::Button(UTF8(caption).c_str(), ImVec2(0, buttonHeight));
                const bool focusedEnter = ImGui::IsItemFocused() &&
                    (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
                if (clicked || focusedEnter) dialog.Close(id);
                ImGui::PopID();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) dialog.Close(dialog.closeResult);
            ImGui::EndDisabled();
            ImGui::PopStyleVar();
            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::Render();
            device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
            if (SUCCEEDED(device->BeginScene()))
            {
                ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
                device->EndScene();
            }
            device->Present(nullptr, nullptr, nullptr, nullptr);
            if (first) sound.Play();
            first = false;
        }
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(dialog.context);
        dialog.context = nullptr;
        ImGui::SetCurrentContext(saved);
        device->Release();
        d3d->Release();
        DestroyWindow(window);
        UnregisterClassW(cls.lpszClassName, cls.hInstance);
        if (enabled) { EnableWindow(request.owner, TRUE); SetForegroundWindow(request.owner); }
        return dialog.result;
    }
}

float SDKDialogs::SoundVolume() { return soundVolume; }
void SDKDialogs::SetSoundVolume(float volume)
{
    soundVolume = std::isfinite(volume) ? (std::max)(0.f, (std::min)(1.f, volume)) : 1.f;
    if (soundPreview && !soundPreview->alias.empty())
    {
        const auto command = L"setaudio " + soundPreview->alias + L" volume to " + std::to_wstring(int(soundVolume * 1000.f));
        mciSendStringW(command.c_str(), nullptr, 0, nullptr);
    }
}
bool SDKDialogs::SoundEnabled() { return soundEnabled; }
const char* SDKDialogs::CustomSoundPath() { return customSoundPath.c_str(); }
void SDKDialogs::SetSoundSettings(bool enabled, const char* customPath)
{
    const std::string path = customPath ? customPath : "";
    soundPreview.reset();
    soundEnabled = enabled;
    customSoundPath = path;
    soundSettingsError.clear();
}
bool SDKDialogs::DrawSoundSettings()
{
    bool changed = false;
    if (ImGui::Checkbox("Enable message box sound", &soundEnabled))
    {
        soundPreview.reset();
        changed = true;
    }
    float percent = soundVolume * 100.f;
    if (ImGui::SliderFloat("Volume", &percent, 0.f, 100.f, "%.0f%%"))
    {
        SetSoundVolume(percent / 100.f);
        changed = true;
    }
    ImGui::TextUnformatted(customSoundPath.empty() ? "Sound: Default" : "Sound: Custom");
    if (!customSoundPath.empty())
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 350.f);
        ImGui::TextUnformatted(customSoundPath.c_str());
        ImGui::PopTextWrapPos();
    }
    if (ImGui::Button("Choose audio file..."))
    {
        wchar_t filename[32768]{};
        OPENFILENAMEW dialog{sizeof(dialog)};
        dialog.hwndOwner = editorOwner;
        dialog.lpstrFilter = L"Audio files (*.wav;*.mp3)\0*.wav;*.mp3\0All files\0*.*\0\0";
        dialog.lpstrFile = filename;
        dialog.nMaxFile = DWORD(sizeof(filename)/sizeof(filename[0]));
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
        if (GetOpenFileNameW(&dialog))
        {
            soundPreview.reset();
            Sound candidate;
            const auto chosen = UTF8(filename);
            if (chosen.size() >= 4000)
                soundSettingsError = "The selected path is too long to save. Choose a file with a shorter path.";
            else if (candidate.Open(filename))
            {
                customSoundPath = chosen;
                soundSettingsError.clear();
                changed = true;
            }
            else soundSettingsError = "Cannot open this audio file. Choose a playable WAV or MP3 file.";
        }
        else if (CommDlgExtendedError()) soundSettingsError = "Cannot open the audio file picker.";
    }
    if (ImGui::Button("Use default sound"))
    {
        soundPreview.reset();
        customSoundPath.clear();
        soundSettingsError.clear();
        changed = true;
    }
    ImGui::BeginDisabled(!soundEnabled);
    if (ImGui::Button("Test sound"))
    {
        soundPreview.reset(new Sound);
        soundPreview->Prepare();
        if (soundPreview->alias.empty()) soundSettingsError = "Cannot play the selected sound. Check that the file still exists and is supported.";
        else { soundSettingsError.clear(); soundPreview->Play(); }
    }
    ImGui::EndDisabled();
    if (ImGui::Button("Stop preview")) soundPreview.reset();
    if (!soundSettingsError.empty())
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 350.f);
        ImGui::TextUnformatted(soundSettingsError.c_str());
        ImGui::PopTextWrapPos();
    }
    return changed;
}

void SDKDialogs::Initialize(HWND owner)
{
    editorOwner = owner;
}
void SDKDialogs::Shutdown()
{
    soundPreview.reset();
    EndProgress();
    editorOwner = nullptr;
    std::lock_guard<std::mutex> lock(notificationsMutex);
    notifications.clear();
}
bool SDKDialogs::HandleMessage(UINT message, LPARAM parameter, LRESULT& result)
{
    if (message != dialogMessage) return false;
    result = ShowDialog(*reinterpret_cast<Request*>(parameter));
    return true;
}
int SDKDialogs::Show(HWND owner, const char* text, const char* title, UINT flags,
    const char* const* captions)
{
    Request request{ owner ? owner : editorOwner, text, title, flags & ~SDKDialogs::FourChoice, captions, false,
        (flags & SDKDialogs::FourChoice) != 0 };
    if (editorOwner && GetWindowThreadProcessId(editorOwner, nullptr) != GetCurrentThreadId())
        return static_cast<int>(SendMessageW(editorOwner, dialogMessage, 0, reinterpret_cast<LPARAM>(&request)));
    return ShowDialog(request);
}
int SDKDialogs::ShowWide(HWND owner, const wchar_t* text, const wchar_t* title, UINT flags)
{
    const std::string message = UTF8(text), caption = UTF8(title);
    Request request{ owner ? owner : editorOwner, message.c_str(), caption.c_str(), flags, nullptr, true, false };
    if (editorOwner && GetWindowThreadProcessId(editorOwner, nullptr) != GetCurrentThreadId())
        return static_cast<int>(SendMessageW(editorOwner, dialogMessage, 0, reinterpret_cast<LPARAM>(&request)));
    return ShowDialog(request);
}
void SDKDialogs::Notify(const char* text, const char* title, UINT flags)
{
    std::lock_guard<std::mutex> lock(notificationsMutex);
    notifications.push_back({ text ? text : "", title ? title : "", flags });
}
void SDKDialogs::Pump()
{
    Notification notification;
    {
        std::lock_guard<std::mutex> lock(notificationsMutex);
        if (notifications.empty()) return;
        notification = std::move(notifications.front());
        notifications.pop_front();
    }
    Show(editorOwner, notification.text.c_str(), notification.title.c_str(), notification.flags);
}

namespace
{
    // Loading can run inside an ImGui menu callback with the editor's D3D
    // BeginScene still active. Its frame/device must remain untouched.
    struct ProgressWindow
    {
        Dialog dialog;
        HWND window = nullptr, owner = nullptr;
        HINSTANCE module = nullptr;
        HMODULE assets = nullptr;
        IDirect3D9* d3d = nullptr;
        IDirect3DDevice9* device = nullptr;
        D3DPRESENT_PARAMETERS parameters{};
        bool ownerEnabled = false;
        bool backendsReady = false;
        ULONGLONG lastDraw = 0;
        std::string text;
        float fraction = 0.f, completed = 0.f, total = 0.f;
        float scale = 1.f;

        ~ProgressWindow()
        {
            ImGuiContext* saved = ImGui::GetCurrentContext();
            if (dialog.context)
            {
                ImGui::SetCurrentContext(dialog.context);
                if (backendsReady)
                {
                    ImGui_ImplDX9_Shutdown();
                    ImGui_ImplWin32_Shutdown();
                }
                ImGui::DestroyContext(dialog.context);
                dialog.context = nullptr;
                ImGui::SetCurrentContext(saved);
            }
            if (device) device->Release();
            if (d3d) d3d->Release();
            const bool focused = window && GetForegroundWindow() == window;
            if (ownerEnabled && IsWindow(owner)) EnableWindow(owner, TRUE);
            if (window) DestroyWindow(window);
            if (module) UnregisterClassW(L"XRaySDKImGuiProgress", module);
            if (assets) FreeLibrary(assets);
            if (focused && IsWindow(owner)) SetForegroundWindow(owner);
        }

        bool Create(HWND parent)
        {
            owner = parent;
            ImGuiContext* saved = ImGui::GetCurrentContext();
            ImGuiStyle style;
            float fontSize = 16.f;
            if (saved) { style = ImGui::GetStyle(); fontSize = (std::max)(14.f, ImGui::GetFontSize()); }
            else ImGui::StyleColorsDark(&style);
            scale = (std::max)(1.f, fontSize / 16.f);
            const int width = int(480 * scale), height = int(250 * scale);
            MONITORINFO monitor{sizeof(monitor)};
            GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor);
            RECT area = monitor.rcWork;
            RECT parentRect;
            if (owner && GetWindowRect(owner, &parentRect)) area = parentRect;
            const int x = (std::max)(monitor.rcWork.left, (std::min)(
                area.left + (area.right - area.left - width) / 2, monitor.rcWork.right - width));
            const int y = (std::max)(monitor.rcWork.top, (std::min)(
                area.top + (area.bottom - area.top - height) / 2, monitor.rcWork.bottom - height));
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&DialogProc), &module);
            WNDCLASSW cls{};
            cls.lpfnWndProc = DialogProc;
            cls.hInstance = module;
            cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
            cls.lpszClassName = L"XRaySDKImGuiProgress";
            RegisterClassW(&cls);
            dialog.dragHeight = int(fontSize + style.WindowPadding.y * 2);
            dialog.closeLeft = width;
            window = CreateWindowExW(WS_EX_LAYERED, cls.lpszClassName, L"X-Ray SDK - Loading",
                WS_POPUP, x, y, width, height, owner, nullptr, module, &dialog);
            if (!window) return false;
            // Leave the rounded ImGui corners transparent in the native host.
            SetLayeredWindowAttributes(window, RGB(1, 2, 3), 255, LWA_COLORKEY);
            d3d = Direct3DCreate9(D3D_SDK_VERSION);
            if (!d3d) return false;
            parameters.Windowed = TRUE;
            parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
            parameters.BackBufferFormat = D3DFMT_UNKNOWN;
            parameters.hDeviceWindow = window;
            parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
            if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &parameters, &device))) return false;
            dialog.context = ImGui::CreateContext();
            ImGui::SetCurrentContext(dialog.context);
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            ImGui::GetStyle() = style;
            wchar_t modulePath[MAX_PATH]{};
            GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
            wchar_t* separator = wcsrchr(modulePath, L'\\');
            if (separator)
            {
                *(separator + 1) = 0;
                assets = LoadLibraryExW((std::wstring(modulePath) + L"LauncherAssets.dll").c_str(),
                    nullptr, LOAD_LIBRARY_AS_DATAFILE);
            }
            HRSRC resource = assets ? FindResourceW(assets, MAKEINTRESOURCEW(IDR_LAUNCHER_FONT), MAKEINTRESOURCEW(10)) : nullptr;
            HGLOBAL font = resource ? LoadResource(assets, resource) : nullptr;
            if (font)
            {
                ImFontConfig config;
                config.FontDataOwnedByAtlas = false;
                io.Fonts->AddFontFromMemoryTTF(LockResource(font), int(SizeofResource(assets, resource)),
                    fontSize, &config, io.Fonts->GetGlyphRangesCyrillic());
            }
            else io.Fonts->AddFontDefault();
            ImGui_ImplWin32_Init(window);
            ImGui_ImplDX9_Init(device);
            backendsReady = true;
            ImGui::SetCurrentContext(saved);
            ownerEnabled = owner && IsWindowEnabled(owner);
            if (ownerEnabled) EnableWindow(owner, FALSE);
            ShowWindow(window, SW_SHOW);
            return true;
        }

        void Draw(bool force)
        {
            const ULONGLONG now = GetTickCount64();
            if (!force && now - lastDraw < 33) return;
            lastDraw = now;
            // Never dispatch editor commands while a partially loaded scene is live.
            MSG message;
            while (PeekMessageW(&message, window, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                {
                    PostQuitMessage(int(message.wParam));
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            const HRESULT status = device->TestCooperativeLevel();
            if (status == D3DERR_DEVICELOST) return;
            ImGuiContext* saved = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(dialog.context);
            if (status == D3DERR_DEVICENOTRESET)
            {
                ImGui_ImplDX9_InvalidateDeviceObjects();
                if (FAILED(device->Reset(&parameters))) { ImGui::SetCurrentContext(saved); return; }
            }
            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.f * scale);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.f * scale, 16.f * scale));
            ImGui::Begin("##SDKProgress", nullptr, ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            ImGui::PopStyleVar(3);
            ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_CheckMark], "X-RAY SDK");
            ImGui::TextUnformatted("Loading");
            ImGui::Spacing();
            ImGui::BeginChild("##LoadingMessage", ImVec2(0, 90.f * scale), false);
            ImGui::TextWrapped("%s", text.c_str());
            ImGui::EndChild();
            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 68.f * scale);
            ImGui::TextDisabled("Progress");
            SDKUI::ProgressBar(fraction, true, scale);
            if (total > 0.f) ImGui::TextDisabled("%.0f%%  /  %.0f of %.0f", fraction * 100.f, completed, total);
            else ImGui::TextDisabled("%.0f%%", fraction * 100.f);
            ImGui::End();
            ImGui::Render();
            device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(1, 2, 3), 1.f, 0);
            if (SUCCEEDED(device->BeginScene()))
            {
                ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
                device->EndScene();
            }
            device->Present(nullptr, nullptr, nullptr, nullptr);
            ImGui::SetCurrentContext(saved);
        }
    };
    std::unique_ptr<ProgressWindow> progressWindow;
}

bool SDKDialogs::BeginProgress(HWND owner)
{
    if (progressWindow) return true;
    std::unique_ptr<ProgressWindow> window(new ProgressWindow);
    if (!window->Create(owner ? owner : editorOwner)) return false;
    progressWindow = std::move(window);
    return true;
}

void SDKDialogs::UpdateProgress(const char* text, float completed, float total, float overall, bool force)
{
    if (!progressWindow) return;
    progressWindow->text = UTF8(text);
    progressWindow->total = (std::max)(0.f, total);
    progressWindow->completed = (std::max)(0.f, (std::min)(completed, progressWindow->total));
    progressWindow->fraction = overall >= 0.f ?
        (std::max)(progressWindow->fraction, (std::min)(overall, 1.f)) :
        (total > 0.f ? progressWindow->completed / total : 0.f);
    progressWindow->Draw(force);
}

void SDKDialogs::EndProgress()
{
    progressWindow.reset();
}
