#include "stdafx.h"

UIPropertiesItem::UIPropertiesItem(shared_str Name, UIPropertiesForm* propertiesFrom) : UITreeItem(Name), PropertiesFrom(propertiesFrom)
{
	PItem = nullptr;
}

void UIPropertiesItem::Draw()
{
	if (PItem && PItem->Type() == PROP_BUTTON && Owner)
	{
		const xr_string name = Name.c_str();
		const xr_string suffix = " Copy/Paste";
		if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
		{
			const xr_string pairedName = name.substr(0, name.size() - suffix.size());
			for (UITreeItem* sibling : Owner->Items)
			{
				UIPropertiesItem* propertySibling = static_cast<UIPropertiesItem*>(sibling);
				if (propertySibling != this && propertySibling->PItem &&
					xr_strcmp(*propertySibling->Name, pairedName.c_str()) == 0 &&
					propertySibling->PItem->Type() == PROP_VECTOR)
					return;
			}
		}
	}

	UIPropertiesItem* transformButtons = nullptr;
	const bool isTransformValue = PItem &&
		(xr_strcmp(*Name, "Position") == 0 || xr_strcmp(*Name, "Rotation") == 0 || xr_strcmp(*Name, "Scale") == 0);
	if (isTransformValue && Owner)
	{
		for (UITreeItem* sibling : Owner->Items)
		{
			UIPropertiesItem* propertySibling = static_cast<UIPropertiesItem*>(sibling);
			string_path buttonName;
			xr_sprintf(buttonName, "%s Copy/Paste", Name.c_str());
			if (propertySibling->PItem && propertySibling->PItem->Type() == PROP_BUTTON &&
				xr_strcmp(*propertySibling->Name, buttonName) == 0)
			{
				transformButtons = propertySibling;
				break;
			}
		}
	}

	ImGui::TableNextRow();
	ImGui::TableNextColumn();

	if (PItem && PItem->m_Flags.test(PropItem::flShowCB))
	{
		if (ImGui::CheckboxFlags("##value", &PItem->m_Flags.flags, PropItem::flCBChecked))
		{
			PItem->OnChange();
			PropertiesFrom->Modified();
		}

		ImGui::SameLine(0, 2);
	}

	if (Items.size())
	{
		ImGuiTreeNodeFlags FloderFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
		bool open = ImGui::TreeNodeEx(DX2U(Name.c_str()), FloderFlags);
		ImGui::TableNextColumn();
		if (transformButtons)
		{
			const float buttonWidth = 44.f;
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			DrawItem(ImGui::GetContentRegionAvail().x - buttonWidth - spacing);
			ImGui::SameLine(0, spacing);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.f, 1.f));
			ImGui::PushID(transformButtons->Name.c_str());
			transformButtons->DrawItem(buttonWidth);
			ImGui::PopID();
			ImGui::PopStyleVar();
		}
		else
			DrawItem();

		if (open)
		{
			for (UITreeItem* Item : Items)
				static_cast<UIPropertiesItem*>(Item)->Draw();
			
			ImGui::TreePop();
		}
	}
	else
	{
		ImGui::TreeNodeEx(DX2U(Name.c_str()), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
		const ImVec2 row = ImGui::GetItemRectMin();
		const float fontSize = ImGui::GetFontSize();
		ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(row.x + fontSize * .5f, row.y + fontSize * .5f),
			fontSize * .085f, ImGui::GetColorU32(ImGuiCol_TextDisabled), 12);
		ImGui::TableNextColumn();
		if (transformButtons)
		{
			const float buttonWidth = 44.f;
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			DrawItem(ImGui::GetContentRegionAvail().x - buttonWidth - spacing);
			ImGui::SameLine(0, spacing);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.f, 1.f));
			ImGui::PushID(transformButtons->Name.c_str());
			transformButtons->DrawItem(buttonWidth);
			ImGui::PopID();
			ImGui::PopStyleVar();
		}
		else
			DrawItem();
	}
}

void UIPropertiesItem::DrawRoot()
{
	VERIFY(!PItem);

	for (UITreeItem* Item : Items)	
		static_cast<UIPropertiesItem*>(Item)->Draw();	
}

void UIPropertiesItem::DrawItem(float width)
{
	if (!PItem)
		return;

	EPropType type = PItem->Type();

	switch (type)
	{
	case PROP_CANVAS:
	{
		if (PItem->m_Flags.test(PropItem::flMixed))
		{
			ImGui::TextDisabled(DX2U(PItem->GetDrawText().c_str()));

		}
		else
		{
			ImGui::PushItemWidth(-1);
			CanvasValue* val = dynamic_cast<CanvasValue*>(PItem->GetFrontValue()); R_ASSERT(val);
			if (!val->OnDrawCanvasEvent.empty())
				val->OnDrawCanvasEvent(val);
			ImGui::PopItemWidth();
		}
	}
	break;

	case PROP_BUTTON:
		if (PItem->m_Flags.test(PropItem::flMixed))		
			ImGui::TextDisabled(DX2U(PItem->GetDrawText().c_str()));		
		else
		{
			ImGui::PushID(Name.c_str());
			bool bRes = false;
			bool bSafe = false;
			ButtonValue* V = dynamic_cast<ButtonValue*>(PItem->GetFrontValue()); 
			R_ASSERT(V);

			if (!V->value.empty())
			{
				const float available = width > 0.f ? width : ImGui::GetContentRegionAvail().x;
				float minimum = ImGui::GetFrameHeight();
				for (const shared_str& label : V->value)
				{
					const float width = ImGui::CalcTextSize(DX2U(label.c_str())).x + ImGui::GetStyle().FramePadding.x * 2.f;
					if (width > minimum) minimum = width;
				}
				int columns = int((available + 2.f) / (minimum + 2.f));
				if (columns < 1) columns = 1;
				if (columns > int(V->value.size())) columns = int(V->value.size());
				float dx = (available - 2.f * (columns - 1)) / columns;
				if (dx < 1.f) dx = 1.f;
				V->btn_num = V->value.size();

				for (RStringVecIt it = V->value.begin(); it != V->value.end(); it++)
				{
					int k = it - V->value.begin();

					if (ImGui::Button(DX2U(it->c_str()), ImVec2(dx, 0)))
					{
						V->btn_num = k;
						bRes |= V->OnBtnClick(bSafe);
					}

					if ((k + 1) % columns != 0 && k + 1 < int(V->value.size()))
						ImGui::SameLine(0, 2);
				}
			}
			else			
				ImGui::Text("");
			
			ImGui::PopID();
            if (bRes) PropertiesFrom->Modified();
		}
		break;

	case PROP_WAVE:
	case PROP_UNDEF:
		break;

	case PROP_CAPTION:	
		ImGui::TextDisabled(DX2U(PItem->GetDrawText().c_str()));
		break;

	default:
		ImGui::PushID(Name.c_str());

		if (PropertiesFrom->IsReadOnly())
		{
			if (type == PROP_BOOLEAN)
			{
				FlagValueCustom* V = dynamic_cast<FlagValueCustom*>(PItem->GetFrontValue()); VERIFY(V);
				ImGui::TextDisabled(V->GetValueEx() ? "true" : "false");
			}
			else			
				ImGui::TextDisabled(DX2U(PItem->GetDrawText().c_str()));			
		}
		else if (PItem->m_Flags.test(PropItem::flMixed) && !PItem->m_Flags.test(PropItem::flIgnoreMixed))
		{
			if (ImGui::Button("(Mixed)", ImVec2(-1, 0)))			
				RemoveMixed();			
		}
		else
		{
			if (PItem->m_Flags.test(PropItem::flDisabled))
			{
				if (type == PROP_FLAG)
				{
					FlagValueCustom* V = dynamic_cast<FlagValueCustom*>(PItem->GetFrontValue()); VERIFY(V);
					ImGui::TextDisabled(V->GetValueEx() ? "true" : "false");
				}
				else				
					ImGui::TextDisabled(DX2U(PItem->GetDrawText().c_str()));				
			}
			else
			{
				ImGui::PushItemWidth(width);
				DrawProp();
				ImGui::PopItemWidth();
			}
		}
		ImGui::PopID();
		break;
	}
}

UITreeItem* UIPropertiesItem::CreateItem(shared_str Name)
{
	return xr_new<UIPropertiesItem>(Name, PropertiesFrom);
}
