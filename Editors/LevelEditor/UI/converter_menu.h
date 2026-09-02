
#pragma once

#include <windows.h>
#include <string>
#include <cstring>
#include <vector>
#include <fstream>
#include <sstream>
#include <map>

namespace ConverterMenu
{
    namespace detail
    {
        inline bool& IsOpenRef()
        {
            static bool s_isOpen = true;
            return s_isOpen;
        }

        inline std::string& SdkRootRef()
        {
            static std::string s_sdkRoot = ".";
            return s_sdkRoot;
        }

        inline char* LevelBuf()
        {
            static char s_buf[256] = "default:mp_atp_test";
            return s_buf;
        }

        inline char* OutBuf()
        {
            static char s_buf[256] = "mp_atp_test123";
            return s_buf;
        }

        inline char* ModeBuf()
        {
            static char s_buf[64] = "le";
            return s_buf;
        }

        inline bool& WithLodsRef()
        {
            static bool s_withLods = true;
            return s_withLods;
        }

        inline std::string& LastStatusRef()
        {
            static std::string s_status;
            return s_status;
        }

        inline bool& LastRunOkRef()
        {
            static bool s_ok = true;
            return s_ok;
        }

        inline std::string& DetectStatusRef()
        {
            static std::string s_detectStatus;
            return s_detectStatus;
        }

        inline bool& DetectOkRef()
        {
            static bool s_detectOk = false;
            return s_detectOk;
        }

        inline std::string Trim(const std::string& s)
        {
            size_t a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) return "";
            size_t b = s.find_last_not_of(" \t\r\n");
            return s.substr(a, b - a + 1);
        }

        inline std::string DirOf(const std::string& filePath)
        {
            size_t pos = filePath.find_last_of("\\/");
            if (pos == std::string::npos) return ".";
            return filePath.substr(0, pos);
        }

        inline std::string JoinPath(const std::string& base, const std::string& rel)
        {
            if (rel.empty()) return base;
            std::string b = base;
            while (!b.empty() && (b.back() == '\\' || b.back() == '/'))
                b.pop_back();
            std::string r = rel;
            size_t start = 0;
            while (start < r.size() && (r[start] == '\\' || r[start] == '/'))
                ++start;
            r = r.substr(start);
            return b + "\\" + r;
        }

        inline bool FileExists(const std::string& path)
        {
            DWORD attr = GetFileAttributesA(path.c_str());
            return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
        }

        inline bool DirExists(const std::string& path)
        {
            DWORD attr = GetFileAttributesA(path.c_str());
            return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
        }

        inline std::string ExeDir()
        {
            char buf[MAX_PATH] = {};
            GetModuleFileNameA(nullptr, buf, MAX_PATH);
            return DirOf(std::string(buf));
        }

        inline bool FindFsGameLtx(std::string& outPath, int maxLevelsUp = 8)
        {
            std::string dir = ExeDir();
            for (int i = 0; i <= maxLevelsUp; ++i)
            {
                std::string candidate = dir + "\\fsgame.ltx";
                if (FileExists(candidate))
                {
                    outPath = candidate;
                    return true;
                }
                std::string parent = DirOf(dir);
                if (parent.empty() || parent == dir)
                    break;
                dir = parent;
            }
            return false;
        }

        struct LtxEntry
        {
            std::string rootToken; 
            std::string relPath;   
        };

        inline bool ParseLtx(const std::string& ltxPath, std::map<std::string, LtxEntry>& outEntries)
        {
            std::ifstream file(ltxPath.c_str());
            if (!file.is_open())
                return false;

            std::string line;
            while (std::getline(file, line))
            {
                std::string trimmed = Trim(line);
                if (trimmed.empty() || trimmed[0] == ';')
                    continue;

                size_t eq = trimmed.find('=');
                if (eq == std::string::npos)
                    continue;

                std::string name = Trim(trimmed.substr(0, eq));
                if (name.size() < 2 || name.front() != '$' || name.back() != '$')
                    continue;

                std::string rest = trimmed.substr(eq + 1);

                std::vector<std::string> fields;
                std::stringstream ss(rest);
                std::string field;
                while (std::getline(ss, field, '|'))
                    fields.push_back(Trim(field));

                if (fields.size() < 3)
                    continue;

                LtxEntry entry;
                entry.rootToken = fields[2];
                entry.relPath = (fields.size() >= 4) ? fields[3] : "";

                outEntries[name] = entry;
            }
            return true;
        }

        inline std::string ResolveAlias(
            const std::string& name,
            const std::map<std::string, LtxEntry>& entries,
            const std::string& fsRootDir,
            int depth = 0)
        {
            if (name == "$fs_root$")
                return fsRootDir;

            if (depth > 16)
                return fsRootDir;

            std::map<std::string, LtxEntry>::const_iterator it = entries.find(name);
            if (it == entries.end())
            {
                return name;
            }

            const LtxEntry& e = it->second;

            std::string base;
            if (!e.rootToken.empty() && e.rootToken.front() == '$' && e.rootToken.back() == '$')
                base = ResolveAlias(e.rootToken, entries, fsRootDir, depth + 1);
            else
                base = e.rootToken;

            return JoinPath(base, e.relPath);
        }

    }

    inline void SetSdkRoot(const std::string& sdkRoot)
    {
        detail::SdkRootRef() = sdkRoot;
    }

    inline const std::string& GetSdkRoot()
    {
        return detail::SdkRootRef();
    }

    inline bool AutoDetectSdkRoot()
    {
        std::string ltxPath;
        if (!detail::FindFsGameLtx(ltxPath))
        {
            detail::DetectOkRef() = false;
            detail::DetectStatusRef() = "fsgame.ltx не найден рядом с exe (поднимался по дереву папок)";
            return false;
        }

        std::map<std::string, detail::LtxEntry> entries;
        if (!detail::ParseLtx(ltxPath, entries))
        {
            detail::DetectOkRef() = false;
            detail::DetectStatusRef() = "Не удалось открыть fsgame.ltx: " + ltxPath;
            return false;
        }

        if (entries.find("$sdk_root$") == entries.end())
        {
            detail::DetectOkRef() = false;
            detail::DetectStatusRef() = "В fsgame.ltx не найден алиас $sdk_root$: " + ltxPath;
            return false;
        }

        std::string fsRootDir = detail::DirOf(ltxPath);
        std::string resolved = detail::ResolveAlias("$sdk_root$", entries, fsRootDir);

        if (resolved.empty() || !detail::DirExists(resolved))
        {
            detail::DetectOkRef() = false;
            detail::DetectStatusRef() = "sdk_root указывает на несуществующую папку: " + resolved;
            return false;
        }

        detail::SdkRootRef() = resolved;
        detail::DetectOkRef() = true;
        return true;
    }

    inline const std::string& GetDetectStatus()
    {
        return detail::DetectStatusRef();
    }

    inline bool IsOpen()
    {
        return detail::IsOpenRef();
    }

    inline void SetOpen(bool open)
    {
        detail::IsOpenRef() = open;
    }

    inline void Open()
    {
        detail::IsOpenRef() = true;
    }

    inline void Close()
    {
        detail::IsOpenRef() = false;
    }

    inline void ToggleOpen()
    {
        detail::IsOpenRef() = !detail::IsOpenRef();
    }

    namespace detail
    {
        inline bool RunConverter(const std::string& args, std::string& outStatus)
        {
            std::string converterDir = SdkRootRef() + "\\converter";
            std::string batPath = converterDir + "\\converter.bat";

            std::string cmdLine = "cmd.exe /c \"\"" + batPath + "\" " + args + "\"";

            std::vector<char> buf(cmdLine.begin(), cmdLine.end());
            buf.push_back('\0');

            STARTUPINFOA si;
            PROCESS_INFORMATION pi;
            ZeroMemory(&si, sizeof(si));
            si.cb = sizeof(si);
            ZeroMemory(&pi, sizeof(pi));

            BOOL ok = CreateProcessA(
                nullptr,
                buf.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_NEW_CONSOLE,
                nullptr,
                converterDir.c_str(),
                &si,
                &pi
            );

            if (ok)
            {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                outStatus = "Запущено: " + batPath;
                return true;
            }
            else
            {
                DWORD err = GetLastError();
                outStatus = "Ошибка запуска (код " + std::to_string(err) + "): " + batPath;
                return false;
            }
        }
    }

    inline void Draw()
    {
        if (!detail::IsOpenRef())
            return;

        {
            static bool s_autoDetectTried = false;
            if (!s_autoDetectTried)
            {
                s_autoDetectTried = true;
                AutoDetectSdkRoot();
            }
        }

        ImGui::SetNextWindowSize(ImVec2(460, 320), ImGuiCond_FirstUseEver);

        bool* pOpen = &detail::IsOpenRef();
        if (!ImGui::Begin("Converter", pOpen, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("SDK root: %s", detail::SdkRootRef().c_str());
        {
            ImVec4 col = detail::DetectOkRef() ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                : ImVec4(1.0f, 0.7f, 0.3f, 1.0f);
            ImGui::TextColored(col, "%s", detail::DetectStatusRef().c_str());
        }
        ImGui::Text("Bat file: %s\\converter\\converter.bat", detail::SdkRootRef().c_str());
        ImGui::Separator();

        ImGui::InputText("-level", detail::LevelBuf(), 256);
        ImGui::InputText("-out", detail::OutBuf(), 256);
        ImGui::InputText("-mode", detail::ModeBuf(), 64);
        ImGui::Checkbox("-with_lods", &detail::WithLodsRef());

        ImGui::Separator();

        std::string args = std::string("-level ") + detail::LevelBuf() +
            " -out " + detail::OutBuf() +
            " -mode " + detail::ModeBuf();
        if (detail::WithLodsRef())
            args += " -with_lods";

        ImGui::TextWrapped("Команда: converter.bat %s", args.c_str());

        if (ImGui::Button("Запустить", ImVec2(120, 0)))
        {
            detail::LastRunOkRef() = detail::RunConverter(args, detail::LastStatusRef());
        }

        ImGui::SameLine();
        if (ImGui::Button("Закрыть"))
        {
            Close();
        }

        if (!detail::LastStatusRef().empty())
        {
            ImGui::Separator();
            ImVec4 col = detail::LastRunOkRef() ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            ImGui::TextColored(col, "%s", detail::LastStatusRef().c_str());
        }

        ImGui::End();
    }
}