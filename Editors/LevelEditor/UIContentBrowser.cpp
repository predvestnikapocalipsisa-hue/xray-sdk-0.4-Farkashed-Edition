#include "stdafx.h"
#include "UIContentBrowser.h"

static const float kBaseTile = 72.f;
static const float kBasePad = 5.f;
static const float kBaseLabel = 26.f;

// ─────────────────────────────────────────────────────────────────────────────
UIContentBrowser::UIContentBrowser()
    : m_RealTexture(nullptr)
    , m_RemoveTexture(nullptr)
    , m_bVisible(true)
    , m_bShowPreview(true)
    , m_TileScale(1.f)
    , m_Mode(CBM_OBJECTS)
    , m_ObjectToolRef(nullptr)
    , m_SpawnToolRef(nullptr)
{
    m_SearchBuf[0] = 0;
    m_ObjectList = xr_new<UIItemListForm>();
    m_Props = xr_new<UIPropertiesForm>();
    m_ObjectList->SetOnItemFocusedEvent(
        TOnILItemFocused(this, &UIContentBrowser::OnItemFocused));
    Refresh();
}

UIContentBrowser::~UIContentBrowser()
{
    if (m_RemoveTexture) m_RemoveTexture->Release();
    if (m_RealTexture)   m_RealTexture->Release();
    m_FolderHelper.ReleaseThumbnails();
    xr_delete(m_Props);
    xr_delete(m_ObjectList);
}

// ── mode switch ───────────────────────────────────────────────────────────────
void UIContentBrowser::SetMode(EContentBrowserMode mode)
{
    if (m_Mode == mode) return;
    m_Mode = mode;
    Refresh();
}

// ── Refresh ───────────────────────────────────────────────────────────────────
void UIContentBrowser::Refresh()
{
    if (m_RealTexture) m_RemoveTexture = m_RealTexture;
    m_RealTexture = nullptr;
    m_Props->ClearProperties();
    m_CurrentItem = "";
    m_SearchBuf[0] = 0;
    m_FolderHelper.m_CurrentPath = "";

    ListItemsVec items;

    if (m_Mode == CBM_OBJECTS)
    {
        // scene objects (.object files)
        FS_FileSet lst;
        FS.file_list(lst, "$objects$", FS_ListFiles, "*.object");
        for (auto& it : lst)
        {
            xr_string name = it.name;
            size_t dot = name.rfind('.');
            if (dot != xr_string::npos) name.resize(dot);
            LHelper().CreateItem(items, name.c_str(), 0, ListItem::flDrawThumbnail, 0);
        }
    }
    else
    {
        LHelper().CreateItem(items, RPOINT_CHOOSE_NAME, 0, 0, RPOINT_CHOOSE_NAME);
        LHelper().CreateItem(items, ENVMOD_CHOOSE_NAME, 0, 0, ENVMOD_CHOOSE_NAME);
        CInifile::Root& data = ((CInifile*)pSettings)->sections();
        for (CInifile::RootIt it = data.begin(); it != data.end(); it++)
        {
            LPCSTR val;
            if ((*it)->line_exist("$spawn", &val))
            {
                shared_str caption = pSettings->r_string_wb((*it)->Name, "$spawn");
                if (caption.size())
                    LHelper().CreateItem(items, caption.c_str(), 0,
                        ListItem::flDrawThumbnail,
                        (LPVOID) * (*it)->Name);
            }
        }
        m_FolderHelper.ReleaseThumbnails();
    }

    m_ObjectList->AssignItems(items);
    m_FolderHelper.RebuildFolders(items);
}

// ── OnItemFocused ─────────────────────────────────────────────────────────────
void UIContentBrowser::OnItemFocused(ListItem* item)
{
    if (m_RealTexture) m_RemoveTexture = m_RealTexture;
    m_RealTexture = nullptr;
    m_Props->ClearProperties();
    m_CurrentItem = "";

    if (!item) return;
    m_CurrentItem = item->Key();

    if (m_Mode == CBM_OBJECTS)
    {
        // Загрузить превьюшку
        EObjectThumbnail* thm = xr_new<EObjectThumbnail>(*m_CurrentItem);
        if (thm)
        {
            thm->Update((ImTextureID&)m_RealTexture);
            PropItemVec info;
            thm->FillInfo(info);
            m_Props->AssignItems(info);
            xr_delete(thm);
        }
        ExecCommand(COMMAND_CHANGE_TARGET, OBJCLASS_SCENEOBJECT);
        ExecCommand(COMMAND_CHANGE_ACTION, etaAdd);

        ESceneToolBase* base = Scene->GetTool(OBJCLASS_SCENEOBJECT);
        if (base && base->pForm)
        {
            UIObjectTool* t = dynamic_cast<UIObjectTool*>(base->pForm);
            if (t) t->SelectRef(*m_CurrentItem);
        }
    }
    else
    {
        ExecCommand(COMMAND_CHANGE_TARGET, OBJCLASS_SPAWNPOINT);
        ExecCommand(COMMAND_CHANGE_ACTION, etaAdd);

        ESceneToolBase* base = Scene->GetTool(OBJCLASS_SPAWNPOINT);
        if (base && base->pForm)
        {
            UISpawnTool* t = dynamic_cast<UISpawnTool*>(base->pForm);
            if (t) t->SelectRef(*m_CurrentItem);
        }
    }
}

// ── breadcrumb ────────────────────────────────────────────────────────────────
void UIContentBrowser::DrawBreadcrumb()
{
    if (ImGui::SmallButton("root"))
        m_FolderHelper.m_CurrentPath = "";
    ImGui::SameLine(0, 2);

    const char* cur = *m_FolderHelper.m_CurrentPath;
    if (!cur || !cur[0]) return;

    xr_string path(cur);
    xr_vector<xr_string> parts;
    size_t start = 0, pos;
    while ((pos = path.find('\\', start)) != xr_string::npos)
    {
        parts.push_back(path.substr(start, pos - start));
        start = pos + 1;
    }
    parts.push_back(path.substr(start));

    for (size_t i = 0; i < parts.size(); i++)
    {
        ImGui::TextDisabled("/"); ImGui::SameLine(0, 2);
        xr_string subPath;
        for (size_t j = 0; j <= i; j++)
        {
            if (j > 0) subPath += "\\"; subPath += parts[j];
        }

        if (i == parts.size() - 1)
            ImGui::TextUnformatted(parts[i].c_str());
        else
            if (ImGui::SmallButton(parts[i].c_str()))
                m_FolderHelper.m_CurrentPath = subPath.c_str();
        ImGui::SameLine(0, 2);
    }
    ImGui::NewLine();
}

// ── tile grid ─────────────────────────────────────────────────────────────────
void UIContentBrowser::DrawTileGridCustom()
{
    const float tileSize = kBaseTile * m_TileScale;
    const float tilePad = kBasePad;
    const float labelH = kBaseLabel;
    const float cellW = tileSize + tilePad * 2.f;
    const float cellH = tileSize + labelH + tilePad * 2.f;

    const float avail = ImGui::GetContentRegionAvail().x;
    const int   cols = std::max(1, (int)(avail / cellW));
    const char* filter = m_FolderHelper.CurrentFilter();
    bool doSearch = m_SearchBuf[0] != 0;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));

    int col = 0;
    for (ListItem* item : m_ObjectList->GetItems())
    {
        if (!item->Visible()) continue;

        const char* key = item->Key();
        const char* name = key ? key : "";

        if (filter && filter[0])
        {
            size_t plen = strlen(filter);
            if (strncmp(name, filter, plen) != 0)         continue;
            if (name[plen] != '\\')                        continue;
            if (strchr(name + plen + 1, '\\') != nullptr) continue;
        }

        if (doSearch)
        {
            const char* lbl = strrchr(name, '\\');
            lbl = lbl ? lbl + 1 : name;
            xr_string s(lbl), q(m_SearchBuf);
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            std::transform(q.begin(), q.end(), q.begin(), ::tolower);
            if (s.find(q) == xr_string::npos) continue;
        }

        const ListItemsVec& sel = m_ObjectList->m_SelectedItems;
        bool selected = (!sel.empty() && sel.back() == item) || item->selected;

        ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::PushID(item);
        bool clicked = ImGui::InvisibleButton("##cb_tile", ImVec2(cellW, cellH));
        bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        ImU32 bgCol = selected ? IM_COL32(44, 93, 135, 255)
            : hovered ? IM_COL32(65, 65, 65, 255)
            : IM_COL32(45, 45, 45, 200);
        ImU32 bdCol = selected ? IM_COL32(100, 160, 220, 255)
            : hovered ? IM_COL32(100, 100, 100, 255)
            : IM_COL32(70, 70, 70, 120);

        ImVec2 r0 = cursor;
        ImVec2 r1 = ImVec2(cursor.x + cellW, cursor.y + cellH);
        dl->AddRectFilled(r0, r1, bgCol, 3.f);
        dl->AddRect(r0, r1, bdCol, 3.f, 0, 1.f);

        ImVec2 iMin = ImVec2(cursor.x + tilePad, cursor.y + tilePad);
        ImVec2 iMax = ImVec2(iMin.x + tileSize, iMin.y + tileSize);

        ImTextureID thumb = nullptr;
        if (m_Mode == CBM_OBJECTS)
        {
            thumb = m_FolderHelper.GetThumb(key);
            if (!thumb)
            {
                // Check if tile rect is inside the visible scroll region
                ImVec2 winMin = ImGui::GetWindowPos();
                ImVec2 winMax = ImVec2(winMin.x + ImGui::GetWindowWidth(),
                    winMin.y + ImGui::GetWindowHeight());
                if (cursor.y < winMax.y && cursor.y + cellH > winMin.y)
                {
                    // Load this one thumbnail on demand
                    EObjectThumbnail* thm = xr_new<EObjectThumbnail>(key, true);
                    if (thm)
                    {
                        ImTextureID tex = nullptr;
                        thm->Update(tex);
                        xr_delete(thm);
                        if (tex)
                            m_FolderHelper.StoreThumb(key, tex);
                        thumb = m_FolderHelper.GetThumb(key);
                    }
                }
            }
        }
        if (thumb)
        {
            dl->AddImage(thumb, iMin, iMax, ImVec2(0, 0), ImVec2(1, 1));
        }
        else
        {
            dl->AddRectFilled(iMin, iMax, IM_COL32(45, 65, 90, 255), 2.f);
            dl->AddRect(iMin, iMax, IM_COL32(70, 100, 130, 200), 2.f);
            const char* disp = strrchr(name, '\\');
            disp = disp ? disp + 1 : name;
            char letter[2] = { disp[0] ? (char)toupper((unsigned char)disp[0]) : '?', 0 };
            ImVec2 ts = ImGui::CalcTextSize(letter);
            dl->AddText(ImGui::GetFont(), tileSize * 0.28f,
                ImVec2(iMin.x + (tileSize - ts.x) * 0.5f,
                    iMin.y + (tileSize - ts.y) * 0.5f),
                IM_COL32(160, 190, 210, 180), letter);
        }

        {
            const char* disp = strrchr(name, '\\');
            disp = disp ? disp + 1 : name;
            ImU32 tc = selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 200, 200, 255);
            float ty = iMax.y + 3.f;
            dl->PushClipRect(ImVec2(cursor.x + tilePad, ty),
                ImVec2(cursor.x + cellW - tilePad, ty + labelH), true);
            dl->AddText(ImGui::GetFont(), 11.f,
                ImVec2(cursor.x + tilePad, ty), tc, disp);
            dl->PopClipRect();
        }

        if (hovered && key) ImGui::SetTooltip("%s", key);

        if (clicked)
            m_ObjectList->SelectItem(key);

        col++;
        if (col < cols) ImGui::SameLine();
        else            col = 0;
    }
    ImGui::PopStyleVar();
}

// ── main Draw ─────────────────────────────────────────────────────────────────
void UIContentBrowser::Draw()
{
    if (m_RemoveTexture) { m_RemoveTexture->Release(); m_RemoveTexture = nullptr; }
    if (!m_bVisible) return;

    {
        ObjClassID target = LTools->GetTarget();
        EContentBrowserMode wantedMode =
            (target == OBJCLASS_SPAWNPOINT) ? CBM_SPAWN : CBM_OBJECTS;
        if (wantedMode != m_Mode)
            SetMode(wantedMode);
    }

    ImGui::SetNextWindowSize(ImVec2(900, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Content Browser", &m_bVisible,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::End(); return;
    }

    const char* modeLabel = (m_Mode == CBM_SPAWN) ? "Spawn Elements" : "Objects";
    ImGui::TextDisabled("[%s]", modeLabel);
    ImGui::SameLine(0, 8);
    if (ImGui::SmallButton("Refresh")) Refresh();
    ImGui::SameLine(0, 8);
    ImGui::TextDisabled("|"); ImGui::SameLine(0, 8);
    ImGui::SetNextItemWidth(180);
    ImGui::InputTextWithHint("##search", "Search...", m_SearchBuf, sizeof(m_SearchBuf));
    ImGui::SameLine(0, 8);
    ImGui::TextDisabled("|"); ImGui::SameLine(0, 8);
    ImGui::SetNextItemWidth(80);
    ImGui::SliderFloat("##scale", &m_TileScale, 0.5f, 2.f, "%.1fx");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tile size");
    ImGui::SameLine(0, 8);
    ImGui::Checkbox("Preview", &m_bShowPreview);

    ImGui::Separator();
    DrawBreadcrumb();
    ImGui::Separator();

    const float previewW = (m_bShowPreview && m_Mode == CBM_OBJECTS) ? 200.f : 0.f;
    const float bodyH = ImGui::GetContentRegionAvail().y;

    ImGui::BeginChild("##cb_tree", ImVec2(160, bodyH), true);
    m_FolderHelper.DrawFolderTreePanel(m_ObjectList);
    ImGui::EndChild();

    ImGui::SameLine(0, 3);

    float gridW = previewW > 0 ? -(previewW + 3) : 0;
    ImGui::BeginChild("##cb_grid", ImVec2(gridW, bodyH), false,
        ImGuiWindowFlags_HorizontalScrollbar);
    DrawTileGridCustom();
    ImGui::EndChild();

    if (previewW > 0)
    {
        ImGui::SameLine(0, 3);
        ImGui::BeginChild("##cb_preview", ImVec2(previewW, bodyH), true);
        if (m_RealTexture)
            ImGui::Image(m_RealTexture, ImVec2(previewW - 16, previewW - 16));
        else
            ImGui::Dummy(ImVec2(previewW - 16, previewW - 16));

        if (*m_CurrentItem)
        {
            ImGui::Separator();
            const char* k = *m_CurrentItem;
            const char* disp = strrchr(k, '\\');
            ImGui::TextWrapped("%s", disp ? disp + 1 : k);
            ImGui::Separator();
            m_Props->Draw();
        }
        ImGui::EndChild();
    }

    ImGui::End();
}