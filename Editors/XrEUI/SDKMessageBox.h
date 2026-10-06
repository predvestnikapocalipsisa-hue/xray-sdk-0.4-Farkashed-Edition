#pragma once
#include <windows.h>
#if defined(XR_IMGUI_STANDALONE)
#define SDK_DIALOG_API
#elif defined(XREUI_API)
#define SDK_DIALOG_API XREUI_API
#elif defined(XR_SDK_RUNTIME_BUILD)
#define SDK_DIALOG_API __declspec(dllexport)
#else
#define SDK_DIALOG_API __declspec(dllimport)
#endif

// Windows button/result constants are retained for existing synchronous callers.
namespace SDKDialogs
{
    SDK_DIALOG_API int Show(HWND owner, const char* text, const char* title, UINT flags,
        const char* const* captions = nullptr);
    SDK_DIALOG_API int ShowWide(HWND owner, const wchar_t* text, const wchar_t* title, UINT flags);
    SDK_DIALOG_API void Notify(const char* text, const char* title, UINT flags);
    void Pump();
    void Initialize(HWND owner);
    void Shutdown();
    bool HandleMessage(UINT message, LPARAM parameter, LRESULT& result);
}
