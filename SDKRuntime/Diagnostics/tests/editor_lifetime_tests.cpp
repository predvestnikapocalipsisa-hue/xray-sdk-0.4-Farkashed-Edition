#include "../../../Editors/XrECore/stdafx.h"
#include "../../../Editors/XrECore/Editor/Library.h"
#include "../../../Editors/XrECore/Editor/ui_main.h"
#include <iostream>
#include <stdexcept>

static void Require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
static void Add(ListItemsVec& items, const char* name)
{
    auto* item = xr_new<ListItem>(0);
    item->SetName(name);
    items.push_back(item);
}
// Model loading requests a redraw even without a viewport. Keep the real cache
// and loader, but provide an idle UI and defer GPU/collision buffer creation.
class IdleUI : public TUI
{
public:
    void OutUICursorPos() override {}
    void OutGridSize() override {}
    void OutInfo() override {}
    void RealUpdateScene() override {}
    char* GetCaption() override { static char caption[] = "tests"; return caption; }
    void OutCameraPos() override {}
    void SetStatus(LPCSTR, bool = true) override {}
    void ResetStatus() override {}
    bool ApplyShortCut(DWORD, TShiftState) override { return false; }
    bool ApplyGlobalShortCut(DWORD, TShiftState) override { return false; }
    LPCSTR EditorName() override { return "tests"; }
    LPCSTR EditorDesc() override { return "tests"; }
    void RegisterCommands() override {}
};
int main(int argc, char** argv)
{
    // Optional integration check: run with an asset name from a configured SDK
    // root to exercise the real model cache and discard-on-last-reference path.
    if (argc == 2)
    {
        Core.InitCore("stability_tests");
        {
            IdleUI idle;
            CCustomPreferences preferences;
            EPrefs = &preferences;
            preferences.object_flags.set(epoDiscardInstance, true);
            preferences.object_flags.set(epoDeffLoadRB | epoDeffLoadCF, true);
            Lib.OnCreate();
            for (int i = 0; i < 1000; ++i)
            {
                auto* object = Lib.CreateEditObject(argv[1]);
                Require(object != nullptr && Lib.ObjectCount() == 1, "model load failed");
                Lib.RemoveEditObject(object);
                Require(!object && Lib.ObjectCount() == 0, "last reference was not discarded");
            }
            Lib.OnDestroy();
            EPrefs = nullptr;
        }
        std::cout << "PASS: 1000 model load/discard cycles\n";
        FlushLog();
        Core.DestroyCore();
        return 0;
    }
    Memory._initialize();
    try
    {
        {
            UIItemListForm list;
            list.m_Flags.set(UIItemListForm::fMultiSelect, true);
            ListItemsVec first;
            Add(first, "folder");
            list.AssignItems(first, "folder");
            Require(list.m_SelectedItems.size() == 1, "existing selection lost");

            ListItemsVec replacement;
            Add(replacement, "folder\\tree");
            list.AssignItems(replacement, nullptr, true, true);
            Require(list.m_SelectedItems.empty(), "folder without an object restored as selection");
            list.SelectItem("missing\\tree");
            list.SelectItem("folder\\missing");
            Require(list.m_SelectedItems.empty(), "missing selection changed the list");

            for (int i = 0; i < 2000; ++i)
            {
                ListItemsVec items;
                Add(items, i % 2 ? "folder\\tree" : "other\\tree");
                list.AssignItems(items, "deleted\\tree", true, true);
                Require(list.m_SelectedItems.empty(), "deleted selection restored");
                list.SelectItem(i % 2 ? "folder\\tree" : "other\\tree");
                Require(list.m_SelectedItems.size() == 1, "valid selection failed after refresh");
            }
            list.ClearList();
            Require(list.m_SelectedItems.empty() && list.m_Items.empty(), "clear retained deleted items");
        }
        std::cout << "PASS: missing items/folders, saved selection becoming a folder, 2000 list refreshes\n";
        Memory._destroy();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
