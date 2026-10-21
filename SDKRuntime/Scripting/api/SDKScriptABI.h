#pragma once

// SDKRuntime.dll exports this cdecl function (x64 SDK scripting distribution).
// All strings are UTF-8. Reply: {"ok": JSON_value} or {"error": "message"}.
// The returned pointer belongs to the calling thread and is valid until its
// next bridge call. Copy it before making another call. Do not free it.
// Calls involving editor state must run on the editor/UI thread.
// An ABI consumer should query "context" and check api_version == 1 first.
typedef const char* (__cdecl* SDKScriptCallFunction)(
    const char* operation, const char* text, double x, double y, double z);

// Load dynamically, avoiding Python or C++ class layout dependencies:
// auto call = reinterpret_cast<SDKScriptCallFunction>(
//     GetProcAddress(GetModuleHandleW(L"SDKRuntime.dll"), "SDKScriptCall"));
