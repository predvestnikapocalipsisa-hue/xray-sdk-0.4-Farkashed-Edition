#ifdef XR_IMGUI_STANDALONE
#include <windows.h>
#include <d3d9.h>
#include "imgui.h"
#else
#include "stdafx.h"
#endif
#include "SDKMessageBox.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "../../LauncherAssets/ResourceIds.h"
#include <mmsystem.h>
#include <algorithm>
#include <string>
#include <vector>
#include <mutex>
#include <deque>
#include <cstdint>
#pragma comment(lib, "winmm.lib")

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{
    constexpr UINT dialogMessage = WM_APP + 0x3A1;
    HWND editorOwner = nullptr;
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
        void Prepare()
        {
            wchar_t modulePath[MAX_PATH]{};
            GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
            wchar_t* separator = wcsrchr(modulePath, L'\\');
            if (!separator) return;
            *(separator + 1) = 0;
            const std::wstring dll = std::wstring(modulePath) + L"LauncherAssets.dll";
            assets = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
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
            alias = L"xr_dialog_" + std::to_wstring(reinterpret_cast<uintptr_t>(this));
            const std::wstring command = L"open \"" + std::wstring(path) + L"\" type mpegvideo alias " + alias;
            mciSendStringW(command.c_str(), nullptr, 0, nullptr);
        }
        void Play()
        {
            if (!alias.empty()) mciSendStringW((L"play " + alias + L" from 0").c_str(), nullptr, 0, nullptr);
        }
        ~Sound()
        {
            if (!alias.empty()) mciSendStringW((L"close " + alias).c_str(), nullptr, 0, nullptr);
            if (path[0]) DeleteFileW(path);
            if (assets) FreeLibrary(assets);
        }
    };
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

void SDKDialogs::Initialize(HWND owner)
{
    editorOwner = owner;
}
void SDKDialogs::Shutdown()
{
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
