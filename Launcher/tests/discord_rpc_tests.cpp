#include "../DiscordPresence.h"
#include <atomic>
#include <thread>
#include <iostream>
#include <stdexcept>

using namespace FarkashedDiscord;

static void Check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

static void Transfer(HANDLE pipe, void* data, DWORD size, bool writing)
{
    char* cursor = static_cast<char*>(data);
    while (size)
    {
        OVERLAPPED op = {};
        op.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD count = 0;
        BOOL ok = writing ? WriteFile(pipe, cursor, size, &count, &op) : ReadFile(pipe, cursor, size, &count, &op);
        if (!ok && GetLastError() == ERROR_IO_PENDING)
        {
            if (WaitForSingleObject(op.hEvent, 5000) != WAIT_OBJECT_0) CancelIoEx(pipe, &op);
            ok = GetOverlappedResult(pipe, &op, &count, TRUE);
        }
        CloseHandle(op.hEvent);
        Check(ok && count, "mock pipe transfer failed / timed out");
        cursor += count;
        size -= count;
    }
}

static std::pair<uint32_t, std::string> ReadFrame(HANDLE pipe)
{
    uint32_t header[2] = {};
    Transfer(pipe, header, 8, false);
    Check(header[1] < 65536, "invalid client frame length");
    std::string data(header[1], '\0');
    if (!data.empty()) Transfer(pipe, &data[0], header[1], false);
    return { header[0], data };
}

static void SendFrame(HANDLE pipe, uint32_t opcode, const std::string& data, bool fragmented = false)
{
    std::vector<char> frame(8 + data.size());
    const uint32_t length = static_cast<uint32_t>(data.size());
    std::memcpy(frame.data(), &opcode, 4);
    std::memcpy(frame.data() + 4, &length, 4);
    std::memcpy(frame.data() + 8, data.data(), data.size());
    if (fragmented)
    {
        Transfer(pipe, frame.data(), 3, true);
        Sleep(25);
        Transfer(pipe, frame.data() + 3, static_cast<DWORD>(frame.size() - 3), true);
    }
    else Transfer(pipe, frame.data(), static_cast<DWORD>(frame.size()), true);
}

static void TestIPC()
{
    const std::wstring prefix = L"\\\\.\\pipe\\farkashed-discord-test-" + std::to_wstring(GetCurrentProcessId()) + L"-";
    std::atomic<bool> done(false), listening(false);
    std::string error;
    std::thread server([&]()
    {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        try
        {
            for (int attempt = 0; attempt < 2; ++attempt)
            {
                pipe = CreateNamedPipeW((prefix + L"0").c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);
                Check(pipe != INVALID_HANDLE_VALUE, "create mock pipe failed");
                OVERLAPPED connect = {};
                connect.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                const BOOL connected = ConnectNamedPipe(pipe, &connect);
                const DWORD code = connected ? ERROR_SUCCESS : GetLastError();
                listening = true;
                if (code == ERROR_IO_PENDING)
                {
                    if (WaitForSingleObject(connect.hEvent, 6000) != WAIT_OBJECT_0) CancelIoEx(pipe, &connect);
                    DWORD ignored = 0;
                    const BOOL result = GetOverlappedResult(pipe, &connect, &ignored, TRUE);
                    CloseHandle(connect.hEvent);
                    Check(result, "mock connection timed out");
                }
                else { CloseHandle(connect.hEvent); Check(connected || code == ERROR_PIPE_CONNECTED, "mock connect failed"); }
                const auto handshake = ReadFrame(pipe);
                Check(handshake.first == 0 && handshake.second == std::string("{\"v\":1,\"client_id\":\"") + ApplicationId + "\"}", "fixed Application ID handshake");
                SendFrame(pipe, 1, "{ \"evt\" : \"READY\", \"cmd\":\"DISPATCH\" }", true);
                const auto update = ReadFrame(pipe);
                Check(update.first == 1 && update.second.find("\"cmd\":\"SET_ACTIVITY\"") != std::string::npos, "SET_ACTIVITY missing");
                Check(update.second.find("\"name\":\"Custom \\\"SDK\\\"\"") != std::string::npos, "custom name / escaping missing");
                Check(update.second.find("\"pid\":" + std::to_string(GetCurrentProcessId())) != std::string::npos, "process ID missing");
                SendFrame(pipe, 3, "ping payload");
                const auto pong = ReadFrame(pipe);
                Check(pong.first == 4 && pong.second == "ping payload", "ping/pong failed");
                if (attempt == 0) SendFrame(pipe, 2, "{}");
                DisconnectNamedPipe(pipe);
                CloseHandle(pipe);
                pipe = INVALID_HANDLE_VALUE;
            }
        }
        catch (const std::exception& e) { error = e.what(); }
        if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
        done = true;
    });
    while (!listening && !done) Sleep(1);
    Settings s;
    s.custom = true;
    s.name = "Custom \"SDK\"";
    IPC ipc(prefix);
    ipc.Update(ActivityJson(s, "Level Editor", "test.level", 123));
    while (!done) { ipc.Tick(); Sleep(10); }
    server.join();
    Check(error.empty(), error.c_str());
}

static void TestLiveDiscord()
{
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int i = 0; i < 10; ++i)
    {
        const auto name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
        pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) break;
    }
    Check(pipe != INVALID_HANDLE_VALUE, "Discord desktop IPC unavailable");
    try
    {
        SendFrame(pipe, 0, std::string("{\"v\":1,\"client_id\":\"") + ApplicationId + "\"}");
        Check(ReadFrame(pipe).first == 1, "Discord handshake failed");
        Settings s;
        s.custom = true;
        s.name = "X-Ray SDK Custom RPC";
        const auto activity = ActivityJson(s, "Testing SDK IPC", "", 0);
        SendFrame(pipe, 1, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" +
            std::to_string(GetCurrentProcessId()) + ",\"activity\":" + activity + "},\"nonce\":\"sdk-test\"}");
        auto response = ReadFrame(pipe);
        while (response.first == 3) { SendFrame(pipe, 4, response.second); response = ReadFrame(pipe); }
        const bool accepted = response.first == 1 && response.second.find("X-Ray SDK Custom RPC") != std::string::npos &&
            response.second.find("\"ERROR\"") == std::string::npos;
        SendFrame(pipe, 1, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" +
            std::to_string(GetCurrentProcessId()) + ",\"activity\":null},\"nonce\":\"sdk-clear\"}");
        ReadFrame(pipe);
        Check(accepted, "Discord did not accept the custom activity name");
        CloseHandle(pipe);
        std::cout << "Live Discord accepted custom activity name with fixed Application ID; test activity cleared.\n";
    }
    catch (...) { CloseHandle(pipe); throw; }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string(argv[1]) == "--live") { TestLiveDiscord(); return 0; }
        Settings s;
        s.custom = true;
        s.name = "SDK \"custom\"";
        s.state = "Editing {document}";
        s.buttonLabel[0] = "Website";
        s.buttonUrl[0] = "https://example.com";
        std::istringstream stored(Serialize(s));
        Settings loaded;
        Check(Parse(stored, loaded) && Serialize(s) == Serialize(loaded), "settings round trip failed");
        std::istringstream broken("v1 1 1 \"truncated\"");
        const auto previous = Serialize(loaded);
        Check(!Parse(broken, loaded) && Serialize(loaded) == previous, "truncated config changed live settings");
        s.end = 10; s.start = 20; s.timer = 2;
        Check(!Validate(s).empty(), "invalid timestamps accepted");
        s.end = 30;
        s.buttonUrl[0] = "file:///bad";
        Check(!Validate(s).empty(), "invalid button URL accepted");
        Check(Expand("{environment}/{document}", "Editor", "{environment}.level") == "Editor/{environment}.level", "recursive token expansion");
        Check(JsonString("a\n\"\\") == "\"a\\u000a\\\"\\\\\"", "JSON escaping failed");
        Check(LimitText(std::string(126, 'a') + "\xd0\xaf").size() == 126, "UTF-8 truncation split a character");
        s = Settings(); s.custom = true; s.largeImageKey.clear(); s.largeImageText.clear();
        s.details.clear(); s.state.clear(); s.timer = 2; s.end = 123;
        const auto activity = ActivityJson(s, "", "", 42);
        Check(activity.find("assets") == std::string::npos && activity.find("details") == std::string::npos, "empty fields were not hidden");
        Check(activity.find("\"timestamps\":{\"end\":123}") != std::string::npos, "countdown timestamp failed");
        TestIPC();
        std::cout << "Discord settings and IPC tests passed (fragmentation, fixed ID, custom name, ping/pong, reconnect).\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
