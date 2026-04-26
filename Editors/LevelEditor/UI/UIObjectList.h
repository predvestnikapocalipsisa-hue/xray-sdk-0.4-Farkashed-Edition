#pragma once
#include <vector> 

class CCustomObject;

class UIObjectList : public XrUI
{
public:
    UIObjectList();
    virtual ~UIObjectList();
    virtual void Draw();
    static void Update();
    static void Show();
    static void Close();
    static IC bool IsOpen() { return Form; }

private:
    static UIObjectList* Form;

    CCustomObject* m_PrevSelected = nullptr;
    bool m_ScrollToSelected = false;

private:
    void DrawObjects();
    void DrawObject(CCustomObject* obj, const char* name);

private:
    ObjClassID m_cur_cls;
    enum EMode
    {
        M_All,
        M_Visible,
        M_Inbvisible
    };
    EMode m_Mode;
    CCustomObject* m_SelectedObject;
    CCustomObject* m_AnchorObject;               
    std::vector<CCustomObject*> m_VisibleRefs;   
    string_path m_Filter;
};