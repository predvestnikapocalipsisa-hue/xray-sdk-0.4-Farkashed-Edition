#include "stdafx.h"
#include "UIContentBrowser.h"

static const float kBaseTile = 72.f;
static const float kBasePad = 5.f;
static const float kBaseLabel = 26.f;

// ‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä
UIContentBrowser::UIContentBrowser()
    : m_RealTexture(nullptr)
    , m_RemoveTexture(nullptr)
    , m_bVisible(true)
    , m_bShowPreview(true)
    , m_TileScale(1.f)
    , m_Mode(CBM_OBJECTS)
    , m_ObjectToolRef(nullptr)
    , m_SpawnToolRef(nullptr)
    , m_View(CBV_ALL)
    , m_ViewDirty(false)
    , m_CacheNeedsAssign(false)
{
    m_SearchBuf[0] = 0;
    m_ObjectList = xr_new<UIItemListForm>();
    m_Props = xr_new<UIPropertiesForm>();

    // –°–Ω–∞—á–∞–ª–∞ —á–∏—Ç–∞–µ–º —Å–æ—Ö—Ä–∞–Ω–µ–Ω–Ω–æ–µ —Å–æ—Å—Ç–æ—è–Ω–∏–µ –∏–∑ —Ñ–∞–π–ª–∞!
    LoadUserState();

    m_ObjectList->SetOnItemFocusedEvent(
        TOnILItemFocused(this, &UIContentBrowser::OnItemFocused));
    Refresh();
}

UIContentBrowser::~UIContentBrowser()
{
    SaveUserState();
    if (m_RemoveTexture) m_RemoveTexture->Release();
    if (m_RealTexture)   m_RealTexture->Release();
    m_FolderHelper.ReleaseThumbnails();

    // –°–æ—Ö—Ä–∞–Ω—è–µ–º –∞–∫—Ç—É–∞–ª—å–Ω–æ–µ —Å–æ—Å—Ç–æ—è–Ω–∏–µ –ø—Ä–∏ –∑–∞–∫—Ä—ã—Ç–∏–∏/—É–¥–∞–ª–µ–Ω–∏–∏ –æ–∫–Ω–∞
    SaveUserState();

    xr_delete(m_Props);
    xr_delete(m_ObjectList);
}

// ‚"Ä‚"Ä mode switch ‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä
void UIContentBrowser::SetMode(EContentBrowserMode mode)
{
    if (m_Mode == mode) return;
    m_Mode = mode;
    Refresh();
}

void UIContentBrowser::RefreshObjects()
{
    // Reload cached editable objects from their original files, then rebuild
    // scene-instance surfaces and refresh every object list that is visible.
    Lib.ReloadObjects();

    ESceneObjectTool* objectTool = dynamic_cast<ESceneObjectTool*>(Scene->GetTool(OBJCLASS_SCENEOBJECT));
    if (objectTool)
        objectTool->ReloadReferences();

    Scene->OnObjectsUpdate();
    LTools->OnObjectsUpdate();

    if (objectTool && objectTool->pForm)
    {
        UIObjectTool* objectForm = dynamic_cast<UIObjectTool*>(objectTool->pForm);
        if (objectForm)
            objectForm->RefreshList();
    }

    m_FolderHelper.ReleaseThumbnails();
    Refresh();
}
static xr_string UTF8ToLower(const char* str)
{
    if (!str || !str[0]) return "";

    // –ò—Å–ø–æ–ª—å–∑—É–µ–º –∫–æ–Ω–≤–µ—Ä—Ç–∞—Ü–∏—é —á–µ—Ä–µ–∑ CP1251 –¥–ª—è –±–µ–∑–æ–ø–∞—Å–Ω–æ–≥–æ tolower —Å –ø–æ–¥–¥–µ—Ä–∂–∫–æ–π –∫–∏—Ä–∏–ª–ª–∏—Ü—ã
    xr_string cp1251 = XrUIManager::ConvertUTF8ToCP1251(str);
    if (!cp1251.empty())
        CharLowerBuffA(&cp1251[0], (DWORD)cp1251.size());
    return XrUIManager::ConvertCP1251ToUTF8(cp1251.c_str());
}

// ‚"Ä‚"Ä Refresh ‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä
void UIContentBrowser::Refresh()
{
    if (m_RealTexture) m_RemoveTexture = m_RealTexture;
    m_RealTexture = nullptr;
    m_Props->ClearProperties();
    m_CurrentItem = "";
    m_SearchBuf[0] = 0;
    m_FolderHelper.m_CurrentPath = "";
    m_FolderHelper.ReleaseThumbnails();

    m_CachedItems.clear();
    m_CacheNeedsAssign = true;

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
            AddCachedItem(name.c_str(), ListItem::flDrawThumbnail, 0);
        }
    }
    else if (m_Mode == CBM_SPAWN)
    {
        AddCachedItem(RPOINT_CHOOSE_NAME, 0, RPOINT_CHOOSE_NAME);
        AddCachedItem(ENVMOD_CHOOSE_NAME, 0, ENVMOD_CHOOSE_NAME);
        CInifile::Root& data = ((CInifile*)pSettings)->sections();
        for (CInifile::RootIt it = data.begin(); it != data.end(); it++)
        {
            LPCSTR val;
            if ((*it)->line_exist("$spawn", &val))
            {
                shared_str caption = pSettings->r_string_wb((*it)->Name, "$spawn");
                if (caption.size())
                    AddCachedItem(caption.c_str(), ListItem::flDrawThumbnail, (LPVOID)*(*it)->Name);
            }
        }
    }
    else
    {
        FS_FileSet lst;
        FS.file_list(lst, _groups_, FS_ListFiles, "*.group");
        for (auto& it : lst)
        {
            xr_string name = it.name;
            size_t dot = name.rfind('.');
            if (dot != xr_string::npos) name.resize(dot);
            AddCachedItem(name.c_str(), ListItem::flDrawThumbnail, 0);
        }
    }

    ApplyView();
}

// ‚"Ä‚"Ä OnItemFocused ‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä
void UIContentBrowser::AddCachedItem(const char* key, int flags, LPVOID data)
{
    if (!key) return;
    CachedItem entry; entry.key = key; entry.flags = flags; entry.data = data;
    m_CachedItems.push_back(entry);
}

void UIContentBrowser::ApplyView()
{
    if (m_CacheNeedsAssign)
    {
        ListItemsVec items;
        for (const CachedItem& entry : m_CachedItems)
            LHelper().CreateItem(items, entry.key.c_str(), 0, entry.flags, entry.data);
        m_ObjectList->AssignItems(items);
        m_FolderHelper.RebuildFolders(items);
        m_CacheNeedsAssign = false;
    }

    const ListItemsVec& allItems = m_ObjectList->GetItems();
    for (ListItem* item : allItems)
    {
        bool visible = m_View == CBV_ALL;
        if (m_View != CBV_ALL)
        {
            const xr_vector<xr_string>& source = m_View == CBV_RECENT ? m_RecentItems : m_FavoriteItems;
            visible = std::find(source.begin(), source.end(), xr_string(item->Key())) != source.end();
        }
        item->Visible(visible);
    }
    m_ViewDirty = false;
}
void UIContentBrowser::OnItemFocused(ListItem* item)
{
    if (m_RealTexture) m_RemoveTexture = m_RealTexture;
    m_RealTexture = nullptr;
    m_Props->ClearProperties();
    m_CurrentItem = "";

    if (!item) return;
    m_CurrentItem = item->Key();
    RememberItem(*m_CurrentItem);

    if (m_Mode == CBM_OBJECTS)
    {
        // –ó–∞–≥—Ä—É–∑–∏—Ç—å –ø—Ä–µ–≤—å—é—à–∫—É
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
    else if (m_Mode == CBM_SPAWN)
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
    else
    {
        ESceneGroupTool* tool = dynamic_cast<ESceneGroupTool*>(Scene->GetTool(OBJCLASS_GROUP));
        if (tool && m_CurrentItem.size())
            tool->SetCurrentObject(*m_CurrentItem);

		EGroupThumbnail* thm = xr_new<EGroupThumbnail>(*m_CurrentItem, true);
		if (thm && thm->Valid())
		{
			thm->Update((ImTextureID&)m_RealTexture);
			PropItemVec info;
			thm->FillInfo(info);
			m_Props->AssignItems(info);
		}
		xr_delete(thm);
    }
}

// ‚"Ä‚"Ä breadcrumb ‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä
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

        const xr_string utf8Part = XrUIManager::ConvertCP1251ToUTF8(parts[i].c_str());
        if (i == parts.size() - 1)
            ImGui::TextUnformatted(utf8Part.c_str());
        else
            if (ImGui::SmallButton(utf8Part.c_str()))
                m_FolderHelper.m_CurrentPath = subPath.c_str();
        ImGui::SameLine(0, 2);
    }
    ImGui::NewLine();
}

// ‚"Ä‚"Ä tile grid ‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä
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

        // ‚"Ä‚"Ä –ö–æ—Ä—Ä–µ–∫—Ç–Ω—ã–π –ø–æ–∏—Å–∫ —Å –ø–æ–¥–¥–µ—Ä–∂–∫–æ–π –∫–∏—Ä–∏–ª–ª–∏—Ü—ã UTF-8 ‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä
        if (doSearch)
        {
            const char* lbl = strrchr(name, '\\');
            lbl = lbl ? lbl + 1 : name;

            // –ö–æ–Ω–≤–µ—Ä—Ç–∏—Ä—É–µ–º –∏–º—è –ø—Ä–µ–¥–º–µ—Ç–∞ –∏–∑ CP1251 –≤ UTF-8
            xr_string nameUtf8 = XrUIManager::ConvertCP1251ToUTF8(lbl);

            // –ü–µ—Ä–µ–≤–æ–¥–∏–º –æ–±–µ —Å—Ç—Ä–æ–∫–∏ –≤ lower case —Å –ø–æ–¥–¥–µ—Ä–∂–∫–æ–π UTF-8 –∫–∏—Ä–∏–ª–ª–∏—Ü—ã
            xr_string s = UTF8ToLower(nameUtf8.c_str());
            xr_string q = UTF8ToLower(m_SearchBuf); // m_SearchBuf –ø—Ä–∏—Ö–æ–¥–∏—Ç –æ—Ç ImGui —É–∂–µ –≤ UTF-8

            if (s.find(q) == xr_string::npos)
                continue;
        }

        const ListItemsVec& sel = m_ObjectList->m_SelectedItems;
        bool selected = (!sel.empty() && sel.back() == item) || item->selected;

        ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::PushID(item);
        bool clicked = ImGui::InvisibleButton("##cb_tile", ImVec2(cellW, cellH));
        bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        ImVec2 winMin = ImGui::GetWindowPos();
        ImVec2 winMax = ImVec2(winMin.x + ImGui::GetWindowWidth(),
            winMin.y + ImGui::GetWindowHeight());
        bool isVisible = (cursor.y + cellH > winMin.y) && (cursor.y < winMax.y);

        if (isVisible)
        {
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
            if (m_Mode == CBM_OBJECTS || m_Mode == CBM_GROUP)
            {
                thumb = m_FolderHelper.GetThumb(key);
                if (!thumb)
                {
                    // Groups store their screenshots in group thumbnails; object items
                    // use the regular object thumbnail format.
                    const ECustomThumbnail::THMType thumbType =
                        m_Mode == CBM_GROUP ? ECustomThumbnail::ETGroup : ECustomThumbnail::ETObject;
                    EImageThumbnail* thm = m_Mode == CBM_GROUP
						? static_cast<EImageThumbnail*>(xr_new<EGroupThumbnail>(key, true))
						: ImageLib.CreateThumbnail(key, thumbType, true);
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
                const xr_string utf8Display = XrUIManager::ConvertCP1251ToUTF8(disp);
                dl->AddText(ImGui::GetFont(), 11.f,
                    ImVec2(cursor.x + tilePad, ty), tc, utf8Display.c_str());
                dl->PopClipRect();
            }
        }

        if (hovered && key) { const xr_string utf8Key = XrUIManager::ConvertCP1251ToUTF8(key); ImGui::SetTooltip("%s", utf8Key.c_str()); }

        bool starClicked = false;
        if (key)
        {
            const ImVec2 starMin(cursor.x + cellW - 23.f, cursor.y + 2.f);
            const ImVec2 starMax(starMin.x + 20.f, starMin.y + 20.f);
            const bool starHovered = ImGui::IsMouseHoveringRect(starMin, starMax);
            if (isVisible)
            {
                const ImU32 starColor = IsFavorite(key) ? IM_COL32(255, 205, 65, 255) : IM_COL32(175, 180, 190, 210);
                dl->AddText(ImGui::GetFont(), 16.f, starMin, starColor, "\xE2\x98\x85");
            }
            if (starHovered) ImGui::SetTooltip(IsFavorite(key) ? "Remove from Favorite" : "Add to Favorite");
            starClicked = starHovered && ImGui::IsMouseClicked(0);
            if (starClicked) ToggleFavorite(key);
        }

        if (clicked && !starClicked)
        {
            m_ObjectList->SelectItem(key);
            RememberItem(key);
        }


        col++;
        if (col < cols) ImGui::SameLine();
        else            col = 0;
    }
    if (m_ViewDirty) ApplyView();
    ImGui::PopStyleVar();
}

void UIContentBrowser::RememberItem(const char* key)
{
    if (!key || !key[0]) return;
    m_RecentItems.erase(std::remove(m_RecentItems.begin(), m_RecentItems.end(), xr_string(key)), m_RecentItems.end());
    m_RecentItems.insert(m_RecentItems.begin(), key);
    if (m_RecentItems.size() > 25) m_RecentItems.resize(25);
    SaveUserState();
}

bool UIContentBrowser::IsFavorite(const char* key) const
{
    return key && std::find(m_FavoriteItems.begin(), m_FavoriteItems.end(), xr_string(key)) != m_FavoriteItems.end();
}

void UIContentBrowser::ToggleFavorite(const char* key)
{
    if (!key || !key[0]) return;
    if (IsFavorite(key)) m_FavoriteItems.erase(std::remove(m_FavoriteItems.begin(), m_FavoriteItems.end(), xr_string(key)), m_FavoriteItems.end());
    else m_FavoriteItems.push_back(key);
    SaveUserState();
    m_ViewDirty = true;
}


void UIContentBrowser::LoadUserState()
{
    string_path fn;
    if (!FS.exist(fn, "$local_root$", UserCfgPath()) &&
        !FS.exist(fn, "$sdk_root$", UserCfgPath()))
        return;

    CInifile ini(fn, true);
    if (ini.section_exist("recent"))
        for (int i = 0; i < 25; ++i) { char k[32]; sprintf_s(k, "item%d", i); if (ini.line_exist("recent", k)) m_RecentItems.push_back(ini.r_string("recent", k)); }
    if (ini.section_exist("favorites"))
        for (int i = 0; i < 500; ++i) { char k[32]; sprintf_s(k, "item%d", i); if (ini.line_exist("favorites", k)) m_FavoriteItems.push_back(ini.r_string("favorites", k)); }
    if (!ini.section_exist("state"))
        return;

    if (ini.line_exist("state", "visible"))
        m_bVisible = ini.r_bool("state", "visible");
    if (ini.line_exist("state", "show_preview"))
        m_bShowPreview = ini.r_bool("state", "show_preview");
}

void UIContentBrowser::SaveUserState()
{
    string_path fn;
    FS.update_path(fn, "$local_root$", UserCfgPath());

    CInifile ini(fn, false, false, true);
    ini.w_bool("state", "visible", m_bVisible);
    ini.w_bool("state", "show_preview", m_bShowPreview);
    for (int i = 0; i < (int)m_RecentItems.size(); ++i) { char k[32]; sprintf_s(k, "item%d", i); ini.w_string("recent", k, m_RecentItems[i].c_str()); }
    for (int i = 0; i < (int)m_FavoriteItems.size(); ++i) { char k[32]; sprintf_s(k, "item%d", i); ini.w_string("favorites", k, m_FavoriteItems[i].c_str()); }
    ini.save_as(fn);
}

// ‚"Ä‚"Ä main Draw ‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä‚"Ä
void UIContentBrowser::Draw()
{
    if (m_RemoveTexture) { m_RemoveTexture->Release(); m_RemoveTexture = nullptr; }
    if (!m_bVisible) return;

    {
        ObjClassID target = LTools->GetTarget();
        EContentBrowserMode wantedMode = CBM_OBJECTS;
        if (target == OBJCLASS_SPAWNPOINT) wantedMode = CBM_SPAWN;
        else if (target == OBJCLASS_GROUP) wantedMode = CBM_GROUP;
        if (wantedMode != m_Mode)
            SetMode(wantedMode);
    }

    ImGui::SetNextWindowSize(ImVec2(900, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Content Browser", &m_bVisible,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::End(); return;
    }

    const char* modeLabel = m_Mode == CBM_SPAWN ? "Spawn Elements" :
        m_Mode == CBM_GROUP ? "Groups" : "Objects";
    ImGui::TextDisabled("[%s]", modeLabel);
    ImGui::SameLine(0, 8);
    if (ImGui::SmallButton("Refresh")) Refresh();
    ImGui::SameLine(0, 4);
    if (ImGui::SmallButton("Sync Objects")) RefreshObjects();
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
    if (ImGui::Checkbox("Preview", &m_bShowPreview))
        SaveUserState();

    ImGui::Separator();
    DrawBreadcrumb();
    ImGui::Separator();

    const float previewW = (m_bShowPreview && (m_Mode == CBM_OBJECTS || m_Mode == CBM_GROUP)) ? 200.f : 0.f;
    const float bodyH = ImGui::GetContentRegionAvail().y;

    ImGui::BeginChild("##cb_tree", ImVec2(160, bodyH), true);
    if (ImGui::Selectable("Recent", m_View == CBV_RECENT)) { m_View = CBV_RECENT; m_FolderHelper.m_CurrentPath = ""; ApplyView(); }
    if (ImGui::Selectable("Favorite", m_View == CBV_FAVORITES)) { m_View = CBV_FAVORITES; m_FolderHelper.m_CurrentPath = ""; ApplyView(); }
    ImGui::Separator();
    bool rootClicked = false;
    m_FolderHelper.DrawFolderTreePanel(m_ObjectList, &rootClicked, m_View == CBV_ALL);
    if (rootClicked && m_View != CBV_ALL) { m_View = CBV_ALL; ApplyView(); }
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
            const xr_string utf8Display = XrUIManager::ConvertCP1251ToUTF8(disp ? disp + 1 : k);
            ImGui::TextWrapped("%s", utf8Display.c_str());
            ImGui::Separator();
            m_Props->Draw();
        }
        ImGui::EndChild();
    }

    ImGui::End();
}
