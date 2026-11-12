#pragma once
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <cstdint>
#pragma comment(lib, "shell32.lib")

namespace FarkashedDiscord
{
    constexpr const char* ApplicationId = "1542531211602169966";

    struct Settings
    {
        bool enabled = true;
        bool custom = false;
        std::string name = "X-Ray SDK Farkashed Edition";
        int type = 0;
        std::string details = "{environment}";
        std::string state = "{document}";
        std::string largeImageKey = "logo";
        std::string largeImageText = "X-Ray SDK Farkashed Edition";
        std::string smallImageKey, smallImageText;
        int timer = 0; // Hidden, elapsed session time, explicit Unix timestamps.
        int64_t start = 0, end = 0;
        std::string partyId;
        int partySize = 0, partyMax = 0;
        std::string matchSecret, joinSecret, spectateSecret;
        bool instance = false;
        std::string buttonLabel[2], buttonUrl[2];
    };

    inline std::wstring SettingsPath()
    {
        wchar_t documents[MAX_PATH] = {};
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL | CSIDL_FLAG_CREATE,
            nullptr, SHGFP_TYPE_CURRENT, documents))) return {};
        const std::wstring directory = std::wstring(documents) + L"\\X-Ray SDK";
        if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return {};
        return directory + L"\\discord.cfg";
    }

    inline std::string Validate(const Settings& s)
    {
        if (!s.custom) return {};
        if (s.name.empty()) return "Activity name cannot be empty.";
        if (s.type != 0 && s.type != 2 && s.type != 3 && s.type != 5) return "Invalid activity type.";
        if (s.timer < 0 || s.timer > 2 || s.start < 0 || s.end < 0 ||
            (s.timer == 2 && s.end && s.end <= s.start))
            return "Use nonnegative Unix timestamps; end must be after start.";
        if (s.partySize < 0 || s.partyMax < 0 || s.partySize > s.partyMax ||
            (s.partyMax && s.partyId.empty()))
            return "Party needs an ID and 0 <= size <= maximum.";
        const std::string* text[] = { &s.name, &s.details, &s.state, &s.largeImageText,
            &s.smallImageText, &s.partyId, &s.matchSecret, &s.joinSecret, &s.spectateSecret };
        for (const auto* value : text)
            if (value->size() > 127 || value->find_first_of("\r\n") != std::string::npos ||
                value->find('\0') != std::string::npos) return "Text fields must fit 127 UTF-8 bytes on one line.";
        if (s.largeImageKey.size() > 512 || s.smallImageKey.size() > 512)
            return "Image keys / URLs must fit 512 UTF-8 bytes.";
        for (int i = 0; i < 2; ++i)
        {
            if (s.buttonLabel[i].empty() != s.buttonUrl[i].empty()) return "Each button needs both a label and URL.";
            if (s.buttonLabel[i].size() > 32 || s.buttonUrl[i].size() > 512) return "Button label: max 32 bytes; URL: max 512 bytes.";
            if (!s.buttonUrl[i].empty() && s.buttonUrl[i].find("https://") != 0 && s.buttonUrl[i].find("http://") != 0)
                return "Button URLs must start with https:// or http://.";
        }
        return {};
    }

    inline std::string Serialize(const Settings& s)
    {
        std::ostringstream f;
        f << "v1\n" << s.enabled << ' ' << s.custom << '\n';
        const std::string* values[] = { &s.name, &s.details, &s.state,
            &s.largeImageKey, &s.largeImageText, &s.smallImageKey, &s.smallImageText,
            &s.partyId, &s.matchSecret, &s.joinSecret, &s.spectateSecret,
            &s.buttonLabel[0], &s.buttonUrl[0], &s.buttonLabel[1], &s.buttonUrl[1] };
        for (const auto* value : values) f << std::quoted(*value) << '\n';
        f << s.type << ' ' << s.timer << ' ' << s.start << ' ' << s.end << ' '
            << s.partySize << ' ' << s.partyMax << ' ' << s.instance << '\n';
        return f.str();
    }

    inline bool Parse(std::istream& f, Settings& result)
    {
        Settings s;
        std::string tag;
        if (!(f >> tag) || tag != "v1" || !(f >> s.enabled >> s.custom)) return false;
        std::string* values[] = { &s.name, &s.details, &s.state,
            &s.largeImageKey, &s.largeImageText, &s.smallImageKey, &s.smallImageText,
            &s.partyId, &s.matchSecret, &s.joinSecret, &s.spectateSecret,
            &s.buttonLabel[0], &s.buttonUrl[0], &s.buttonLabel[1], &s.buttonUrl[1] };
        for (auto* value : values) if (!(f >> std::quoted(*value))) return false;
        if (!(f >> s.type >> s.timer >> s.start >> s.end >> s.partySize >> s.partyMax >> s.instance) ||
            !Validate(s).empty()) return false;
        f >> std::ws;
        if (!f.eof()) return false;
        result = s;
        return true;
    }

    inline bool LoadSettings(Settings& s)
    {
        const auto path = SettingsPath();
        if (path.empty()) return false;
        std::ifstream f(path.c_str(), std::ios::binary);
        return f && Parse(f, s);
    }

    inline bool SaveSettings(const Settings& s)
    {
        if (!Validate(s).empty()) return false;
        const auto path = SettingsPath();
        if (path.empty()) return false;
        const auto temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        std::ofstream f(temporary.c_str(), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        const auto data = Serialize(s);
        f.write(data.data(), data.size());
        f.close();
        if (f && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        DeleteFileW(temporary.c_str());
        return false;
    }

    inline std::string Expand(const std::string& text, const std::string& environment, const std::string& document)
    {
        std::string result;
        for (size_t i = 0; i < text.size();)
        {
            if (text.compare(i, 13, "{environment}") == 0) { result += environment; i += 13; }
            else if (text.compare(i, 10, "{document}") == 0) { result += document; i += 10; }
            else result += text[i++];
        }
        return result;
    }
}
