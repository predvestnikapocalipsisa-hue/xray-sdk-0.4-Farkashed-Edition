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
        bool editor = false;
        bool connected = false;
        HANDLE editorMarker = nullptr;
        HANDLE processMarker = nullptr;
        HANDLE publisherMutex = nullptr;
        ULONGLONG nextPoll = 0;
        ULONGLONG lastSend = 0;
        std::string lastEnvironment;
        std::string lastDocument;

        static std::wstring ProcessMarkerName(DWORD process)
        {
            return L"Local\\FarkashedSDK.Discord.Editor." + std::to_wstring(process);
        }

        static bool MarkerExists(const wchar_t* name)
        {
            HANDLE marker = OpenMutexW(SYNCHRONIZE, FALSE, name);
            if (!marker)
                return false;
            CloseHandle(marker);
            return true;
        }

        void Disconnect()
        {
            if (!connected)
                return;
            // Close the old connection before another SDK process acquires RPC.
            Discord_Shutdown();
            ReleaseMutex(publisherMutex);
            connected = false;
            lastSend = 0;
            lastEnvironment.clear();
            lastDocument.clear();
        }

    public:
        void Initialize(bool isEditor = false)
        {
            if (initialized)
                return;
            editor = isEditor;
            publisherMutex = CreateMutexW(nullptr, FALSE, L"Local\\FarkashedSDK.Discord.Publisher");
            if (!publisherMutex)
                return;
            if (editor)
            {
                // Unowned mutex handles act as process-lifetime markers. Windows
                // removes them on normal exit and on a crash, without stale flags.
                editorMarker = CreateMutexW(nullptr, FALSE, L"Local\\FarkashedSDK.Discord.Editors");
                processMarker = CreateMutexW(nullptr, FALSE, ProcessMarkerName(GetCurrentProcessId()).c_str());
                if (!editorMarker || !processMarker)
                {
                    Shutdown();
                    return;
                }
            }
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
            DWORD foregroundProcess = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
            const bool anotherEditorForeground = foregroundProcess != GetCurrentProcessId() &&
                MarkerExists(ProcessMarkerName(foregroundProcess).c_str());
            const bool eligible = editor ? !anotherEditorForeground :
                !MarkerExists(L"Local\\FarkashedSDK.Discord.Editors");
            if (!eligible)
            {
                Disconnect();
                return;
            }
            bool justConnected = false;
            if (!connected)
            {
                const DWORD result = WaitForSingleObject(publisherMutex, 0);
                if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
                    return;
                DiscordEventHandlers handlers = {};
                // Do not register join commands for the launcher/editor host.
                Discord_Initialize(ApplicationId, &handlers, 0, nullptr);
                connected = justConnected = true;
            }
            Discord_RunCallbacks();
            const std::string details = LimitText(environment);
            const std::string state = LimitText(document);
            // Keep the selected editor visible when focus moves to Discord or
            // the launcher. Only its connection can publish document updates.
            if (justConnected || details != lastEnvironment ||
                state != lastDocument || now - lastSend >= 15000)
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
        }

        void Shutdown()
        {
            Disconnect();
            if (processMarker)
                CloseHandle(processMarker);
            if (editorMarker)
                CloseHandle(editorMarker);
            if (publisherMutex)
                CloseHandle(publisherMutex);
            processMarker = editorMarker = publisherMutex = nullptr;
            initialized = false;
            nextPoll = lastSend = 0;
            std::string().swap(lastEnvironment);
            std::string().swap(lastDocument);
        }
        ~Presence() { Shutdown(); }
    };
}
