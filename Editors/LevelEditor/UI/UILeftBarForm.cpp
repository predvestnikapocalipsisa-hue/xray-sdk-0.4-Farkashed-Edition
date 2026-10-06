#include "stdafx.h"

UILeftBarForm::UILeftBarForm()
{
	m_UseSnapList = false;
	m_SnapListMode = false;
	m_SnapItem_Current = 0;
}

UILeftBarForm::~UILeftBarForm()
{
}

void UILeftBarForm::Draw()
{
	if (!ImGui::Begin("LeftBar", 0)) { ImGui::End(); return; }
    static ImGuiTextFilter toolFilter;
    toolFilter.Draw("##Find tools", ImGui::GetContentRegionAvail().x);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Filter object tools by name");
    ImGui::Spacing();
	ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
	if (ImGui::CollapsingHeader("Tools"))
	{
		static ObjClassID Tools[OBJCLASS_COUNT + 1] = {
			OBJCLASS_SCENEOBJECT,
			OBJCLASS_LIGHT,
			OBJCLASS_SOUND_SRC,
			OBJCLASS_SOUND_ENV, OBJCLASS_GLOW,
			OBJCLASS_SHAPE,
			OBJCLASS_SPAWNPOINT,
			OBJCLASS_WAY,
			OBJCLASS_SECTOR,
			OBJCLASS_PORTAL,
			OBJCLASS_GROUP,
			OBJCLASS_PS,
			OBJCLASS_DO,
			OBJCLASS_AIMAP,
			OBJCLASS_WM,
			OBJCLASS_FOG_VOL,
			OBJCLASS_force_dword};

        xr_vector<ObjClassID> visibleTools;
        for (u32 i = 0; Tools[i] != OBJCLASS_force_dword; ++i)
            if (toolFilter.PassFilter(Scene->GetTool(Tools[i])->ClassDesc()))
                visibleTools.push_back(Tools[i]);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2, 1));
        const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings |
            ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_BordersInnerV;
        if (ImGui::BeginTable("Tool grid", 2, flags))
        {
            const u32 rows = (visibleTools.size() + 1) / 2;
            for (u32 row = 0; row < rows; ++row)
            {
                ImGui::TableNextRow();
                for (u32 column = 0; column < 2; ++column)
                {
                    ImGui::TableSetColumnIndex(column);
                    const u32 index = row + column * rows;
                    if (index >= visibleTools.size()) continue;
                    const ObjClassID id = visibleTools[index];
                    ESceneToolBase* tool = Scene->GetTool(id);
                    ImGui::PushID(tool->ClassName());
                    bool visible = tool->IsVisible();
                    if (ImGui::Checkbox("##visible", &visible))
                    {
                        tool->m_EditFlags.set(ESceneToolBase::flVisible, visible);
                        UI->RedrawScene();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show/hide %s", tool->ClassDesc());
                    ImGui::SameLine();
                    float width = ImGui::GetContentRegionAvail().x;
                    if (width < 1.f) width = 1.f;
                    if (ImGui::Selectable(tool->ClassDesc(), LTools->GetTarget() == id, 0,
                        ImVec2(width, ImGui::GetFrameHeight())))
                        ExecCommand(COMMAND_CHANGE_TARGET, id);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tool->ClassDesc());
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar(3);

	}
	ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
	if (ImGui::CollapsingHeader("Snap List"))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 4));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 0));
		ImGui::Separator();
		{
			ImGui::BulletText("Commands", ImGuiDir_Left);
			if (ImGui::BeginPopupContextItem("Commands", 1))
			{
				if (ImGui::MenuItem("Make List From Selected"))
				{
					ExecCommand(COMMAND_SET_SNAP_OBJECTS);
				}
				if (ImGui::MenuItem("Select Object From List"))
				{
					ExecCommand(COMMAND_SELECT_SNAP_OBJECTS);
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Add Selected To List"))
				{
					ExecCommand(COMMAND_ADD_SEL_SNAP_OBJECTS);
				}
				if (ImGui::MenuItem("Remove Selected From List"))
				{
					ExecCommand(COMMAND_DEL_SEL_SNAP_OBJECTS);
				}
				ImGui::EndPopup();
			}
			ImGui::OpenPopupOnItemClick("Commands", 0);
		}
		//	ImGui::Checkbox("Enable/Show Snap List", &test);
		ImGui::Checkbox("Enable/Show Snap List", &m_UseSnapList);

		ImGui::Separator();
		ImGui::Checkbox("+/- Mode", &m_SnapListMode);
		ImGui::SameLine(0, 10);
		if (ImGui::Button("X"))
		{
			if (ELog.DlgMsg(mtConfirmation, mbYes | mbNo, "Are you sure to clear snap objects?") == mrYes)
				ExecCommand(COMMAND_CLEAR_SNAP_OBJECTS);
		}
		ImGui::PopStyleVar(2);
		/*

	if (lst&&!lst->empty()){
		int idx=0;
		ObjectIt _F=lst->begin();
		for (;_F!=lst->end(); _F++,idx++){
			AnsiString s; s.sprintf("%d: %s",idx,(*_F)->Name);
			lbSnapList->Items->Add(s);
		}
	}*/
		ObjectList *lst = Scene->GetSnapList(true);

		ImGui::SetNextItemWidth(-1);
		ImGui::ListBox(
			"##snap_list_box", &m_SnapItem_Current, [](void *data, int ind, const char **out) -> bool
			{auto item = reinterpret_cast<ObjectList*>(data)->begin(); std::advance(item, ind); *out = (*item)->GetName(); return true; },
			reinterpret_cast<void *>(lst), lst->size(), 7);
		ImGui::PopStyleVar(2);

	}
	if (LTools->GetToolForm())
		LTools->GetToolForm()->Draw();
	ImGui::End();
}
