#include "pch_script.h"
#include "CompiledLevelConfig.h"
#include "xrSE_Factory_import_export.h"
#include "../../XrCore/Xr_ini_fallback.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string_view>

namespace
{
namespace fs = std::filesystem;

bool MatchFilename(std::wstring_view name, std::wstring_view pattern)
{
    size_t character = 0, token = 0, star = std::wstring_view::npos, retry = 0;
    while (character < name.size())
    {
        if (token < pattern.size() && pattern[token] == L'*')
        {
            star = token++;
            retry = character;
        }
        else if (token < pattern.size() && (pattern[token] == L'?' ||
            CompareStringOrdinal(&name[character], 1, &pattern[token], 1, TRUE) == CSTR_EQUAL))
        {
            ++character;
            ++token;
        }
        else if (star != std::wstring_view::npos)
        {
            token = star + 1;
            character = ++retry;
        }
        else return false;
    }
    return pattern.find_first_not_of(L'*', token) == std::wstring_view::npos;
}

class ConfigIncludes
{
    fs::path root;
    std::set<fs::path> visited;
    std::string text;

    void Append(const fs::path& file, unsigned depth = 0)
    {
        if (depth > 128) throw std::runtime_error("Source LTX include nesting is too deep");
        const auto path = fs::canonical(file);
        const auto relative = path.lexically_relative(root);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            throw std::runtime_error("LTX include is outside the source config directory");
        if (!visited.insert(path).second) return;
        if (visited.size() > 8192)
            throw std::runtime_error("Too many source LTX includes");
        if (fs::file_size(path) >= 64 * 1024 * 1024)
            throw std::runtime_error("Source LTX exceeds the editor's parser limits");

        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Could not read " + path.u8string());
        std::string line;
        bool firstLine = true;
        while (std::getline(input, line))
        {
            if (firstLine && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
            firstLine = false;
            const auto start = line.find_first_not_of(" \t");
            if (start != std::string::npos && line.compare(start, 8, "#include") == 0)
            {
                const auto begin = line.find('"', start + 8);
                const auto end = begin == std::string::npos ? begin : line.find('"', begin + 1);
                if (end == std::string::npos || end == begin + 1)
                    throw std::runtime_error("Malformed LTX include in " + path.u8string());
                const auto include = path.parent_path() / fs::path(line.substr(begin + 1, end - begin - 1));
                const auto mask = include.native().find_first_of(L"*?");
                if (mask == std::wstring::npos)
                {
                    Append(include, depth + 1);
                    continue;
                }
                const auto directory = fs::canonical(include.parent_path());
                const auto includeRoot = directory.lexically_relative(root);
                if (includeRoot.empty() || includeRoot.is_absolute() || *includeRoot.begin() == "..")
                    throw std::runtime_error("LTX include mask is outside the source config directory");
                const auto pattern = include.filename().native();
                std::vector<fs::path> matches;
                for (const auto& entry : fs::directory_iterator(directory))
                    if (entry.is_regular_file() && MatchFilename(entry.path().filename().native(), pattern))
                        matches.push_back(entry.path());
                std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b)
                {
                    return _wcsicmp(a.c_str(), b.c_str()) < 0;
                });
                for (const auto& match : matches) Append(match, depth + 1);
            }
            else
            {
                if (line.size() >= 4095 || text.size() + line.size() >= 64 * 1024 * 1024 ||
                    line.find('\0') != std::string::npos)
                    throw std::runtime_error("Source LTX exceeds the editor's parser limits");
                text += line;
                text += '\n';
            }
        }
        if (input.bad()) throw std::runtime_error("Could not finish reading " + path.u8string());
    }

public:
    explicit ConfigIncludes(const fs::path& system) : root(system.parent_path()) { Append(system); }
    std::unique_ptr<CInifile> Read()
    {
        IReader reader(text.data(), static_cast<int>(text.size()));
        return std::make_unique<CInifile>(&reader);
    }
};

struct Source
{
    fs::path system;
    std::unique_ptr<CInifile> config;
};
std::vector<Source> sources;
fs::path selected;
std::vector<fs::path> remembered;
bool usedSelected = false;

fs::path Manifest()
{
    string_path path;
    FS.update_path(path, "$app_root$", "settings\\compiled_level_configs.txt");
    return fs::path(path);
}

CInifile::Sect* FindInSource(const fs::path& system, LPCSTR section)
{
    auto source = std::find_if(sources.begin(), sources.end(), [&](const Source& item) { return item.system == system; });
    if (source == sources.end())
    {
        sources.push_back({system, nullptr});
        source = sources.end() - 1;
        try
        {
            xrEditorOperationScope recover;
            source->config = ConfigIncludes(system).Read();
            Msg("Compiled level config: %s (%u sections)", system.u8string().c_str(), source->config->section_count());
        }
        catch (const xrEditorOperationError& e) { Msg("! Source config %s: %s", system.u8string().c_str(), e.details); }
        catch (const std::exception& e) { Msg("! Source config %s: %s", system.u8string().c_str(), e.what()); }
    }
    if (source->config && source->config->section_exist(section)) return &source->config->r_section(section);
    return nullptr;
}

CInifile::Sect* FindSection(LPCSTR section)
{
    if (!selected.empty())
        if (auto* result = FindInSource(selected, section))
        {
            usedSelected = true;
            return result;
        }
    for (const auto& path : remembered)
        if (path != selected)
            if (auto* result = FindInSource(path, section)) return result;
    return nullptr;
}
}

void InitializeCompiledLevelConfig()
{
    std::ifstream file(Manifest(), std::ios::binary);
    std::string path;
    while (remembered.size() < 128 && std::getline(file, path))
    {
        if (path.empty() || path.size() > 32768) continue;
        try
        {
            const auto system = fs::u8path(path);
            if (system.is_absolute() && _wcsicmp(system.filename().c_str(), L"system.ltx") == 0)
                remembered.push_back(system);
        }
        catch (const std::exception& e) { Msg("! Invalid compiled level config path: %s", e.what()); }
    }
    SetIniSectionFallback(FindSection);
}

void DestroyCompiledLevelConfig()
{
    SetIniSectionFallback(nullptr);
    selected.clear();
    remembered.clear();
    sources.clear();
}

void XrSE_Factory::SelectCompiledLevelConfig(const wchar_t* directory)
{
    selected.clear();
    usedSelected = false;
    if (!directory || !*directory) return;
    try
    {
        const auto level = fs::canonical(fs::path(directory));
        const auto data = level.parent_path().parent_path();
        if (_wcsicmp(level.parent_path().filename().c_str(), L"levels") != 0) return;
        for (const auto* folder : {L"configs", L"config"})
        {
            const auto system = data / folder / L"system.ltx";
            if (!fs::is_regular_file(system)) continue;
            selected = fs::canonical(system);
            return;
        }
    }
    catch (const std::exception& e) { Msg("! Could not locate source configs: %s", e.what()); }
}

CLASS_ID XrSE_Factory::compiled_entity_class(LPCSTR section)
{
    // The source class determines the binary layout. Settings lookups still
    // use the active SDK definitions first.
    if (!selected.empty())
        if (auto* definition = FindInSource(selected, section))
        {
            LPCSTR name = nullptr;
            if (definition->line_exist("class", &name) && name && *name)
            {
                usedSelected = true;
                return TEXT2CLSID(name);
            }
        }
    return pSettings->line_exist(section, "class") ? pSettings->r_clsid(section, "class") : 0;
}

void XrSE_Factory::RememberCompiledLevelConfig()
{
    if (!usedSelected || selected.empty() ||
        (!remembered.empty() && remembered.front() == selected)) return;
    remembered.erase(std::remove(remembered.begin(), remembered.end(), selected), remembered.end());
    remembered.insert(remembered.begin(), selected);
    try
    {
        const auto file = Manifest();
        fs::create_directories(file.parent_path());
        auto temp = file;
        temp += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        for (const auto& path : remembered) stream << path.u8string() << '\n';
        stream.close();
        if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING))
            throw std::runtime_error("Could not update the source config paths");
    }
    catch (const std::exception& e) { Msg("! Compiled level configs: %s", e.what()); }
}
