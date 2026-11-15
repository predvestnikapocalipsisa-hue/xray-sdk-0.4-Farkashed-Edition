#pragma once
#include "UITileToolHelper.h"

// Forward declarations
class UIObjectTool;
class UISpawnTool;

enum EContentBrowserMode
{
    CBM_OBJECTS,
    CBM_SPAWN,
    CBM_GROUP,
};

enum EContentBrowserView { CBV_ALL, CBV_RECENT, CBV_FAVORITES };

class UIContentBrowser
{
public:
    UIContentBrowser();
    ~UIContentBrowser();

    void Draw();
    void Refresh();
    void RefreshObjects();
    void SetMode(EContentBrowserMode mode);

    // Call once after creating tool forms so the browser can notify them on selection
    void SetObjectTool(UIObjectTool* t) { m_ObjectToolRef = t; }
    void SetSpawnTool(UISpawnTool* t) { m_SpawnToolRef = t; }

    void Show() { m_bVisible = true; }
    void Hide() { m_bVisible = false; }
    bool IsVisible() { return m_bVisible; }

private:
    void OnItemFocused(ListItem* item);
    void DrawBreadcrumb();
    void DrawTileGridCustom();
    void LoadUserState();
    void SaveUserState();
    void RememberItem(const char* key);
    bool IsFavorite(const char* key) const;
    void ToggleFavorite(const char* key);
    void ApplyView();
    void AddCachedItem(const char* key, int flags, LPVOID data);
    static const char* UserCfgPath() { return "content_browser.ltx"; }


    UIItemListForm* m_ObjectList;
    UITileToolHelper  m_FolderHelper;
    UIPropertiesForm* m_Props;

    ImTextureID  m_RealTexture;
    ImTextureID  m_RemoveTexture;
    shared_str   m_CurrentItem;

    EContentBrowserMode m_Mode;

    // weak refs to tool forms — set externally, not owned
    UIObjectTool* m_ObjectToolRef;
    UISpawnTool* m_SpawnToolRef;

    char  m_SearchBuf[256];
    float m_TileScale;
    bool  m_bVisible;
    bool  m_bShowPreview;
    struct CachedItem { xr_string key; int flags; LPVOID data; };
    EContentBrowserView m_View;
    xr_vector<CachedItem> m_CachedItems;
    bool m_ViewDirty;
    bool m_CacheNeedsAssign;
    xr_vector<xr_string> m_RecentItems;
    xr_vector<xr_string> m_FavoriteItems;
};