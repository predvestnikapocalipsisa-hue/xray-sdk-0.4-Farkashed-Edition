#pragma once
#include <string>

namespace SDKScript
{
    using Backend = std::string(*)(const std::string&, const std::string&, double, double, double);
    ECORE_API void SetBackend(Backend backend);
    ECORE_API void Draw();
    ECORE_API void Tick();
    ECORE_API void Shutdown();
    ECORE_API void CommandEvent(const char* name, bool before);
    ECORE_API std::string Quote(const std::string& text);
    ECORE_API std::string FromUTF8(const std::string& text);
    ECORE_API std::string ToUTF8(const char* text);
}

// UTF-8, JSON result, UI-thread-only. The Python wrapper raises native errors.
extern "C" ECORE_API const char* __cdecl SDKScriptCall(const char* operation, const char* text, double x, double y, double z);
