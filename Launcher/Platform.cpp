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
#include <cmath>

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

    // Area filtering integrates all source pixels covered by an output pixel.
    // Accumulate premultiplied colors to avoid fringes around transparent edges.
    void ResizeImage(std::vector<unsigned char>& pixels, unsigned& width, unsigned& height,
        unsigned size, unsigned requestedHeight)
    {
        unsigned left = width, top = height, right = 0, bottom = 0;
        if (requestedHeight)
        {
            // Keep the logo's full design canvas and its intentional margins.
            left = top = 0;
            right = width;
            bottom = height;
        }
        else for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x)
                if (pixels[(size_t(y) * width + x) * 4 + 3])
                {
                    left = (std::min)(left, x);
                    top = (std::min)(top, y);
                    right = (std::max)(right, x + 1);
                    bottom = (std::max)(bottom, y + 1);
                }
        if (right <= left || bottom <= top)
            return;
        const unsigned sourceWidth = right - left, sourceHeight = bottom - top;
        const float ratio = float(size) / float((std::max)(sourceWidth, sourceHeight));
        const unsigned targetWidth = requestedHeight ? size : (std::max)(1u, unsigned(sourceWidth * ratio + 0.5f));
        const unsigned targetHeight = requestedHeight ? requestedHeight : (std::max)(1u, unsigned(sourceHeight * ratio + 0.5f));
        std::vector<unsigned char> output(size_t(targetWidth) * targetHeight * 4);
        for (unsigned y = 0; y < targetHeight; ++y)
            for (unsigned x = 0; x < targetWidth; ++x)
            {
                const float x0 = left + float(x) * sourceWidth / targetWidth;
                const float x1 = left + float(x + 1) * sourceWidth / targetWidth;
                const float y0 = top + float(y) * sourceHeight / targetHeight;
                const float y1 = top + float(y + 1) * sourceHeight / targetHeight;
                float alpha = 0.f, area = 0.f, colors[3] = {};
                for (unsigned sy = unsigned(y0); sy < (std::min)(bottom, unsigned(std::ceil(y1))); ++sy)
                    for (unsigned sx = unsigned(x0); sx < (std::min)(right, unsigned(std::ceil(x1))); ++sx)
                    {
                        const float weight = ((std::min)(x1, float(sx + 1)) - (std::max)(x0, float(sx))) *
                            ((std::min)(y1, float(sy + 1)) - (std::max)(y0, float(sy)));
                        const unsigned char* source = &pixels[(size_t(sy) * width + sx) * 4];
                        const float weightedAlpha = weight * source[3];
                        area += weight;
                        alpha += weightedAlpha;
                        for (unsigned c = 0; c < 3; ++c)
                            colors[c] += weightedAlpha * source[c];
                    }
                unsigned char* destination = &output[(size_t(y) * targetWidth + x) * 4];
                for (unsigned c = 0; c < 3; ++c)
                    destination[c] = alpha > 0.f ? static_cast<unsigned char>((std::min)(255.f, colors[c] / alpha + 0.5f)) : 0;
                destination[3] = area > 0.f ? static_cast<unsigned char>((std::min)(255.f, alpha / area + 0.5f)) : 0;
            }
        pixels = std::move(output);
        width = targetWidth;
        height = targetHeight;
    }

}

Texture::~Texture() { Release(handle); }

bool LauncherPlatform::Initialize(HINSTANCE applicationInstance)
{
    instance = applicationInstance;
    ImGui_ImplWin32_EnableDpiAwareness();
    HICON icon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    if (!icon)
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    HICON smallIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    const WNDCLASSEXW windowClass = { sizeof(WNDCLASSEXW), CS_CLASSDC, WindowProc, 0, 0,
        instance, icon, LoadCursorW(nullptr, IDC_ARROW), nullptr,
        nullptr, WindowClass, smallIcon ? smallIcon : icon };
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
    parameters.BackBufferFormat = D3DFMT_A8R8G8B8;
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    parameters.hDeviceWindow = window;
    HRESULT result = direct3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &parameters, &device);
    if (FAILED(result))
        result = direct3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &parameters, &device);
    if (FAILED(result))
        return false;
    SetBlur(true);
    const std::wstring assetPath = FarkashedLauncher::ModuleDirectory() + L"\\LauncherAssets.dll";
    assetsModule = LoadLibraryExW(assetPath.c_str(), nullptr,
        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!assetsModule)
        return false;
    DragAcceptFiles(window, TRUE);
    const int cornerPreference = 1; // Preserve the rectangular red frame on Windows 11.
    DwmSetWindowAttribute(window, 33, &cornerPreference, sizeof(cornerPreference));
    return true;
}

void LauncherPlatform::Shutdown()
{
    redrawDuringMove = {};
    KillTimer(window, 1);
    Release(device);
    Release(direct3D);
    if (assetsModule)
        FreeLibrary(assetsModule);
    assetsModule = nullptr;
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

LauncherAssetData LauncherPlatform::Asset(unsigned resourceId) const
{
    if (!assetsModule)
        return {};
    const HRSRC resource = FindResourceW(assetsModule, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource)
        return {};
    const HGLOBAL loaded = LoadResource(assetsModule, resource);
    if (!loaded)
        return {};
    return { static_cast<const unsigned char*>(LockResource(loaded)), SizeofResource(assetsModule, resource) };
}

std::unique_ptr<Texture> LauncherPlatform::LoadTexture(unsigned resourceId, unsigned targetWidth, unsigned targetHeight)
{
    const LauncherAssetData asset = Asset(resourceId);
    if (!asset.data || !asset.size || !targetWidth)
        return {};
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* image = nullptr;
    IWICFormatConverter* converter = nullptr;
    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (SUCCEEDED(result))
        result = factory->CreateStream(&stream);
    if (SUCCEEDED(result))
        result = stream->InitializeFromMemory(const_cast<BYTE*>(asset.data), asset.size);
    if (SUCCEEDED(result))
        result = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
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
            ResizeImage(pixels, width, height, targetWidth, targetHeight);
            texture = Upload(pixels.data(), width, height);
        }
    }
    Release(converter);
    Release(image);
    Release(decoder);
    Release(stream);
    Release(factory);
    return texture;
}

void LauncherPlatform::SetBlur(bool enabled)
{
    blurRequested = enabled;
    // Windows 11 provides a DWM backdrop. Older Windows 10 uses the dynamically
    // resolved accent policy; no desktop capture or CPU blur is needed.
    nativeBlur = false;
    const int backdropType = enabled ? 3 : 1; // DWMSBT_TRANSIENTWINDOW / NONE
    const HRESULT backdropResult = DwmSetWindowAttribute(window, 38, &backdropType, sizeof(backdropType));
    struct AccentPolicy { int state; int flags; DWORD color; int animation; };
    struct CompositionData { int attribute; void* data; SIZE_T size; };
    using SetComposition = BOOL(WINAPI*)(HWND, CompositionData*);
    const auto setComposition = reinterpret_cast<SetComposition>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
    if (SUCCEEDED(backdropResult))
        nativeBlur = enabled;
    if (setComposition)
    {
        // Disable the legacy policy when the documented system backdrop is available.
        AccentPolicy policy = { enabled && FAILED(backdropResult) ? 3 : 0, 0, 0, 0 };
        CompositionData data = { 19, &policy, sizeof(policy) };
        const BOOL success = setComposition(window, &data);
        if (enabled && FAILED(backdropResult))
            nativeBlur = success != FALSE;
    }
    const MARGINS margins = nativeBlur ? MARGINS{ -1, -1, -1, -1 } : MARGINS{};
    DwmExtendFrameIntoClientArea(window, &margins);
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
    device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
    device->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    device->Clear(0, nullptr, D3DCLEAR_TARGET, nativeBlur ? D3DCOLOR_ARGB(0, 0, 0, 0) : D3DCOLOR_XRGB(12, 12, 14), 1.f, 0);
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
    dialog->SetTitle(L"Select the folder containing Launcher.exe and the SDK modules");
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
    const bool debugMode = FarkashedLauncher::Setting(L"DebugMode") == L"1";
    if (!debugMode && !file.empty() && !FarkashedLauncher::FileExists(file))
    {
        error = L"This recent file has been moved or deleted:\n" + file;
        return false;
    }
    const std::wstring moduleName = executable.substr(executable.find_last_of(L"\\/") + 1);
    std::wstring editor;
    if (_wcsicmp(moduleName.c_str(), L"ActorEditor.dll") == 0) editor = L"actor";
    if (_wcsicmp(moduleName.c_str(), L"LevelEditor.dll") == 0) editor = L"level";
    if (_wcsicmp(moduleName.c_str(), L"ParticleEditor.dll") == 0) editor = L"particle";
    if (_wcsicmp(moduleName.c_str(), L"ShaderEditor.dll") == 0) editor = L"shader";
    const std::wstring host = directory + L"\\Launcher.exe";
    if (editor.empty() || !FarkashedLauncher::FileExists(host))
    {
        error = L"The selected SDK must contain Launcher.exe and the editor DLLs.";
        return false;
    }
    std::wstring arguments = QuoteArgument(host) + L" --sdk-editor " + editor;
    if (debugMode)
        arguments += L" -sdk_debug";
    if (!file.empty())
        arguments += L" --launcher-open " + QuoteArgument(file);
    std::vector<wchar_t> commandLine(arguments.begin(), arguments.end());
    commandLine.push_back(0);
    STARTUPINFOW startup = { sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(host.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0,
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
        case WM_ENTERSIZEMOVE:
            app->moving = true;
            SetTimer(window, 1, 16, nullptr);
            return 0;
        case WM_TIMER:
            if (wParam == 1 && app->moving && app->redrawDuringMove)
            {
                app->redrawDuringMove();
                return 0;
            }
            break;
        case WM_EXITSIZEMOVE:
            KillTimer(window, 1);
            app->moving = false;
            return 0;
        case WM_DWMCOMPOSITIONCHANGED:
            app->SetBlur(app->blurRequested);
            return 0;
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
