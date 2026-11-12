#pragma once
#include <windows.h>
#include <string>
#include <algorithm>
#include <ctime>
#include "DiscordIPC.h"

namespace FarkashedDiscord
{

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

    class Presence
    {
        bool initialized = false;
        bool editor = false;
        bool connected = false;
        HANDLE editorMarker = nullptr;
        HANDLE processMarker = nullptr;
        HANDLE publisherMutex = nullptr;
        ULONGLONG nextPoll = 0;
        IPC ipc;
        Settings settings;
        ULONGLONG nextSettingsPoll = 0;
        int64_t sessionStart = 0;

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
            ipc.Close();
            ReleaseMutex(publisherMutex);
            connected = false;
        }

    public:
        const char* Status() const
        {
            if (!settings.enabled) return "Discord RPC is disabled.";
            if (!connected) return "Waiting for this SDK process to publish activity.";
            return ipc.Status();
        }

        void Initialize(bool isEditor = false)
        {
            if (initialized)
                return;
            editor = isEditor;
            LoadSettings(settings);
            sessionStart = static_cast<int64_t>(std::time(nullptr));
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
            if (now >= nextSettingsPoll)
            {
                LoadSettings(settings); // Keep the last valid configuration on read failure.
                nextSettingsPoll = now + 1000;
            }
            if (!settings.enabled)
            {
                Disconnect();
                return;
            }
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
            if (!connected)
            {
                const DWORD result = WaitForSingleObject(publisherMutex, 0);
                if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
                    return;
                connected = true;
            }
            // Keep one publisher across the launcher and all SDK editors.
            const Settings active = settings.custom ? settings : Settings();
            ipc.Update(ActivityJson(active, environment, document, sessionStart));
            ipc.Tick();
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
            nextPoll = 0;
            nextSettingsPoll = 0;
        }
        ~Presence() { Shutdown(); }
    };
}
