#include "../ResourceDownload.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool value, const char* description)
    {
        if (!value) throw std::runtime_error(description);
    }

    struct Workspace
    {
        std::wstring root;
        std::vector<std::wstring> directories;
        Workspace()
        {
            wchar_t temp[MAX_PATH] = {}, unique[MAX_PATH] = {};
            Check(GetTempPathW(MAX_PATH, temp) != 0, "temporary directory");
            Check(GetTempFileNameW(temp, L"xrd", 0, unique) != 0, "unique temporary name");
            Check(DeleteFileW(unique) != FALSE, "remove temporary placeholder");
            root = unique;
            Add(root);
        }
        void Add(const std::wstring& path)
        {
            Check(CreateDirectoryW(path.c_str(), nullptr) != FALSE, "create test directory");
            directories.push_back(path);
        }
        ~Workspace()
        {
            // Only delete files immediately inside directories created by this test.
            for (auto it = directories.rbegin(); it != directories.rend(); ++it)
            {
                WIN32_FIND_DATAW data = {};
                HANDLE search = FindFirstFileW((*it + L"\\*").c_str(), &data);
                if (search != INVALID_HANDLE_VALUE)
                {
                    do
                    {
                        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                            DeleteFileW((*it + L"\\" + data.cFileName).c_str());
                    } while (FindNextFileW(search, &data));
                    FindClose(search);
                }
                RemoveDirectoryW(it->c_str());
            }
        }
    };
}

int wmain()
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 0;
    try
    {
        Workspace workspace;
        const auto sdk = workspace.root + L"\\SDK";
        const auto states = workspace.root + L"\\state";
        workspace.Add(sdk);
        workspace.Add(states);
        Check(SDKResources::Missing(sdk), "both folders missing");
        workspace.Add(sdk + L"\\settings");
        Check(SDKResources::Missing(sdk), "gamedata missing");
        workspace.Add(sdk + L"\\gamedata");
        Check(!SDKResources::Missing(sdk), "both folders exist");
        workspace.Add(sdk + L"\\bins");
        Check(SDKResources::ResourceRoot(sdk + L"\\bins") == sdk, "resource root is above bins");
        Check(!SDKResources::Missing(sdk + L"\\bins"), "resources next to bins are detected");
        const auto fresh = workspace.root + L"\\fresh-sdk";
        workspace.Add(fresh);
        workspace.Add(fresh + L"\\bins");
        Check(SDKResources::ResourceRoot(fresh + L"\\bins") == fresh, "install outside empty bins");
        Check(SDKResources::Missing(fresh + L"\\bins"), "fresh SDK still requires resources");
        Check(!SDKResources::DirectoryExists(states + L"\\absent"), "absent directory");

        using Demo = SDKResources::DemoDownload;
        uint64_t checkpoint = 0;
        {
            Demo download;
            download.Initialize(sdk, states);
            Check(!download.Started(), "new session is idle");
            download.Start();
            Check(download.Running() && download.Error().empty(), "start saves state");
            download.Advance(.5);
            Check(download.Downloaded() > 0 && download.Rate() > 0, "bytes and speed advance");
            download.Pause();
            checkpoint = download.Downloaded();
            download.Advance(1.);
            Check(download.Downloaded() == checkpoint && download.Rate() == 0., "pause freezes progress");
        }
        {
            Demo download;
            download.Initialize(sdk + L"\\.", states);
            Check(download.Started() && !download.Running() && download.Downloaded() == checkpoint,
                "reopen paused at exact checkpoint with normalized SDK path");
            download.Start();
            download.Advance(.5);
            Check(download.Downloaded() > checkpoint, "resume continues");
            checkpoint = download.Downloaded();
            // Closing while running must checkpoint too.
        }
        {
            Demo download;
            download.Initialize(sdk, states);
            Check(download.Downloaded() == checkpoint && !download.Running(), "close while running persists");
            download.Start();
            for (int i = 0; i < 160 && !download.Installing(); ++i) download.Advance(.5);
            Check(download.Installing() && download.Progress() == 1.f && download.Rate() == 0., "download transitions to install");
            download.Advance(.5);
            download.Pause();
            Check(download.InstallProgress() > 0.f && download.InstallProgress() < 1.f, "installation can pause");
        }
        {
            Demo download;
            download.Initialize(sdk, states);
            Check(download.Installing() && !download.Running() && download.InstallProgress() > 0.f,
                "installation restores paused");
            download.Start();
            for (int i = 0; i < 10; ++i) download.Advance(.5);
            Check(download.Complete() && !download.Running(), "installation completes");
            Check(!SDKResources::DirectoryExists(sdk + L"\\rawdata"), "demo never installs fake resource folders");
        }
        {
            Demo download;
            download.Initialize(sdk, states);
            Check(download.Complete(), "completion survives reopening");
            download.Start();
            Check(download.Running() && download.Downloaded() == 0, "restart test resets progress");
            download.Pause();
        }
        {
            Demo other;
            other.Initialize(workspace.root + L"\\other-sdk", states);
            Check(!other.Started(), "SDK installations have independent state");
            Demo inaccessible;
            inaccessible.Initialize(sdk, workspace.root + L"\\missing\\nested");
            inaccessible.Start();
            Check(!inaccessible.Running() && !inaccessible.Error().empty(), "save failure blocks progress");
            inaccessible.Advance(1.);
            Check(inaccessible.Downloaded() == 0, "cannot advance without a checkpoint");
        }
        {
            WIN32_FIND_DATAW data = {};
            HANDLE search = FindFirstFileW((states + L"\\*.ini").c_str(), &data);
            Check(search != INVALID_HANDLE_VALUE, "saved fixture exists");
            const auto path = states + L"\\" + data.cFileName;
            FindClose(search);
            Check(WritePrivateProfileStringW(L"Demo", L"Downloaded", L"18446744073709551616", path.c_str()) != FALSE,
                "write overflow fixture");
            Demo invalid;
            invalid.Initialize(sdk, states);
            Check(!invalid.Started() && invalid.Downloaded() == 0, "overflow checkpoint is rejected");
        }
        std::cout << "PASS: resource detection, pause/resume, reopen, install, isolation, invalid state and save failure\n";
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL: " << exception.what() << '\n';
        result = 1;
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}
