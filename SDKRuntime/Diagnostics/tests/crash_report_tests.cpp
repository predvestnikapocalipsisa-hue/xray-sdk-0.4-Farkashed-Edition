#include "../CrashReport.h"
#include <fstream>
#include <iostream>
#include <string>

static int Capture(EXCEPTION_POINTERS* info)
{
    SDKCrashReport::Write(info);
    return EXCEPTION_EXECUTE_HANDLER;
}
static void Fault()
{
    __try { RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr); }
    __except (Capture(GetExceptionInformation())) {}
}
int wmain()
{
    wchar_t temporary[MAX_PATH]{};
    GetTempPathW(_countof(temporary), temporary);
    const auto directory = std::wstring(temporary) + L"SDK crash report test " + std::to_wstring(GetCurrentProcessId());
    if (!SDKCrashReport::Initialize(directory.c_str())) return 1;
    Fault();
    WIN32_FIND_DATAW item{};
    HANDLE search = FindFirstFileW((directory + L"\\*.txt").c_str(), &item);
    if (search == INVALID_HANDLE_VALUE) return 2;
    FindClose(search);
    std::ifstream text((directory + L"\\" + item.cFileName).c_str());
    const std::string contents{std::istreambuf_iterator<char>(text), std::istreambuf_iterator<char>()};
    if (contents.find("0xC0000005") == std::string::npos || contents.find("Module offset:") == std::string::npos) return 3;
    search = FindFirstFileW((directory + L"\\*.dmp").c_str(), &item);
    if (search == INVALID_HANDLE_VALUE) return 4;
    FindClose(search);
    std::ifstream dump((directory + L"\\" + item.cFileName).c_str(), std::ios::binary);
    char signature[4]{};
    dump.read(signature, sizeof(signature));
    if (memcmp(signature, "MDMP", 4)) return 5;
    std::cout << "PASS: exception code/module report and readable minidump without SDK logging\n";
    std::wcout << directory << L"\n";
}
