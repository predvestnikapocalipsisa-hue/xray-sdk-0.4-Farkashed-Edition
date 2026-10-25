#pragma once

#include "RecentFiles.h"
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cwctype>

// Offline prototype. The counters represent a fixture manifest, never real files.
// The real transport and installer live in ResourceInstaller.h / ResourceWorker.cs.
namespace SDKResources
{
    inline bool DirectoryExists(const std::wstring& path)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
    }

    inline bool HasResources(const std::wstring& root)
    {
        return DirectoryExists(root + L"\\settings") && DirectoryExists(root + L"\\gamedata");
    }

    inline std::wstring ParentDirectory(std::wstring path)
    {
        while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
        const auto slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos || path.size() <= 3) return {};
        return path.substr(0, slash == 2 && path[1] == L':' ? 3 : slash);
    }

    inline std::wstring ResourceRoot(const std::wstring& sdk)
    {
        const auto directory = FarkashedLauncher::FullPath(sdk);
        // Match the engine's search for fs.ltx, including resources next to bins.
        for (auto candidate = directory; !candidate.empty(); candidate = ParentDirectory(candidate))
            if (HasResources(candidate) || FarkashedLauncher::FileExists(candidate + L"\\settings\\fs.ltx") ||
                FarkashedLauncher::FileExists(candidate + L"\\fs.ltx")) return candidate;
        for (auto candidate = directory; !candidate.empty(); candidate = ParentDirectory(candidate))
            if (DirectoryExists(candidate + L"\\settings") || DirectoryExists(candidate + L"\\gamedata")) return candidate;
        const auto slash = directory.find_last_of(L"\\/");
        const auto name = directory.substr(slash == std::wstring::npos ? 0 : slash + 1);
        const auto parent = ParentDirectory(directory);
        if (!parent.empty() && (_wcsicmp(name.c_str(), L"bins") == 0 || _wcsicmp(name.c_str(), L"bin") == 0)) return parent;
        return directory;
    }

    inline bool Missing(const std::wstring& sdk)
    {
        return !HasResources(ResourceRoot(sdk));
    }

    class DemoDownload
    {
        using Clock = std::chrono::steady_clock;
        std::wstring target, statePath;
        uint64_t downloaded = 0, installMs = 0;
        bool started = false, running = false;
        double saveElapsed = 0., elapsed = 0., rate = 0., installFractionMs = 0.;
        Clock::time_point lastTick = Clock::now();
        std::string error;

        static bool Parse(const std::wstring& value, uint64_t& result)
        {
            if (value.empty() || value.find_first_not_of(L"0123456789") != std::wstring::npos)
                return false;
            wchar_t* end = nullptr;
            errno = 0;
            result = wcstoull(value.c_str(), &end, 10);
            return errno != ERANGE && end && !*end;
        }

        bool Save()
        {
            const std::wstring content = L"\xfeff[Demo]\r\nVersion=1\r\nTarget=" + target +
                L"\r\nStarted=" + (started ? L"1" : L"0") + L"\r\nDownloaded=" +
                std::to_wstring(downloaded) + L"\r\nInstallMs=" + std::to_wstring(installMs) + L"\r\n";
            const std::wstring temporary = statePath + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
            HANDLE file = statePath.empty() ? INVALID_HANDLE_VALUE : CreateFileW(temporary.c_str(),
                GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            bool saved = false;
            if (file != INVALID_HANDLE_VALUE)
            {
                DWORD written = 0;
                const DWORD size = DWORD(content.size() * sizeof(wchar_t));
                saved = WriteFile(file, content.data(), size, &written, nullptr) && written == size;
                if (saved) saved = FlushFileBuffers(file) != FALSE;
                CloseHandle(file);
                if (saved) saved = MoveFileExW(temporary.c_str(), statePath.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
                if (!saved) DeleteFileW(temporary.c_str());
            }
            error = saved ? "" : "Cannot save progress. Check access and disk space, then Resume.";
            if (!saved) { running = false; rate = 0.; }
            saveElapsed = 0.;
            return saved;
        }

    public:
        static constexpr uint64_t MiB = 1024ull * 1024ull;
        static constexpr uint64_t TotalBytes = (16ull + 1536ull + 2048ull) * MiB;
        static constexpr uint64_t InstallDurationMs = 4000;

        DemoDownload() = default;
        DemoDownload(const DemoDownload&) = delete;
        DemoDownload& operator=(const DemoDownload&) = delete;
        ~DemoDownload() { if (started) Save(); }

        void Initialize(const std::wstring& sdk, const std::wstring& stateDirectory = {})
        {
            if (started) Save();
            target = FarkashedLauncher::FullPath(sdk);
            std::transform(target.begin(), target.end(), target.begin(), [](wchar_t c) { return wchar_t(towlower(c)); });
            while (target.size() > 3 && (target.back() == L'\\' || target.back() == L'/')) target.pop_back();
            std::wstring directory = stateDirectory;
            if (directory.empty())
            {
                const auto history = FarkashedLauncher::HistoryPath();
                if (!history.empty()) directory = history.substr(0, history.find_last_of(L"\\/")) + L"\\ResourceDownloads";
            }
            statePath.clear();
            if (!directory.empty() && (CreateDirectoryW(directory.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS))
            {
                uint64_t hash = 14695981039346656037ull;
                for (wchar_t c : target) hash = (hash ^ uint64_t(c)) * 1099511628211ull;
                statePath = directory + L"\\demo-v1-" + std::to_wstring(hash) + L".ini";
            }
            downloaded = installMs = 0;
            elapsed = saveElapsed = rate = installFractionMs = 0.;
            started = running = false;
            error.clear();
            // Always reopen paused. Ignore stale, invalid or incompatible fixtures.
            const auto read = [this](const wchar_t* key) { return FarkashedLauncher::ReadValue(statePath, L"Demo", key); };
            uint64_t bytes = 0, installed = 0;
            if (read(L"Version") == L"1" && read(L"Target") == target && read(L"Started") == L"1" &&
                Parse(read(L"Downloaded"), bytes) && bytes <= TotalBytes &&
                Parse(read(L"InstallMs"), installed) && installed <= InstallDurationMs &&
                (installed == 0 || bytes == TotalBytes))
            {
                started = true;
                downloaded = bytes;
                installMs = installed;
            }
            lastTick = Clock::now();
        }

        bool Started() const { return started; }
        bool Running() const { return running; }
        bool Installing() const { return downloaded == TotalBytes && !Complete(); }
        bool Complete() const { return installMs == InstallDurationMs; }
        uint64_t Downloaded() const { return downloaded; }
        double Rate() const { return rate; }
        float Progress() const { return float(double(downloaded) / double(TotalBytes)); }
        float InstallProgress() const { return float(installMs) / float(InstallDurationMs); }
        const std::string& Error() const { return error; }
        const char* Package() const
        {
            if (downloaded < 16ull * MiB) return "settings";
            if (downloaded < (16ull + 1536ull) * MiB) return "gamedata";
            return "rawdata";
        }

        void Start()
        {
            if (Complete()) { downloaded = installMs = 0; elapsed = installFractionMs = 0.; }
            started = true;
            running = Save();
            lastTick = Clock::now();
        }

        void Pause()
        {
            running = false;
            rate = 0.;
            if (started) Save();
        }

        // Separate from rendering so downloads also advance while minimized.
        void Poll()
        {
            const auto now = Clock::now();
            const double seconds = std::chrono::duration<double>(now - lastTick).count();
            lastTick = now;
            Advance(seconds);
        }

        void Advance(double seconds)
        {
            if (!running || !std::isfinite(seconds) || seconds <= 0.) return;
            // A suspended computer must not produce an enormous fake speed spike.
            seconds = (std::min)(seconds, 1.);
            elapsed += seconds;
            saveElapsed += seconds;
            if (Installing())
            {
                rate = 0.;
                installFractionMs += seconds * 1000.;
                const auto wholeMs = uint64_t(installFractionMs);
                installFractionMs -= double(wholeMs);
                installMs = (std::min)(uint64_t(InstallDurationMs), installMs + wholeMs);
                if (Complete()) running = false;
            }
            else
            {
                const double simulatedRate = (64. + 12. * std::sin(elapsed * .7)) * double(MiB);
                const uint64_t bytes = (std::min)(TotalBytes - downloaded, uint64_t(simulatedRate * seconds));
                downloaded += bytes;
                rate = double(bytes) / seconds;
                if (Installing()) rate = 0.;
            }
            if (saveElapsed >= 1. || !running) Save();
        }
    };
}
