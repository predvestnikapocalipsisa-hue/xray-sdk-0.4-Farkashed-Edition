#pragma once
#include <windows.h>
#include <string>
#include <algorithm>
#include "../Editors/XrEUI/discord_rpc.h"

namespace FarkashedDiscord
{
    constexpr const char* ApplicationId = "1542531211602169966";

    inline std::string AnsiToUtf8(const char* text)
    {
        if (!text || !*text)
            return {};
        const int wideSize = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
        if (!wideSize)
            return {};
        std::wstring wide(size_t(wideSize), L'\0');
        if (!MultiByteToWideChar(CP_ACP, 0, text, -1, &wide[0], wideSize))
            return {};
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wideSize - 1,
            nullptr, 0, nullptr, nullptr);
        if (!size)
            return {};
        std::string result(size_t(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wideSize - 1,
            &result[0], size, nullptr, nullptr);
        return result;
    }

    inline std::string LimitText(const std::string& text)
    {
        size_t length = (std::min)(text.size(), size_t(127));
        if (length < text.size())
            while (length && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80)
                --length;
        return text.substr(0, length);
    }

    class Presence
    {
        bool initialized = false;
        bool wasForeground = false;
        ULONGLONG nextPoll = 0;
        ULONGLONG lastSend = 0;
        std::string lastEnvironment;
        std::string lastDocument;
    public:
        void Initialize()
        {
            if (initialized)
                return;
            DiscordEventHandlers handlers = {};
            // Do not register join commands for the launcher/editor host.
            Discord_Initialize(ApplicationId, &handlers, 0, nullptr);
            initialized = true;
        }

        void Tick(const std::string& environment, const std::string& document)
        {
            if (!initialized)
                return;
            const ULONGLONG now = GetTickCount64();
            if (now < nextPoll)
                return;
            nextPoll = now + 250;
            Discord_RunCallbacks();
            DWORD foregroundProcess = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
            const bool foreground = foregroundProcess == GetCurrentProcessId();
            const std::string details = LimitText(environment);
            const std::string state = LimitText(document);
            // Background launchers/editors must not overwrite the active editor.
            if (foreground && (!wasForeground || details != lastEnvironment ||
                state != lastDocument || now - lastSend >= 15000))
            {
                DiscordRichPresence activity = {};
                // Discord supplies the first line from the application name.
                activity.details = details.c_str();
                activity.state = state.empty() ? nullptr : state.c_str();
                activity.largeImageKey = "logo";
                activity.largeImageText = "X-Ray SDK Farkashed Edition";
                Discord_UpdatePresence(&activity);
                lastEnvironment = details;
                lastDocument = state;
                lastSend = now;
            }
            wasForeground = foreground;
        }

        void Shutdown()
        {
            if (initialized)
                Discord_Shutdown();
            initialized = false;
            wasForeground = false;
            nextPoll = lastSend = 0;
            std::string().swap(lastEnvironment);
            std::string().swap(lastDocument);
        }
        ~Presence() { Shutdown(); }
    };
}
