#pragma once

class UITileToolHelper
{
public:
    UITileToolHelper()
        : m_IconFolder(nullptr), m_IconObject(nullptr)
    {
        m_CurrentPath = "";
    }

    ~UITileToolHelper()
    {
        ReleaseThumbnails();
    }

    // ── folder tree ──────────────────────────────────────────────────────────

    void RebuildFolders(const ListItemsVec& items)
    {
        m_Folders.clear();
        xr_set<xr_string> seen;
        for (ListItem* item : items)
        {
            if (!item->Visible()) continue;
            const char* key = item->Key();
            if (!key) continue;
            const char* p = key;
            while (const char* sep = strchr(p, '\\'))
            {
                xr_string folder;
                folder.assign(key, (size_t)(sep - key));
                seen.insert(folder);
                p = sep + 1;
            }
        }
        for (const xr_string& s : seen)
            m_Folders.push_back(s);
    }

    const char* CurrentFilter() const
    {
        const char* p = *m_CurrentPath;
        return (p && p[0]) ? p : nullptr;
    }

    void DrawFolderTreePanel(UIItemListForm* /*list*/)
    {
        ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_SpanFullWidth |
            ImGuiTreeNodeFlags_DefaultOpen;
        if (!*m_CurrentPath) rootFlags |= ImGuiTreeNodeFlags_Selected;

        if (m_IconFolder) { ImGui::Image(m_IconFolder, ImVec2(14, 14)); ImGui::SameLine(0, 4); }
        bool rootOpen = ImGui::TreeNodeEx("##root", rootFlags, "All");
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            m_CurrentPath = "";
        if (rootOpen)
        {
            DrawFolderNode("", 0);
            ImGui::TreePop();
        }
    }

    void LoadThumbnails(const ListItemsVec& items,
        ECustomThumbnail::THMType type = ECustomThumbnail::ETObject)
    {
        ReleaseThumbnails();
        for (ListItem* item : items)
        {
            const char* key = item->Key();
            if (!key) continue;
            if (m_Thumbs.count(key)) continue;

            EImageThumbnail* thm = static_cast<EImageThumbnail*>(
                CreateThumbnail(key, type, true));
            if (!thm) continue;

            ImTextureID tex = nullptr;
            thm->Update(tex);
            xr_delete(thm);

            if (tex)
                m_Thumbs[key] = tex;
        }
    }

    ImTextureID GetThumb(const char* key) const
    {
        if (!key) return nullptr;
        auto it = m_Thumbs.find(key);
        return (it != m_Thumbs.end()) ? it->second : nullptr;
    }

    void StoreThumb(const char* key, ImTextureID tex)
    {
        if (!key || !tex) return;
        m_Thumbs[key] = tex;
    }

    void ReleaseThumbnails()
    {
        for (auto& kv : m_Thumbs)
            if (kv.second) kv.second->Release();
        m_Thumbs.clear();
    }

    // ── public state ─────────────────────────────────────────────────────────
    shared_str   m_CurrentPath;
    ImTextureID  m_IconFolder;
    ImTextureID  m_IconObject;

private:
    xr_vector<xr_string>            m_Folders;
    xr_map<xr_string, ImTextureID>  m_Thumbs;

    void DrawFolderNode(const xr_string& prefix, int depth)
    {
        for (const xr_string& folder : m_Folders)
        {
            if (!prefix.empty())
            {
                if (folder.size() <= prefix.size() + 1)            continue;
                if (folder.compare(0, prefix.size(), prefix) != 0) continue;
                if (folder[prefix.size()] != '\\')                  continue;
                xr_string tail = folder.substr(prefix.size() + 1);
                if (tail.find('\\') != xr_string::npos)             continue;
            }
            else
            {
                if (folder.find('\\') != xr_string::npos)          continue;
            }

            xr_string childPfx = folder + "\\";
            bool hasChildren = false;
            for (const xr_string& f2 : m_Folders)
            {
                if (f2.size() > childPfx.size() &&
                    f2.compare(0, childPfx.size(), childPfx) == 0)
                {
                    xr_string t2 = f2.substr(childPfx.size());
                    if (t2.find('\\') == xr_string::npos)
                    {
                        hasChildren = true; break;
                    }
                }
            }

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth |
                ImGuiTreeNodeFlags_OpenOnArrow;
            if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
            if (0 == xr_strcmp(*m_CurrentPath, folder.c_str()))
                flags |= ImGuiTreeNodeFlags_Selected;

            const char* label = folder.c_str();
            if (const char* last = strrchr(folder.c_str(), '\\')) label = last + 1;

            if (m_IconFolder) { ImGui::Image(m_IconFolder, ImVec2(14, 14)); ImGui::SameLine(0, 4); }

            bool open = ImGui::TreeNodeEx(folder.c_str(), flags, "%s", label);
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                m_CurrentPath = folder.c_str();
            if (open) { DrawFolderNode(folder, depth + 1); ImGui::TreePop(); }
        }
    }
};