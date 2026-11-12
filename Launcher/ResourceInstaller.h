#pragma once

#include "ResourceDownload.h"
#include "../LauncherAssets/ResourceIds.h"
#include <array>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace SDKResources
{
    class LiveDownload
    {
        std::wstring target, sdkDirectory, directory, archiveDirectory, previousArchiveDirectory, tempKey, previousTempKey, cacheTag;
        HANDLE process = nullptr, installationLock = INVALID_HANDLE_VALUE;
        bool started = false, stopping = false;
        uint64_t downloaded = 0, total = 10123443705ull, extracted = 0, extractTotal = 0;
        double rate = 0.;
        std::string phase, error;
        std::chrono::steady_clock::time_point nextPoll;

        static void ScanParts(const std::wstring& cache, std::unordered_map<std::wstring, uint64_t>& sizes)
        {
            if (cache.empty()) return;
            WIN32_FIND_DATAW data = {};
            HANDLE search = FindFirstFileW((cache + L"\\*.7z.*").c_str(), &data);
            if (search == INVALID_HANDLE_VALUE) return;
            do
            {
                if (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
                std::wstring name = data.cFileName;
                std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return wchar_t(towlower(c)); });
                if (name.size() > 5 && name.substr(name.size() - 5) == L".part") name.resize(name.size() - 5);
                const auto separator = name.rfind(L".7z.");
                if (separator == std::wstring::npos || name.size() - separator != 7 ||
                    name.find_first_not_of(L"0123456789", separator + 4) != std::wstring::npos) continue;
                const uint64_t length = (uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
                sizes[name] = (std::max)(sizes[name], length);
            } while (FindNextFileW(search, &data));
            FindClose(search);
        }

        static std::string Utf8(const std::wstring& text)
        {
            const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr, nullptr);
            std::string result(length, '\0');
            if (length) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), &result[0], length, nullptr, nullptr);
            return result;
        }

        static std::wstring Quote(const std::wstring& value)
        {
            std::wstring result = L"\"";
            size_t slashes = 0;
            for (wchar_t c : value)
            {
                if (c == L'\\') { ++slashes; continue; }
                result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
                slashes = 0;
                result += c;
            }
            result.append(slashes * 2, L'\\');
            return result + L'"';
        }

        bool ExtractResource(unsigned id, const wchar_t* name)
        {
            auto module = GetModuleHandleW(nullptr);
            auto resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
            auto loaded = resource ? LoadResource(module, resource) : nullptr;
            const void* bytes = loaded ? LockResource(loaded) : nullptr;
            const DWORD size = resource ? SizeofResource(module, resource) : 0;
            if (!bytes || !size) return false;
            const std::wstring path = directory + L"\\" + name;
            const DWORD attributes = GetFileAttributesW(path.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) return false;
            HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return false;
            DWORD written = 0;
            const bool saved = WriteFile(file, bytes, size, &written, nullptr) && written == size;
            CloseHandle(file);
            return saved;
        }

        void ReadState()
        {
            // Allow an atomic replacement by the worker while the snapshot is open.
            HANDLE file = CreateFileW((directory + L"\\state.txt").c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return;
            char bytes[16384] = {};
            DWORD length = 0;
            const bool read = ReadFile(file, bytes, sizeof(bytes) - 1, &length, nullptr) != FALSE;
            CloseHandle(file);
            if (!read || !length || length == sizeof(bytes) - 1) return;
            std::istringstream stream(std::string(bytes, length));
            std::array<std::string, 10> lines;
            for (auto& line : lines) if (!std::getline(stream, line)) return;
            if (lines[0] != "resources-v2" || lines[8] != "github:lunardontcry/SDK-DATA-PASS:7z-v1" || lines[9] != Utf8(target)) return;
            uint64_t newDownloaded = 0, newTotal = 0, newExtracted = 0, newExtractTotal = 0;
            double newRate = 0.;
            std::istringstream numbers(lines[2] + " " + lines[3] + " " + lines[4] + " " + lines[5] + " " + lines[6]);
            if (!(numbers >> newDownloaded >> newTotal >> newRate >> newExtracted >> newExtractTotal) ||
                !newTotal || newDownloaded > newTotal || newExtracted > newExtractTotal || !std::isfinite(newRate) || newRate < 0.) return;
            phase = lines[1];
            downloaded = newDownloaded; total = newTotal;
            extracted = newExtracted; extractTotal = newExtractTotal;
            rate = process && !stopping ? newRate : 0.;
            error = lines[7];
            started = true;
        }

        void ReleaseProcess()
        {
            if (process) { CloseHandle(process); process = nullptr; }
            if (installationLock != INVALID_HANDLE_VALUE) { CloseHandle(installationLock); installationLock = INVALID_HANDLE_VALUE; }
            stopping = false;
            rate = 0.;
        }

    public:
        ~LiveDownload()
        {
            Pause();
            if (process) WaitForSingleObject(process, 7000);
            ReleaseProcess();
        }

        void Initialize(const std::wstring& sdk)
        {
            Pause();
            if (process) WaitForSingleObject(process, 7000);
            ReleaseProcess();
            sdkDirectory = FarkashedLauncher::FullPath(sdk);
            target = ResourceRoot(sdkDirectory);
            std::transform(target.begin(), target.end(), target.begin(), [](wchar_t c) { return wchar_t(towlower(c)); });
            while (target.size() > 3 && (target.back() == L'\\' || target.back() == L'/')) target.pop_back();
            uint64_t hash = 14695981039346656037ull;
            for (wchar_t c : target) hash = (hash ^ uint64_t(c)) * 1099511628211ull;
            cacheTag = L"github-v1-" + std::to_wstring(hash);
            tempKey = L"ResourceTemp_" + std::to_wstring(hash);
            previousTempKey = L"ResourceTempPrevious_" + std::to_wstring(hash);
            const auto history = FarkashedLauncher::HistoryPath();
            directory.clear();
            if (!history.empty())
            {
                auto parent = history.substr(0, history.find_last_of(L"\\/")) + L"\\ResourceDownloads";
                CreateDirectoryW(parent.c_str(), nullptr);
                directory = parent + L"\\" + cacheTag;
                CreateDirectoryW(directory.c_str(), nullptr);
            }
            archiveDirectory = FarkashedLauncher::Setting(tempKey.c_str());
            // Preserve the chosen TEMP disk, but keep old Google ZIP data separate.
            if (archiveDirectory.find(L"drive-v1-") != std::wstring::npos)
            {
                archiveDirectory = ParentDirectory(archiveDirectory) + L"\\" + cacheTag;
                FarkashedLauncher::SetSetting(tempKey.c_str(), archiveDirectory);
            }
            if (archiveDirectory.empty()) archiveDirectory = directory;
            previousArchiveDirectory = FarkashedLauncher::Setting(previousTempKey.c_str());
            if (previousArchiveDirectory.find(L"drive-v1-") != std::wstring::npos) previousArchiveDirectory.clear();
            started = false;
            downloaded = extracted = extractTotal = 0;
            total = 10123443705ull;
            phase.clear(); error.clear();
            if (!directory.empty())
            {
                ReadState();
                std::unordered_map<std::wstring, uint64_t> sizes;
                ScanParts(previousArchiveDirectory, sizes);
                ScanParts(archiveDirectory, sizes);
                if (!sizes.empty())
                {
                    started = true; downloaded = 0;
                    for (const auto& part : sizes) downloaded += part.second;
                    total = (std::max)(total, downloaded);
                }
            }
            if (Complete() && Missing(sdkDirectory))
            {
                // Folder presence, rather than an old completion record, decides
                // whether a subsequently damaged or stripped SDK needs setup.
                started = false;
                downloaded = extracted = extractTotal = 0;
                phase.clear(); error.clear();
            }
            nextPoll = std::chrono::steady_clock::now();
        }

        bool Started() const { return started; }
        const std::wstring& TempDirectory() const { return archiveDirectory; }
        bool SetTempDirectory(const std::wstring& selected)
        {
            if (process || directory.empty()) return false;
            const auto base = FarkashedLauncher::FullPath(selected);
            const auto destination = FarkashedLauncher::FullPath(base + L"\\FarkashedSDK-Resources\\" + cacheTag);
            const int created = SHCreateDirectoryExW(nullptr, destination.c_str(), nullptr);
            if (created != ERROR_SUCCESS && created != ERROR_ALREADY_EXISTS)
            { error = "Cannot create the selected TEMP folder."; return false; }
            const DWORD attributes = GetFileAttributesW(destination.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            { error = "The selected TEMP folder is unavailable."; return false; }
            // Check write access without leaving a probe file behind.
            HANDLE probe = CreateFileW((destination + L"\\.write-check-" + std::to_wstring(GetCurrentProcessId())).c_str(),
                GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
            if (probe == INVALID_HANDLE_VALUE) { error = "Cannot write to the selected TEMP folder."; return false; }
            CloseHandle(probe);
            HANDLE lock = CreateFileW((directory + L"\\launcher.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
                0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (lock == INVALID_HANDLE_VALUE) { error = "Another launcher is using this download."; return false; }
            auto previous = previousArchiveDirectory;
            std::unordered_map<std::wstring, uint64_t> currentParts;
            ScanParts(archiveDirectory, currentParts);
            if (!currentParts.empty()) previous = archiveDirectory;
            if (_wcsicmp(previous.c_str(), destination.c_str()) == 0) previous.clear();
            const bool saved = FarkashedLauncher::SetSetting(previousTempKey.c_str(), previous) &&
                FarkashedLauncher::SetSetting(tempKey.c_str(), destination);
            CloseHandle(lock);
            if (!saved) { error = "Cannot save the selected TEMP folder."; return false; }
            previousArchiveDirectory = previous;
            archiveDirectory = destination;
            error.clear();
            return true;
        }
        bool Running() const { return process != nullptr; }
        bool Stopping() const { return stopping; }
        bool Installing() const { return phase == "extracting" || phase == "installing" ||
            ((phase == "paused" || phase == "error") && extractTotal > 0); }
        bool Complete() const { return phase == "complete"; }
        uint64_t Downloaded() const { return downloaded; }
        uint64_t Total() const { return total; }
        double Rate() const { return rate; }
        float Progress() const { return total ? float(double(downloaded) / double(total)) : 0.f; }
        float InstallProgress() const { return extractTotal ? float(double(extracted) / double(extractTotal)) : 0.f; }
        const std::string& Error() const { return error; }
        const char* Status() const
        {
            if (stopping) return "Pausing...";
            if (!process && !Complete()) return error.empty() ? "Paused" : "Download or installation interrupted";
            if (phase == "checking") return "Preparing download...";
            if (phase == "moving") return "Moving cached archive...";
            if (phase == "verifying") return "Verifying archive parts...";
            if (phase == "extracting") return "Extracting resources...";
            if (phase == "installing") return "Installing resources...";
            if (phase == "complete") return "Resources installed.";
            return "Downloading resources...";
        }

        void Start()
        {
            if (process) return;
            error.clear();
            if (directory.empty()) { error = "Cannot create the resource download cache."; return; }
            const DWORD attributes = GetFileAttributesW(directory.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            { error = "The resource download cache is unavailable."; return; }
            installationLock = CreateFileW((directory + L"\\launcher.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
                0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (installationLock == INVALID_HANDLE_VALUE) { error = "Another launcher is using this resource download."; return; }
            if (!ExtractResource(IDR_SDK_RESOURCE_WORKER, L"ResourceWorker.cs") ||
                !ExtractResource(IDR_SDK_RESOURCE_SCRIPT, L"ResourceWorker.ps1") ||
                !ExtractResource(IDR_SDK_7ZIP_EXE, L"7z.exe") ||
                !ExtractResource(IDR_SDK_7ZIP_DLL, L"7z.dll") ||
                !ExtractResource(IDR_SDK_7ZIP_LICENSE, L"7zip-License.txt") ||
                !ExtractResource(IDR_SDK_7ZIP_README, L"7zip-README.md"))
            { error = "Cannot prepare the resource installer."; ReleaseProcess(); return; }
            DeleteFileW((directory + L"\\pause").c_str());
            DeleteFileW((directory + L"\\state.txt").c_str());
            DeleteFileW((directory + L"\\worker-error.txt").c_str());
            wchar_t system[MAX_PATH] = {};
            GetSystemDirectoryW(system, MAX_PATH);
            const std::wstring executable = std::wstring(system) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
            FILETIME created = {}, exited = {}, kernel = {}, user = {};
            GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
            const uint64_t startTime = (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
            std::wstring command = Quote(executable) + L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
                Quote(directory + L"\\ResourceWorker.ps1") + L" -Target " + Quote(target) + L" -Work " + Quote(directory) +
                L" -Sdk " + Quote(sdkDirectory) + L" -ArchiveDirectory " + Quote(archiveDirectory) +
                L" -ParentId " + std::to_wstring(GetCurrentProcessId()) + L" -ParentStart " + std::to_wstring(startTime);
            if (!previousArchiveDirectory.empty()) command += L" -PreviousArchiveDirectory " + Quote(previousArchiveDirectory);
            STARTUPINFOW startup = {}; startup.cb = sizeof(startup);
            PROCESS_INFORMATION child = {};
            if (!CreateProcessW(executable.c_str(), &command[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                nullptr, directory.c_str(), &startup, &child))
            { error = "Cannot start the resource installer."; ReleaseProcess(); return; }
            CloseHandle(child.hThread);
            process = child.hProcess;
            started = true;
            stopping = false;
            phase = "checking";
            rate = 0.;
        }

        void Pause()
        {
            if (!process || stopping) return;
            HANDLE file = CreateFileW((directory + L"\\pause").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) { error = "Cannot pause the resource installer."; return; }
            CloseHandle(file);
            stopping = true;
            rate = 0.;
        }

        void Poll()
        {
            if (!process || std::chrono::steady_clock::now() < nextPoll) return;
            nextPoll = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
            ReadState();
            if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0)
            {
                ReadState();
                DWORD code = 0;
                GetExitCodeProcess(process, &code);
                if (code == 2) error = "Another installer is still stopping. Try Resume shortly.";
                else if (phase != "paused" && phase != "complete" && phase != "error")
                    error = "Resource installer stopped. See worker-error.txt in the download cache.";
                ReleaseProcess();
            }
        }
    };

    class Download
    {
        DemoDownload demo;
        LiveDownload live;
        bool test = false;
    public:
        void Initialize(const std::wstring& sdk, bool demoMode)
        {
            test = demoMode;
            live.Initialize(sdk);
            if (test) demo.Initialize(ResourceRoot(sdk));
        }
        const std::wstring& TempDirectory() const { return live.TempDirectory(); }
        const std::string& TempError() const { return live.Error(); }
        bool SetTempDirectory(const std::wstring& path) { return live.SetTempDirectory(path); }
        bool TestMode() const { return test; }
        bool Started() const { return test ? demo.Started() : live.Started(); }
        bool Running() const { return test ? demo.Running() : live.Running(); }
        bool Stopping() const { return !test && live.Stopping(); }
        bool Installing() const { return test ? demo.Installing() : live.Installing(); }
        bool Complete() const { return test ? demo.Complete() : live.Complete(); }
        uint64_t Downloaded() const { return test ? demo.Downloaded() : live.Downloaded(); }
        uint64_t Total() const { return test ? DemoDownload::TotalBytes : live.Total(); }
        double Rate() const { return test ? demo.Rate() : live.Rate(); }
        float Progress() const { return test ? demo.Progress() : live.Progress(); }
        float InstallProgress() const { return test ? demo.InstallProgress() : live.InstallProgress(); }
        const std::string& Error() const { return test ? demo.Error() : live.Error(); }
        const char* Status() const
        {
            if (!test) return live.Status();
            if (demo.Complete()) return "Test complete. SDK resources have not been installed.";
            if (!demo.Running()) return demo.Installing() ? "Paused - simulating installation..." : "Paused - simulating download...";
            return demo.Installing() ? "Simulating installation..." : "Simulating download...";
        }
        void Start() { if (test) demo.Start(); else live.Start(); }
        void Pause() { if (test) demo.Pause(); else live.Pause(); }
        void Poll() { if (test) demo.Poll(); else live.Poll(); }
    };
}
