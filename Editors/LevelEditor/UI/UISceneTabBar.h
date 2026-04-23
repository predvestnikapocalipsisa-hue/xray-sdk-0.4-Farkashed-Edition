#pragma once
#include <string>
#include <vector>
#include "../XrEUI/imgui.h"

// ============================================================
//  UISceneTabBar
//  Browser-style scene tabs above the viewport.
//  Each tab represents one scene file.  Only one scene is
//  "active" at a time; switching saves the current and loads
//  the requested one (same behavior as File > Open).
// ============================================================

struct SceneTab
{
    xr_string   displayName;    // short name shown on the tab
    xr_string   filePath;       // full map path (empty = unsaved new)
    bool        isModified;     // show "●" indicator
    bool        wantClose;      // close requested this frame

    SceneTab() : isModified(false), wantClose(false) {}
    SceneTab(const char* path, const char* name)
        : filePath(path ? path : ""), displayName(name ? name : "Untitled"),
          isModified(false), wantClose(false) {}
};

class UISceneTabBar
{
public:
    // ---- Data ----
    static xr_vector<SceneTab> s_tabs;   // ordered list of tabs
    static int                 s_activeIdx;  // index of the active tab (-1 = none)

    // ---- API called from UIMainForm / UIMainMenuForm ----

    // Notify the tab bar that a scene was just opened/created.
    // Call AFTER the engine has actually loaded the scene.
    static void OnSceneLoaded(const char* filePath, const char* displayName);

    // Notify that the current scene was saved (clears modified flag).
    static void OnSceneSaved(const char* filePath);

    // Notify that the current scene's modified state changed.
    static void OnSceneModified(bool modified);

    // Close the tab at idx, switching to adjacent tab if needed.
    // Returns true if the switch was performed without user cancel.
    static bool CloseTab(int idx);

    // Draw the tab bar.  Call once per frame before m_Render->Draw().
    // Returns true if the active scene was switched this frame
    // (caller should redraw the scene).
    static bool Draw();

    // Returns height of the tab bar in pixels (for layout).
    static float GetHeight() { return s_tabBarHeight; }

    // True when there is more than one tab open.
    static bool HasMultipleTabs() { return s_tabs.size() > 1; }

private:
    static float s_tabBarHeight;  // cached last frame height

    // Switch to tab 'idx': save current → load new.
    static void SwitchToTab(int idx);

    // Derive a display name from a file path.
    static xr_string NameFromPath(const char* path);
};
