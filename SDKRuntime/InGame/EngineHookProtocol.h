#pragma once

#include <windows.h>

namespace SDKEngineHook
{
    constexpr GUID PayloadId{0xacec6876, 0xd62f, 0x4b55, {0xbe,0x3b,0xb7,0x17,0x71,0x35,0x6d,0xd2}};
    struct Payload
    {
        wchar_t gameData[32768];
        wchar_t resources[32768];
        wchar_t ready[128];
    };

    using Launch = BOOL (WINAPI*)(LPCWSTR, LPWSTR, LPCWSTR, LPCSTR, const Payload*, PROCESS_INFORMATION*);
}
