#pragma once

#include <windows.h>
#include <d3d9.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct Texture
{
    IDirect3DTexture9* handle = nullptr;
    unsigned width = 0;
    unsigned height = 0;
    ~Texture();
    Texture() = default;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
};

struct LauncherAssetData
{
    const unsigned char* data = nullptr;
    DWORD size = 0;
};

class LauncherPlatform
{
public:
    HWND window = nullptr;
    IDirect3DDevice9* device = nullptr;
    float dpiScale = 1.f;
    bool fontsDirty = true;
    bool closeRequested = false;
    std::vector<std::wstring> droppedFiles;
    std::function<void()> redrawDuringMove;
    bool moving = false;
    bool nativeBlur = false;

    bool Initialize(HINSTANCE instance);
    void Shutdown();
    bool PumpMessages();
    bool PrepareFrame();
    bool Render();
    LauncherAssetData Asset(unsigned resourceId) const;
    std::unique_ptr<Texture> LoadTexture(unsigned resourceId, unsigned targetWidth, unsigned targetHeight = 0);
    void SetBlur(bool enabled);
    std::wstring PickFile();
    std::wstring PickSDKDirectory();
    bool StartEditor(const std::wstring& executable, const std::wstring& file, std::wstring& error);
    void RevealFile(const std::wstring& file);

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

private:
    IDirect3D9* direct3D = nullptr;
    D3DPRESENT_PARAMETERS parameters = {};
    bool quit = false;
    bool resetPending = false;
    bool blurRequested = true;
    HINSTANCE instance = nullptr;
    HMODULE assetsModule = nullptr;

    std::unique_ptr<Texture> Upload(const unsigned char* pixels, unsigned width, unsigned height);
    bool ResetDevice();
};
