#include "stdafx.h"

UIObjectList *UIObjectList::Form = nullptr;

UIObjectList::UIObjectList()
{
    m_Mode = M_Visible;
    m_Filter[0] = 0;
    m_SelectedObject = nullptr;
    m_AnchorObject = nullptr; 
}

UIObjectList::~UIObjectList()
{
}

void UIObjectList::Draw()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(400, 400));

    if (!ImGui::Begin("Object List", &bOpen))
    {
        ImGui::PopStyleVar(1);
        ImGui::End();
        return;
    }

 
    m_VisibleRefs.clear();

    {
        ImGui::BeginGroup();
        if (ImGui::BeginChild("Left", ImVec2(-130, -ImGui::GetFrameHeight() - 4), true))
        {
            DrawObjects();
        }
        ImGui::EndChild();

        ImGui::SetNextItemWidth(-130);
        ImGui::InputText("##value", m_Filter, sizeof(m_Filter));
        ImGui::EndGroup();
    }
    ImGui::SameLine();
    if (ImGui::BeginChild("Right", ImVec2(130, 0)))
    {
        if (ImGui::RadioButton("All", m_Mode == M_All)) m_Mode = M_All;
        if (ImGui::RadioButton("Visible Only", m_Mode == M_Visible)) m_Mode = M_Visible;
        if (ImGui::RadioButton("Invisible Only", m_Mode == M_Inbvisible)) m_Mode = M_Inbvisible;
        
        ImGui::Separator();
        if (ImGui::Button("Show Selected", ImVec2(-1, 0)))
        {
            if (m_SelectedObject)
            {
                m_SelectedObject->Show(true);
                m_SelectedObject->Select(true);
            }
        }
        if (ImGui::Button("Hide Selected", ImVec2(-1, 0)))
        {
            if (m_SelectedObject) m_SelectedObject->Show(false);
        }
    }
    ImGui::EndChild();

    ImGui::PopStyleVar(1);
    ImGui::End();
}

void UIObjectList::Update()
{
    if (!Form || !Scene) return;

    CCustomObject* current = nullptr;

    for (SceneToolsMapPairIt it = Scene->FirstTool(); it != Scene->LastTool(); ++it)
    {
        ESceneCustomOTool* ot = dynamic_cast<ESceneCustomOTool*>(it->second);

        if (ot) {
            ObjectList& lst = ot->GetObjects();
            for (CCustomObject* obj : lst) {
                if (obj->Selected()) {
                    current = obj;
                    break;
                }
            }
        }
        if (current) break;
    }

    if (current && Form->m_PrevSelected != current)
    {
        Form->m_SelectedObject = current;
        Form->m_PrevSelected = current;
        Form->m_ScrollToSelected = true;
    }

        if (!Form->IsClosed()) Form->Draw();
        else xr_delete(Form);
    }

void UIObjectList::Show()
{
    if (Form == nullptr) Form = xr_new<UIObjectList>();
}

void UIObjectList::Close()
{
    xr_delete(Form);
}

void UIObjectList::DrawObjects()
{
    m_cur_cls = LTools->CurrentClassID();
    
    for (SceneToolsMapPairIt it = Scene->FirstTool(); it != Scene->LastTool(); ++it)
    {
        ESceneCustomOTool *ot = dynamic_cast<ESceneCustomOTool *>(it->second);
        if (ot && ((m_cur_cls == OBJCLASS_DUMMY) || (it->first == m_cur_cls)))
        {
            if (it->first == OBJCLASS_DUMMY) continue;

            ObjectList &lst = ot->GetObjects();
            ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
            
            if (ImGui::TreeNode(it->second->ClassDesc()))
            {
                for (ObjectIt _F = lst.begin(); _F != lst.end(); ++_F)
                {
                    CCustomObject* obj = *_F;

                    if (m_Mode == M_Visible && !obj->Visible()) continue;
                    if (m_Mode == M_Inbvisible && obj->Visible()) continue;

                    if (OBJCLASS_GROUP == it->first)
                    {
                        CGroupObject* grp = (CGroupObject*)obj;
                        DrawObject(obj, grp->GetName());

                        ImGui::PushID(grp->GetName());
                        if (ImGui::TreeNode(grp->GetName()))
                        {
                            ObjectList grp_lst;
                            grp->GetObjects(grp_lst);
                            for (ObjectIt _G = grp_lst.begin(); _G != grp_lst.end(); _G++)
                                DrawObject(*_G, nullptr);
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    else
                    {
                        DrawObject(obj, nullptr);
                    }
                }
                ImGui::TreePop();
            }
        }
    }
}

void UIObjectList::DrawObject(CCustomObject *obj, const char *name)
{
    const char* display_name = name ? name : obj->GetName();

    if (m_Filter[0] && !strstr(display_name, m_Filter))
        return;

    m_VisibleRefs.push_back(obj);

    ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (obj->Selected()) Flags |= ImGuiTreeNodeFlags_Bullet;
    if (m_SelectedObject == obj) Flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::TreeNodeEx(display_name, Flags);

    if (m_ScrollToSelected && m_SelectedObject == obj)
    {
        ImGui::SetScrollHereY(0.5f); 
        m_ScrollToSelected = false;  
    }

    if (ImGui::IsItemClicked())
    {
        ImGuiIO& io = ImGui::GetIO();

        if (io.KeyShift && m_AnchorObject != nullptr)
        {

            int start_idx = -1;
            int end_idx = -1;

            for (int i = 0; i < (int)m_VisibleRefs.size(); ++i)
            {
                if (m_VisibleRefs[i] == m_AnchorObject) start_idx = i;
                if (m_VisibleRefs[i] == obj) end_idx = i;
            }

            if (start_idx != -1 && end_idx != -1)
            {
                int from = _min(start_idx, end_idx);
                int to = _max(start_idx, end_idx);

                if (!io.KeyCtrl) Scene->SelectObjects(false, OBJCLASS_DUMMY);

                for (int i = from; i <= to; ++i)
                {
                    m_VisibleRefs[i]->Select(true);
                }
            }
        }
        else if (io.KeyCtrl)
        {
            obj->Select(!obj->Selected());
            m_AnchorObject = obj;
        }
        else
        {
            Scene->SelectObjects(false, OBJCLASS_DUMMY);
            obj->Select(true);
            m_AnchorObject = obj;
        }

        m_PrevSelected = obj;

        m_SelectedObject = obj;
    }
}