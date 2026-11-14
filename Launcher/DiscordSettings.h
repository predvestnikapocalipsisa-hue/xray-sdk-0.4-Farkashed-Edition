#pragma once
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <cstdint>
#include <cstring>
#pragma comment(lib, "shell32.lib")

namespace FarkashedDiscord
{
    constexpr const char* ApplicationId = "1542531211602169966";
    constexpr size_t ImageUrlMaxBytes = 2048;

    enum class ImageSource { None, AssetKey, ExternalUrl };

    struct Settings
    {
        bool enabled = true;
        bool custom = false;
        std::string name = "X-Ray SDK Farkashed Edition";
        bool showNameInDetails = false;
        int type = 0;
        std::string details = "{environment}";
        std::string state = "{document}";
        std::string largeImageKey = "logo";
        std::string largeImageText = "X-Ray SDK Farkashed Edition";
        std::string smallImageKey, smallImageText;
        ImageSource largeImageSource = ImageSource::AssetKey;
        ImageSource smallImageSource = ImageSource::None;
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

    inline size_t ImageUrlPrefixLength(const std::string& value)
    {
        if (_strnicmp(value.c_str(), "https://", 8) == 0) return 8;
        if (_strnicmp(value.c_str(), "http://", 7) == 0) return 7;
        return 0;
    }

    inline ImageSource InferImageSource(const std::string& value)
    {
        if (value.empty()) return ImageSource::None;
        return ImageUrlPrefixLength(value) ? ImageSource::ExternalUrl : ImageSource::AssetKey;
    }

    inline const char* ValidateImage(ImageSource source, const std::string& value)
    {
        if (source == ImageSource::None) return nullptr;
        if (source == ImageSource::ExternalUrl)
        {
            const size_t host = ImageUrlPrefixLength(value);
            if (!host || host == value.size() || value[host] == '/' || value[host] == '?' || value[host] == '#' ||
                value.find_first_of(" \t\r\n\\") != std::string::npos || value.find('\0') != std::string::npos ||
                value.size() > ImageUrlMaxBytes)
                return "enter a public HTTP(S) image URL (up to 2048 bytes).";
            return nullptr;
        }
        if (value.empty() || value.size() > 512 || value.find_first_of(" \t\r\n:/\\") != std::string::npos ||
            value.find('\0') != std::string::npos)
            return "enter a Discord asset key or choose External URL.";
        return nullptr;
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
        if (const auto error = ValidateImage(s.largeImageSource, s.largeImageKey))
            return std::string("Large image: ") + error;
        if (const auto error = ValidateImage(s.smallImageSource, s.smallImageKey))
            return std::string("Small image: ") + error;
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
        f << "v3\n" << s.enabled << ' ' << s.custom << '\n';
        const std::string* values[] = { &s.name, &s.details, &s.state,
            &s.largeImageKey, &s.largeImageText, &s.smallImageKey, &s.smallImageText,
            &s.partyId, &s.matchSecret, &s.joinSecret, &s.spectateSecret,
            &s.buttonLabel[0], &s.buttonUrl[0], &s.buttonLabel[1], &s.buttonUrl[1] };
        for (const auto* value : values) f << std::quoted(*value) << '\n';
        f << s.type << ' ' << s.timer << ' ' << s.start << ' ' << s.end << ' '
            << s.partySize << ' ' << s.partyMax << ' ' << s.instance << ' ' << s.showNameInDetails << '\n';
        f << static_cast<int>(s.largeImageSource) << ' ' << static_cast<int>(s.smallImageSource) << '\n';
        return f.str();
    }

    inline bool Parse(std::istream& f, Settings& result)
    {
        Settings s;
        std::string tag;
        if (!(f >> tag) || (tag != "v1" && tag != "v2" && tag != "v3") || !(f >> s.enabled >> s.custom)) return false;
        std::string* values[] = { &s.name, &s.details, &s.state,
            &s.largeImageKey, &s.largeImageText, &s.smallImageKey, &s.smallImageText,
            &s.partyId, &s.matchSecret, &s.joinSecret, &s.spectateSecret,
            &s.buttonLabel[0], &s.buttonUrl[0], &s.buttonLabel[1], &s.buttonUrl[1] };
        for (auto* value : values) if (!(f >> std::quoted(*value))) return false;
        if (!(f >> s.type >> s.timer >> s.start >> s.end >> s.partySize >> s.partyMax >> s.instance)) return false;
        if (tag != "v1" && !(f >> s.showNameInDetails)) return false;
        if (tag == "v3")
        {
            int largeSource = 0, smallSource = 0;
            if (!(f >> largeSource >> smallSource) || largeSource < 0 || largeSource > 2 || smallSource < 0 || smallSource > 2) return false;
            s.largeImageSource = static_cast<ImageSource>(largeSource);
            s.smallImageSource = static_cast<ImageSource>(smallSource);
        }
        else
        {
            s.largeImageSource = InferImageSource(s.largeImageKey);
            s.smallImageSource = InferImageSource(s.smallImageKey);
            if (ValidateImage(s.largeImageSource, s.largeImageKey)) s.largeImageSource = ImageSource::None;
            if (ValidateImage(s.smallImageSource, s.smallImageKey)) s.smallImageSource = ImageSource::None;
        }
        if (!Validate(s).empty()) return false;
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
