#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Keep WinRT and its C++17 requirements out of the legacy editor headers.
namespace SDKMedia
{
    struct Source { uint64_t id; std::string name; };
    struct State
    {
        std::vector<Source> sources;
        uint64_t selected = 0;
        std::string title, artist, source, message = "Looking for media...";
        bool playing = false, previous = false, next = false, toggle = false, stop = false;
    };
    enum class Action { Previous, Toggle, Stop, Next };
    State Snapshot();
    void Select(uint64_t id); // Zero follows the Windows current session.
    void Send(uint64_t id, Action action);
    void Shutdown();
}
