#pragma once

#include "RuntimeSetup.h"
#include "../../Launcher/RecentFiles.h"
#include <winver.h>
#pragma comment(lib, "version.lib")

namespace SDKExternalEngine
{
    struct Profile
    {
        std::wstring executable;
        std::wstring directory;
        std::wstring arguments;
        std::wstring resources;
        bool sceneBridge = false;
    };

    inline std::wstring SettingKey(std::wstring sdk)
    {
        for (auto& c : sdk)
            if (c == L'/') c = L'\\';
        while (!sdk.empty() && sdk.back() == L'\\') sdk.pop_back();
        if (!sdk.empty()) CharLowerBuffW(&sdk[0], DWORD(sdk.size()));
        unsigned long long hash = 14695981039346656037ull;
        for (const wchar_t c : sdk)
            hash = (hash ^ c) * 1099511628211ull;
        return L"InGameExternalEngine_" + std::to_wstring(hash);
    }

    inline Profile Load(const std::wstring& sdk)
    {
        const auto value = FarkashedLauncher::Setting(SettingKey(sdk).c_str());
        std::wstring fields[4];
        size_t offset = 0;
        for (auto& field : fields)
        {
            const auto separator = value.find(L'|', offset);
            if (separator == std::wstring::npos) return {};
            field = value.substr(offset, separator - offset);
            offset = separator + 1;
        }
        return {fields[0], fields[1], value.substr(offset), fields[2], fields[3] == L"1"};
    }

    inline bool Save(const std::wstring& sdk, const Profile& profile)
    {
        const auto value = profile.executable.empty() ? std::wstring() :
            profile.executable + L"|" + profile.directory + L"|" + profile.resources + L"|" +
            (profile.sceneBridge ? L"1|" : L"0|") + profile.arguments;
        return FarkashedLauncher::SetSetting(SettingKey(sdk).c_str(), value);
    }

    inline std::wstring DefaultDirectory(const std::wstring& executable)
    {
        using namespace SDKInGameSetup;
        const auto bin = Parent(executable);
        for (auto dir = bin; !dir.empty(); dir = Parent(dir))
            if (File(dir + L"\\fsgame.ltx")) return dir;
        return bin;
    }

    inline bool IsDirectory(const std::wstring& path)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
    }

    inline bool HasIXRayName(std::wstring name)
    {
        if (!name.empty()) CharLowerBuffW(&name[0], DWORD(name.size()));
        return name.find(L"ix-ray") != std::wstring::npos || name.find(L"ixray") != std::wstring::npos ||
            name.find(L"ix ray") != std::wstring::npos;
    }

    inline bool IsIXRayBinary(const std::wstring& path)
    {
        if (HasIXRayName(path.substr(path.find_last_of(L"\\/") + 1))) return true;
        DWORD ignored = 0;
        const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
        if (!size) return false;
        std::vector<BYTE> info(size);
        if (!GetFileVersionInfoW(path.c_str(), 0, size, info.data())) return false;
        struct Translation { WORD language, codepage; };
        LPVOID values = nullptr;
        UINT bytes = 0;
        if (!VerQueryValueW(info.data(), L"\\VarFileInfo\\Translation", &values, &bytes)) return false;
        const auto* translations = static_cast<const Translation*>(values);
        for (size_t i = 0; i < bytes / sizeof(Translation); ++i)
            for (const auto* field : {L"ProductName", L"CompanyName", L"FileDescription", L"InternalName", L"OriginalFilename"})
            {
                wchar_t query[100];
                swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\%ls", unsigned(translations[i].language),
                    unsigned(translations[i].codepage), field);
                LPVOID value = nullptr;
                UINT length = 0;
                if (VerQueryValueW(info.data(), query, &value, &length) && length &&
                    HasIXRayName(static_cast<const wchar_t*>(value))) return true;
            }
        return false;
    }

    inline bool IsIXRay(const std::wstring& executable)
    {
        const auto bin = SDKInGameSetup::Parent(executable);
        for (const auto& binary : {executable, bin + L"\\xrEngineCore.dll", bin + L"\\xrCore.dll"})
        {
            if (IsIXRayBinary(binary)) return true;
            // Release builds can retain vanilla X-Ray version resources.
            std::ifstream file(binary.c_str(), std::ios::binary);
            char block[65536];
            std::string bytes;
            while (file.read(block, sizeof(block)) || file.gcount())
            {
                bytes.append(block, size_t(file.gcount()));
                if (bytes.find("IX-Ray Engine") != std::string::npos ||
                    bytes.find("ixray_settings\\default_settings") != std::string::npos ||
                    bytes.find("ixray-1.6-stcop") != std::string::npos ||
                    bytes.find("ixray-1.5-stcs") != std::string::npos) return true;
                bytes.erase(0, bytes.size() > 40 ? bytes.size() - 40 : 0);
            }
        }
        return false;
    }

    inline const wchar_t* IXRayMessage()
    {
        return L"\u041D\u0435 \u043F\u043E\u0437\u043E\u0440\u044C\u0441\u044F, \u0431\u0435\u0437\u0434\u0430\u0440\u044C.";
    }

}
