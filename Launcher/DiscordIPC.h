#pragma once
#include "DiscordSettings.h"
#include <vector>
#include <cstring>
#include <algorithm>

namespace FarkashedDiscord
{
    inline std::string JsonString(const std::string& text)
    {
        std::string out = "\"";
        const char* hex = "0123456789abcdef";
        for (unsigned char c : text)
        {
            if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
            else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
            else out += char(c);
        }
        return out + '"';
    }

    inline std::string LimitText(const std::string& text)
    {
        size_t length = (std::min)(text.size(), size_t(127));
        if (length < text.size())
            while (length && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80) --length;
        return text.substr(0, length);
    }

    inline std::string JsonObject(std::initializer_list<std::pair<const char*, std::string>> fields)
    {
        std::string out = "{";
        for (const auto& field : fields)
        {
            if (field.second.empty()) continue;
            if (out.size() > 1) out += ',';
            out += JsonString(field.first) + ':' + JsonString(field.second);
        }
        return out + '}';
    }

    inline std::string ActivityJson(const Settings& s, const std::string& environment,
        const std::string& document, int64_t sessionStart)
    {
        std::string out = "{\"type\":" + std::to_string(s.type);
        auto field = [&](const char* key, const std::string& value)
        {
            if (!value.empty()) out += ',' + JsonString(key) + ':' + JsonString(value);
        };
        if (s.custom) field("name", LimitText(Expand(s.name, environment, document)));
        field("details", LimitText(Expand(s.details, environment, document)));
        field("state", LimitText(Expand(s.state, environment, document)));
        const int64_t start = s.timer == 1 ? sessionStart : s.timer == 2 ? s.start : 0;
        const int64_t end = s.timer == 2 ? s.end : 0;
        if (start || end)
        {
            out += ",\"timestamps\":{";
            if (start) out += "\"start\":" + std::to_string(start);
            if (end) out += std::string(start ? "," : "") + "\"end\":" + std::to_string(end);
            out += '}';
        }
        if (!s.largeImageKey.empty() || !s.smallImageKey.empty())
            out += ",\"assets\":" + JsonObject({ { "large_image", s.largeImageKey },
                { "large_text", s.largeImageKey.empty() ? "" : s.largeImageText },
                { "small_image", s.smallImageKey }, { "small_text", s.smallImageKey.empty() ? "" : s.smallImageText } });
        if (!s.partyId.empty())
        {
            out += ",\"party\":{\"id\":" + JsonString(s.partyId);
            if (s.partyMax) out += ",\"size\":[" + std::to_string(s.partySize) + ',' + std::to_string(s.partyMax) + ']';
            out += '}';
        }
        if (!s.joinSecret.empty() || !s.matchSecret.empty() || !s.spectateSecret.empty())
        {
            out += ",\"secrets\":" + JsonObject({ { "join", s.joinSecret },
                { "match", s.matchSecret }, { "spectate", s.spectateSecret } });
        }
        bool button = false;
        for (int i = 0; i < 2; ++i)
            if (!s.buttonLabel[i].empty() && !s.buttonUrl[i].empty())
            {
                out += button ? "," : ",\"buttons\":[";
                out += "{\"label\":" + JsonString(s.buttonLabel[i]) + ",\"url\":" + JsonString(s.buttonUrl[i]) + '}';
                button = true;
            }
        if (button) out += ']';
        out += std::string(",\"instance\":") + (s.instance ? "true}" : "false}");
        return out;
    }

    // All pipe I/O is overlapped. Polling never waits for Discord on the UI thread.
    class IPC
    {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        OVERLAPPED read = {}, write = {};
        bool reading = false, writing = false, ready = false;
        char readBuffer[8192] = {};
        std::vector<char> incoming, outgoing;
        std::string desired, sent;
        std::string status = "Waiting for Discord desktop client.";
        std::vector<std::string> pongs;
        ULONGLONG retryAt = 0, handshakeDeadline = 0;
        ULONGLONG writeDeadline = 0;
        uint64_t nonce = 0;
        std::wstring pipePrefix;

        bool Send(uint32_t opcode, const std::string& json)
        {
            if (writing || json.size() > 65536) return false;
            outgoing.resize(8 + json.size());
            const uint32_t length = static_cast<uint32_t>(json.size());
            std::memcpy(outgoing.data(), &opcode, 4);
            std::memcpy(outgoing.data() + 4, &length, 4);
            std::memcpy(outgoing.data() + 8, json.data(), length);
            ResetEvent(write.hEvent);
            writeDeadline = GetTickCount64() + 3000;
            DWORD count = 0;
            if (WriteFile(pipe, outgoing.data(), static_cast<DWORD>(outgoing.size()), &count, &write))
                return count == outgoing.size();
            writing = GetLastError() == ERROR_IO_PENDING;
            return writing;
        }

        bool Receive()
        {
            DWORD count = 0;
            if (!reading)
            {
                ResetEvent(read.hEvent);
                if (!ReadFile(pipe, readBuffer, sizeof(readBuffer), &count, &read))
                {
                    if (GetLastError() != ERROR_IO_PENDING) return false;
                    reading = true;
                    return true;
                }
            }
            else
            {
                if (!GetOverlappedResult(pipe, &read, &count, FALSE)) return GetLastError() == ERROR_IO_INCOMPLETE;
                reading = false;
            }
            if (!count) return false;
            incoming.insert(incoming.end(), readBuffer, readBuffer + count);
            while (incoming.size() >= 8)
            {
                uint32_t opcode = 0, length = 0;
                std::memcpy(&opcode, incoming.data(), 4);
                std::memcpy(&length, incoming.data() + 4, 4);
                if (length > 65536) return false;
                if (incoming.size() < 8 + length) break;
                const std::string payload(incoming.data() + 8, length);
                if (opcode == 2) return false;
                if (opcode == 3)
                {
                    if (pongs.size() >= 16) return false;
                    pongs.push_back(payload);
                }
                if (opcode == 1)
                {
                    // Only the event field is needed; skip JSON whitespace outside strings.
                    std::string compact;
                    bool quoted = false, escaped = false;
                    for (char c : payload)
                    {
                        if (quoted || (c != ' ' && c != '\r' && c != '\n' && c != '\t')) compact += c;
                        if (c == '"' && !escaped) quoted = !quoted;
                        if (c == '\\' && !escaped) escaped = true; else escaped = false;
                    }
                    if (compact.find("\"evt\":\"READY\"") != std::string::npos)
                    {
                        ready = true;
                        status = "Connected to Discord.";
                    }
                    if (compact.find("\"evt\":\"ERROR\"") != std::string::npos)
                        status = "Discord rejected the activity: " + payload;
                    else if (compact.find("\"cmd\":\"SET_ACTIVITY\"") != std::string::npos)
                        status = "Activity accepted by Discord.";
                }
                incoming.erase(incoming.begin(), incoming.begin() + 8 + length);
            }
            return true;
        }

    public:
        explicit IPC(const std::wstring& prefix = L"\\\\.\\pipe\\discord-ipc-") : pipePrefix(prefix) {}
        IPC(const IPC&) = delete;
        IPC& operator=(const IPC&) = delete;
        ~IPC() { Close(); }

        void Close()
        {
            if (pipe != INVALID_HANDLE_VALUE)
            {
                CancelIoEx(pipe, nullptr);
                DWORD ignored = 0;
                if (reading) GetOverlappedResult(pipe, &read, &ignored, TRUE);
                if (writing) GetOverlappedResult(pipe, &write, &ignored, TRUE);
                CloseHandle(pipe);
            }
            if (read.hEvent) CloseHandle(read.hEvent);
            if (write.hEvent) CloseHandle(write.hEvent);
            pipe = INVALID_HANDLE_VALUE;
            read = {}; write = {};
            reading = writing = ready = false;
            incoming.clear(); outgoing.clear(); sent.clear();
            pongs.clear();
            retryAt = GetTickCount64() + 3000;
            status = "Waiting for Discord desktop client (automatic reconnect).";
        }

        void Update(const std::string& json) { desired = json; }
        const char* Status() const { return status.c_str(); }

        void Tick()
        {
            const auto now = GetTickCount64();
            if (pipe == INVALID_HANDLE_VALUE)
            {
                if (now < retryAt) return;
                retryAt = now + 3000;
                for (int i = 0; i < 10; ++i)
                {
                    const auto name = pipePrefix + std::to_wstring(i);
                    pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                        nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
                    if (pipe != INVALID_HANDLE_VALUE) break;
                }
                if (pipe == INVALID_HANDLE_VALUE) return;
                read.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                write.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                handshakeDeadline = now + 10000;
                if (!read.hEvent || !write.hEvent ||
                    !Send(0, std::string("{\"v\":1,\"client_id\":") + JsonString(ApplicationId) + '}')) { Close(); return; }
            }
            if (writing)
            {
                DWORD count = 0;
                if (GetOverlappedResult(pipe, &write, &count, FALSE))
                {
                    writing = false;
                    if (count != outgoing.size()) { Close(); return; }
                }
                else if (GetLastError() != ERROR_IO_INCOMPLETE || now >= writeDeadline) { Close(); return; }
            }
            if ((!ready && now >= handshakeDeadline) || !Receive()) { Close(); return; }
            if (!writing && !pongs.empty())
            {
                if (!Send(4, pongs.front())) { Close(); return; }
                pongs.erase(pongs.begin());
            }
            if (ready && !writing && !desired.empty() && desired != sent)
            {
                const auto command = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" +
                    std::to_string(GetCurrentProcessId()) + ",\"activity\":" + desired +
                    "},\"nonce\":" + JsonString(std::to_string(++nonce)) + '}';
                if (!Send(1, command)) { Close(); return; }
                sent = desired;
            }
        }
    };
}
