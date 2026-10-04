#include "Platform.h"
#include "RecentFiles.h"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include <windowsx.h>
#include <wincodec.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <algorithm>
#include <cstring>
#include <cstdint>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{
    constexpr wchar_t WindowClass[] = L"FarkashedSDK.Launcher";
    constexpr float WindowWidth = 576.f;
    constexpr float WindowHeight = 437.f;

    template<class T> void Release(T*& object)
    {
        if (object)
            object->Release();
        object = nullptr;
    }

    std::wstring LastErrorText(DWORD error)
    {
        LPWSTR message = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
        std::wstring result = message ? message : L"Windows could not start the editor.";
        if (message)
            LocalFree(message);
        return result;
    }

    // Match CommandLineToArgvW, including backslashes before quotes and the closing quote.
    std::wstring QuoteArgument(const std::wstring& argument)
    {
        std::wstring result = L"\"";
        size_t backslashes = 0;
        for (wchar_t character : argument)
        {
            if (character == L'\\')
                ++backslashes;
            else
            {
                result.append(backslashes * (character == L'"' ? 2 : 1), L'\\');
                if (character == L'"')
                    result += L'\\';
                result += character;
                backslashes = 0;
            }
        }
        result.append(backslashes * 2, L'\\');
        return result + L'"';
    }

    // Sliding box passes approximate a Gaussian blur without shaders or extra DLLs.
    void Blur(std::vector<unsigned char>& pixels, unsigned width, unsigned height, int radius)
    {
        std::vector<unsigned char> temporary(pixels.size());
        for (int pass = 0; pass < 3; ++pass)
        {
            for (unsigned y = 0; y < height; ++y)
            {
                int sums[3] = {};
                for (int x = -radius; x <= radius; ++x)
                {
                    const unsigned offset = (y * width + unsigned((std::max)(0, (std::min)(int(width) - 1, x)))) * 4;
                    for (int c = 0; c < 3; ++c)
                        sums[c] += pixels[offset + c];
                }
                for (unsigned x = 0; x < width; ++x)
                {
                    const unsigned offset = (y * width + x) * 4;
                    for (int c = 0; c < 3; ++c)
                        temporary[offset + c] = static_cast<unsigned char>(sums[c] / (radius * 2 + 1));
                    temporary[offset + 3] = 255;
                    const unsigned left = (y * width + unsigned((std::max)(0, int(x) - radius))) * 4;
                    const unsigned right = (y * width + unsigned((std::min)(int(width) - 1, int(x) + radius + 1))) * 4;
                    for (int c = 0; c < 3; ++c)
                        sums[c] += int(pixels[right + c]) - int(pixels[left + c]);
                }
            }
            for (unsigned x = 0; x < width; ++x)
            {
                int sums[3] = {};
                for (int y = -radius; y <= radius; ++y)
                {
                    const unsigned offset = (unsigned((std::max)(0, (std::min)(int(height) - 1, y))) * width + x) * 4;
                    for (int c = 0; c < 3; ++c)
                        sums[c] += temporary[offset + c];
                }
                for (unsigned y = 0; y < height; ++y)
                {
                    const unsigned offset = (y * width + x) * 4;
                    for (int c = 0; c < 3; ++c)
                        pixels[offset + c] = static_cast<unsigned char>(sums[c] / (radius * 2 + 1));
                    pixels[offset + 3] = 255;
                    const unsigned top = (unsigned((std::max)(0, int(y) - radius)) * width + x) * 4;
                    const unsigned bottom = (unsigned((std::min)(int(height) - 1, int(y) + radius + 1)) * width + x) * 4;
                    for (int c = 0; c < 3; ++c)
                        sums[c] += int(temporary[bottom + c]) - int(temporary[top + c]);
                }
            }
        }
    }
}

Texture::~Texture() { Release(handle); }

bool LauncherPlatform::Initialize(HINSTANCE applicationInstance)
{
    instance = applicationInstance;
    ImGui_ImplWin32_EnableDpiAwareness();
    HICON icon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    if (!icon)
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    const WNDCLASSEXW windowClass = { sizeof(WNDCLASSEXW), CS_CLASSDC, WindowProc, 0, 0,
        instance, icon, LoadCursorW(nullptr, IDC_ARROW), nullptr,
        nullptr, WindowClass, nullptr };
    if (!RegisterClassExW(&windowClass))
        return false;
    POINT cursor;
    GetCursorPos(&cursor);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    dpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(monitor);
    MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
    GetMonitorInfoW(monitor, &monitorInfo);
    const int width = int(WindowWidth * dpiScale);
    const int height = int(WindowHeight * dpiScale);
    window = CreateWindowExW(WS_EX_APPWINDOW, WindowClass, L"X-Ray SDK Farkashed Edition",
        WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
        monitorInfo.rcWork.left + (monitorInfo.rcWork.right - monitorInfo.rcWork.left - width) / 2,
        monitorInfo.rcWork.top + (monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - height) / 2,
        width, height, nullptr, nullptr, instance, this);
    if (!window)
        return false;
    direct3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!direct3D)
        return false;
    parameters.Windowed = TRUE;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.BackBufferFormat = D3DFMT_UNKNOWN;
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    parameters.hDeviceWindow = window;
    HRESULT result = direct3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &parameters, &device);
    if (FAILED(result))
        result = direct3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &parameters, &device);
    if (FAILED(result))
        return false;
    CaptureBackdrop(); // Captured before showing the launcher; only kept in GPU memory.
    DragAcceptFiles(window, TRUE);
    const int cornerPreference = 1; // Preserve the rectangular red frame on Windows 11.
    DwmSetWindowAttribute(window, 33, &cornerPreference, sizeof(cornerPreference));
    return true;
}

void LauncherPlatform::Shutdown()
{
    backdrop.reset();
    Release(device);
    Release(direct3D);
    if (window)
        DestroyWindow(window);
    window = nullptr;
    if (instance)
        UnregisterClassW(WindowClass, instance);
}

std::unique_ptr<Texture> LauncherPlatform::Upload(const unsigned char* pixels, unsigned width, unsigned height)
{
    auto texture = std::make_unique<Texture>();
    if (FAILED(device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED, &texture->handle, nullptr)))
        return {};
    D3DLOCKED_RECT lock;
    if (FAILED(texture->handle->LockRect(0, &lock, nullptr, 0)))
        return {};
    for (unsigned y = 0; y < height; ++y)
        std::memcpy(static_cast<unsigned char*>(lock.pBits) + size_t(y) * lock.Pitch,
            pixels + size_t(y) * width * 4, size_t(width) * 4);
    texture->handle->UnlockRect(0);
    texture->width = width;
    texture->height = height;
    return texture;
}

std::unique_ptr<Texture> LauncherPlatform::LoadTexture(const std::wstring& path, bool frame)
{
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* image = nullptr;
    IWICFormatConverter* converter = nullptr;
    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (SUCCEEDED(result))
        result = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(result))
        result = decoder->GetFrame(0, &image);
    if (SUCCEEDED(result))
        result = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(result))
        result = converter->Initialize(image, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
            nullptr, 0.f, WICBitmapPaletteTypeCustom);
    unsigned width = 0, height = 0;
    if (SUCCEEDED(result))
        result = converter->GetSize(&width, &height);
    std::unique_ptr<Texture> texture;
    if (SUCCEEDED(result) && width > 0 && height > 0 && width <= 8192 && height <= 8192)
    {
        std::vector<unsigned char> pixels(size_t(width) * height * 4);
        result = converter->CopyPixels(nullptr, width * 4, UINT(pixels.size()), pixels.data());
        if (SUCCEEDED(result))
        {
            if (frame)
                for (size_t i = 0; i < pixels.size(); i += 4)
                    if (pixels[i] < 16 && pixels[i + 1] < 16 && pixels[i + 2] < 16)
                        pixels[i + 3] = 0; // Keep Window.png's red outline over the blurred backdrop.
            texture = Upload(pixels.data(), width, height);
        }
    }
    Release(converter);
    Release(image);
    Release(decoder);
    Release(factory);
    return texture;
}

void LauncherPlatform::CaptureBackdrop()
{
    desktopBounds.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    desktopBounds.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    desktopBounds.right = desktopBounds.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    desktopBounds.bottom = desktopBounds.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    const int desktopWidth = desktopBounds.right - desktopBounds.left;
    const int desktopHeight = desktopBounds.bottom - desktopBounds.top;
    if (desktopWidth <= 0 || desktopHeight <= 0)
        return;
    D3DCAPS9 caps = {};
    device->GetDeviceCaps(&caps);
    const float scale = (std::min)(0.25f, (std::min)(float(caps.MaxTextureWidth) / desktopWidth,
        float(caps.MaxTextureHeight) / desktopHeight));
    const int width = (std::max)(1, int(desktopWidth * scale));
    const int height = (std::max)(1, int(desktopHeight * scale));
    HDC desktop = GetDC(nullptr);
    if (!desktop)
        return;
    HDC memory = CreateCompatibleDC(desktop);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* data = nullptr;
    HBITMAP bitmap = CreateDIBSection(desktop, &info, DIB_RGB_COLORS, &data, nullptr, 0);
    if (desktop && memory && bitmap)
    {
        HGDIOBJ previous = SelectObject(memory, bitmap);
        SetStretchBltMode(memory, HALFTONE);
        if (StretchBlt(memory, 0, 0, width, height, desktop, desktopBounds.left, desktopBounds.top,
            desktopWidth, desktopHeight, SRCCOPY))
        {
            std::vector<unsigned char> pixels(size_t(width) * height * 4);
            std::memcpy(pixels.data(), data, pixels.size());
            Blur(pixels, unsigned(width), unsigned(height), 5);
            backdrop = Upload(pixels.data(), unsigned(width), unsigned(height));
        }
        SelectObject(memory, previous);
    }
    if (bitmap)
        DeleteObject(bitmap);
    if (memory)
        DeleteDC(memory);
    if (desktop)
        ReleaseDC(nullptr, desktop);
}

bool LauncherPlatform::PumpMessages()
{
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        if (message.message == WM_QUIT)
            quit = true;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return !quit;
}

bool LauncherPlatform::ResetDevice()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    if (FAILED(device->Reset(&parameters)))
        return false;
    ImGui_ImplDX9_CreateDeviceObjects();
    resetPending = false;
    return true;
}

bool LauncherPlatform::Render()
{
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(12, 12, 14), 1.f, 0);
    if (SUCCEEDED(device->BeginScene()))
    {
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        device->EndScene();
    }
    const HRESULT result = device->Present(nullptr, nullptr, nullptr, nullptr);
    if (result == D3DERR_DEVICELOST)
    {
        if (device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
            resetPending = true;
        return false;
    }
    return SUCCEEDED(result);
}

bool LauncherPlatform::PrepareFrame()
{
    const HRESULT status = device->TestCooperativeLevel();
    if (status == D3DERR_DEVICELOST)
        return false;
    if (resetPending || status == D3DERR_DEVICENOTRESET)
        return ResetDevice();
    return SUCCEEDED(status);
}

std::wstring LauncherPlatform::PickFile()
{
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return {};
    const COMDLG_FILTERSPEC filters[] = {
        { L"SDK projects and files", L"*.level;*.object;*.xr" },
        { L"Level Editor scenes", L"*.level" }, { L"Actor Editor objects", L"*.object" },
        { L"Particle libraries", L"*.xr" }
    };
    dialog->SetFileTypes(UINT(sizeof(filters) / sizeof(filters[0])), filters);
    dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Open an SDK project or file");
    std::wstring selected;
    if (SUCCEEDED(dialog->Show(window)))
    {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)))
        {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
            {
                selected = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return selected;
}

std::wstring LauncherPlatform::PickSDKDirectory()
{
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return {};
    dialog->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Select the folder containing the SDK editor executables");
    std::wstring selected;
    if (SUCCEEDED(dialog->Show(window)))
    {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)))
        {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
            {
                selected = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return selected;
}

bool LauncherPlatform::StartEditor(const std::wstring& executable, const std::wstring& file, std::wstring& error)
{
    const std::wstring directory = executable.substr(0, executable.find_last_of(L"\\/"));
    if (!FarkashedLauncher::FileExists(executable))
    {
        error = L"Editor executable was not found:\n" + executable + L"\n\nChoose the SDK folder in Settings.";
        return false;
    }
    if (!file.empty() && !FarkashedLauncher::FileExists(file))
    {
        error = L"This recent file has been moved or deleted:\n" + file;
        return false;
    }
    std::wstring arguments = QuoteArgument(executable);
    if (!file.empty())
        arguments += L" --launcher-open " + QuoteArgument(file);
    std::vector<wchar_t> commandLine(arguments.begin(), arguments.end());
    commandLine.push_back(0);
    STARTUPINFOW startup = { sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0,
        nullptr, directory.c_str(), &startup, &process))
    {
        error = LastErrorText(GetLastError()) + L"\n" + executable;
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

void LauncherPlatform::RevealFile(const std::wstring& file)
{
    PIDLIST_ABSOLUTE item = nullptr;
    if (SUCCEEDED(SHParseDisplayName(file.c_str(), nullptr, &item, 0, nullptr)))
    {
        SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
        CoTaskMemFree(item);
    }
}

LRESULT CALLBACK LauncherPlatform::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    LauncherPlatform* app = reinterpret_cast<LauncherPlatform*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        app = static_cast<LauncherPlatform*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
        return TRUE;
    if (app)
    {
        switch (message)
        {
        case WM_CLOSE:
            app->closeRequested = true;
            return 0;
        case WM_NCHITTEST:
        {
            POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(window, &point);
            RECT client;
            GetClientRect(window, &client);
            if (point.y >= 0 && point.y < int(40.f * app->dpiScale) && point.x > int(14.f * app->dpiScale) &&
                point.x < client.right - int(82.f * app->dpiScale))
                return HTCAPTION;
            return HTCLIENT;
        }
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED && app->device)
            {
                app->parameters.BackBufferWidth = LOWORD(lParam);
                app->parameters.BackBufferHeight = HIWORD(lParam);
                app->resetPending = true;
            }
            return 0;
        case WM_DPICHANGED:
        {
            app->dpiScale = float(HIWORD(wParam)) / 96.f;
            app->fontsDirty = true;
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                int(WindowWidth * app->dpiScale), int(WindowHeight * app->dpiScale), SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_DROPFILES:
        {
            const HDROP drop = reinterpret_cast<HDROP>(wParam);
            const UINT count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
            for (UINT i = 0; i < count; ++i)
            {
                std::vector<wchar_t> path(DragQueryFileW(drop, i, nullptr, 0) + 1);
                DragQueryFileW(drop, i, path.data(), UINT(path.size()));
                app->droppedFiles.emplace_back(path.data());
            }
            DragFinish(drop);
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU || (wParam & 0xfff0) == SC_MAXIMIZE)
                return 0;
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
