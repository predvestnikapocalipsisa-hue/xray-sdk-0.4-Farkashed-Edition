#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS
#include "MediaPlayer.h"
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#pragma comment(lib, "windowsapp.lib")

namespace SDKMedia
{
    namespace
    {
        using namespace winrt::Windows::Media::Control;
        using Session = GlobalSystemMediaTransportControlsSession;
        using Manager = GlobalSystemMediaTransportControlsSessionManager;

        template<class T> auto Await(const T& operation)
        {
            // Broken/closing providers must never stall the render thread or shutdown.
            if (operation.wait_for(std::chrono::seconds(2)) != winrt::Windows::Foundation::AsyncStatus::Completed)
            {
                operation.Cancel();
                throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
            }
            return operation.GetResults();
        }

        struct Entry { uint64_t id; Session session; };
        struct Command { uint64_t id; Action action; };
        class Player
        {
            std::mutex mutex;
            std::condition_variable wake;
            State state;
            bool stopping = false;
            uint64_t preferred = 0;
            std::deque<Command> commands;
            std::thread worker;

            void Run()
            {
                try
                {
                    winrt::init_apartment(winrt::apartment_type::multi_threaded);
                    struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
                    // All WinRT objects are released before uninitializing the apartment.
                    RunSession();
                }
                catch (...) { Publish(State{}, "Media controls unavailable (Windows 10 1809 or later required)"); }
            }

            void Publish(State value, const char* message = nullptr)
            {
                if (message) value.message = message;
                std::lock_guard<std::mutex> lock(mutex);
                state = std::move(value);
            }

            void RunSession()
            {
                Manager manager{nullptr};
                std::vector<Entry> entries;
                uint64_t nextId = 1;
                for (;;)
                {
                    uint64_t requested;
                    std::deque<Command> pending;
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        if (stopping) break;
                        requested = preferred;
                        pending.swap(commands);
                    }
                    try
                    {
                        if (!manager) manager = Await(Manager::RequestAsync());
                        std::string commandError;
                        for (const auto& command : pending)
                        {
                            { std::lock_guard<std::mutex> lock(mutex); if (stopping) return; }
                            auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.id == command.id; });
                            if (it == entries.end()) continue;
                            bool ok = false;
                            switch (command.action)
                            {
                            case Action::Previous: ok = Await(it->session.TrySkipPreviousAsync()); break;
                            case Action::Toggle: ok = Await(it->session.TryTogglePlayPauseAsync()); break;
                            case Action::Stop: ok = Await(it->session.TryStopAsync()); break;
                            case Action::Next: ok = Await(it->session.TrySkipNextAsync()); break;
                            }
                            if (!ok) commandError = "The player did not accept this command";
                        }
                        std::vector<Entry> current;
                        State value;
                        Session selected{nullptr};
                        const Session active = manager.GetCurrentSession();
                        for (const Session& session : manager.GetSessions())
                        {
                            auto old = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.session == session; });
                            const uint64_t id = old == entries.end() ? nextId++ : old->id;
                            current.push_back({id, session});
                            value.sources.push_back({id, winrt::to_string(session.SourceAppUserModelId())});
                            if ((requested && id == requested) || (!requested && session == active))
                            { selected = session; value.selected = id; }
                        }
                        entries = std::move(current);
                        // A closed pinned source falls back to the Windows current session.
                        if (requested && !selected)
                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            if (preferred == requested) preferred = 0;
                        }
                        if (!selected && !entries.empty())
                        {
                            auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.session == active; });
                            if (it == entries.end()) it = entries.begin();
                            selected = it->session; value.selected = it->id;
                        }
                        if (selected)
                        {
                            value.source = winrt::to_string(selected.SourceAppUserModelId());
                            auto info = selected.GetPlaybackInfo();
                            auto controls = info.Controls();
                            value.playing = info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                            value.toggle = controls.IsPlayPauseToggleEnabled();
                            value.previous = controls.IsPreviousEnabled();
                            value.next = controls.IsNextEnabled();
                            value.stop = controls.IsStopEnabled();
                            value.message = commandError;
                            try
                            {
                                auto properties = Await(selected.TryGetMediaPropertiesAsync());
                                value.title = winrt::to_string(properties.Title());
                                value.artist = winrt::to_string(properties.Artist());
                            }
                            catch (...) { value.message = "Track information is temporarily unavailable"; }
                        }
                        else value.message = "Start music in Spotify, SoundCloud, YouTube Music or another player";
                        Publish(std::move(value));
                    }
                    catch (...) { manager = nullptr; entries.clear(); Publish(State{}, "Cannot read media session; retrying..."); }
                    std::unique_lock<std::mutex> lock(mutex);
                    wake.wait_for(lock, std::chrono::milliseconds(750), [&] { return stopping || !commands.empty() || preferred != requested; });
                }
            }
        public:
            Player() : worker([this] { Run(); }) {}
            ~Player()
            {
                { std::lock_guard<std::mutex> lock(mutex); stopping = true; }
                wake.notify_one();
                worker.join();
            }
            State Snapshot() { std::lock_guard<std::mutex> lock(mutex); return state; }
            void Select(uint64_t id) { { std::lock_guard<std::mutex> lock(mutex); preferred = id; } wake.notify_one(); }
            void Send(uint64_t id, Action action)
            {
                { std::lock_guard<std::mutex> lock(mutex); if (id && commands.size() < 8) commands.push_back({id, action}); }
                wake.notify_one();
            }
        };
        std::unique_ptr<Player> player;
    }
    State Snapshot() { if (!player) player.reset(new Player); return player->Snapshot(); }
    void Select(uint64_t id) { if (player) player->Select(id); }
    void Send(uint64_t id, Action action) { if (player) player->Send(id, action); }
    void Shutdown() { player.reset(); }
}
