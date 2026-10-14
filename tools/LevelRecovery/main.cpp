#include "../../Editors/LevelEditor/Recovery/LevelRecovery.h"
#include <filesystem>
#include <iostream>

int wmain(int argc, wchar_t** argv)
{
    const bool trees = argc == 4 && std::wstring(argv[1]) == L"--trees";
    const bool scene = argc == 4 && std::wstring(argv[1]) == L"--scene";
    if (!trees && !scene && (argc < 2 || argc > 3))
    {
        std::cerr << "Usage: LevelRecovery <compiled-level-directory> [new-export-directory]\n"
                     "       LevelRecovery --trees <compiled-level-directory> <new-export-directory>\n"
                     "       LevelRecovery --scene <compiled-level-directory> <new-export-directory>\n";
        return 2;
    }
    try
    {
        auto level = LevelRecovery::Read(std::filesystem::path(argv[trees || scene ? 2 : 1]).u8string());
        if (trees)
        {
            LevelRecovery::ExportTreeLibrary(level,std::filesystem::path(argv[3]).u8string());
            const auto plan=LevelRecovery::BuildTreeLibrary(level);
            std::cout << "Recovered " << plan.models.size() << " native tree models and " << plan.instances.size() << " instances. See tree-recovery.txt in export directory.\n";
            return 0;
        }
        LevelRecovery::GroupConnectedSurfaces(level);
        std::cout << LevelRecovery::Describe(level);
        if (scene)
        {
            const auto plan=LevelRecovery::BuildStaticLibrary(level);
            std::cout << "Static candidates: " << plan.instances.size() << "; shared models: " << plan.models.size() << std::endl;
            LevelRecovery::ExportSceneLibrary(level,plan,std::filesystem::path(argv[3]).u8string());
            std::cout << "Native geometry recovery completed. See scene-recovery.txt.\n"; return 0;
        }
        if (argc == 3)
        {
            LevelRecovery::Export(level, std::filesystem::path(argv[2]).u8string());
            std::cout << "\nRecovery export completed.\n";
        }
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Recovery failed: " << e.what() << '\n';
        return 1;
    }
}
