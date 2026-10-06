#include "stdafx.h"
#include "ELog.h"
#include "UILogForm.h"
#include "../XrEUI/SDKMessageBox.h"
#include "..\XrCore\os_clipboard.h"
#define MSG_ERROR 0x00C4C4FF
#define MSG_INFO 0x00E6FFE7
#define MSG_CONF 0x00FFE6E7
#define MSG_DEF 0x00E8E8E8
bool UILogForm::bAutoScroll = true;
bool UILogForm::bOnlyError = false;
xr_vector<xr_string> *UILogForm::List = nullptr;
int UILogForm::selectedLine = -1;
extern bool bAllowLogCommands;
void UILogForm::AddMessage(TMsgDlgType mt, const xr_string &msg)
{
	xr_string M;
	for (int i = 0; i < msg.size(); i++)
	{
		if (msg[i] == '\r')
			continue;
		if (msg[i] == '\n')
			M += " ";
		else
			M += msg[i];
	}
	xr_string M_utf8 = XrUIManager::ConvertCP1251ToUTF8(M.c_str());
	switch (mt)
	{
	case mtError:
		M_utf8.insert(0, "###");
		break;
	case mtConfirmation:
		M_utf8.insert(0, "##@");
		break;
	}
	GetList()->push_back(M_utf8);
}

void UILogForm::AddDlgMessage(TMsgDlgType mt, const xr_string &msg)
{
	AddMessage(mt, msg);
    SDKDialogs::Notify(msg.c_str(), mt == mtError ? "Error" : mt == mtConfirmation ? "Warning" : "Information",
        MB_OK | (mt == mtError ? MB_ICONERROR : mt == mtConfirmation ? MB_ICONWARNING : MB_ICONINFORMATION));
}

void UILogForm::Show()
{
	bAllowLogCommands = true;
}

void UILogForm::Hide()
{
	bAllowLogCommands = false;
}

void UILogForm::Update()
{
	if (bAllowLogCommands)
	{
		bool NeedCopy = false;
		if (!ImGui::Begin("Log", &bAllowLogCommands))
		{
			ImGui::End();
			return;
		}
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6,3));
		if (ImGui::Button("Clear"))
		{
			GetList()->clear();
			selectedLine = -1;
		}
		ImGui::SameLine();
		if (ImGui::Button("Flush"))
		{
			FlushLog();
		}
		ImGui::SameLine();
		if (ImGui::Button("Copy"))
		{
			NeedCopy = true;
		}
		ImGui::SameLine();
        if (ImGui::Button("Filters")) ImGui::OpenPopup("Log filters");
        if (ImGui::BeginPopup("Log filters"))
        {
            ImGui::Checkbox("Auto scroll", &bAutoScroll);
            ImGui::Checkbox("Errors only", &bOnlyError);
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
        static ImGuiTextFilter filter;
        filter.Draw("##Search log", ImGui::GetContentRegionAvail().x);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Filter messages; use -word to exclude");

		ImGui::Spacing();
		if (ImGui::BeginChild("Log", ImVec2(0, 0), true))
		{
            const bool followTail = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.f;
            xr_string CopyLog;
            int visibleIndex = 0;
			for (int i = 0; i < (int)GetList()->size(); i++)
			{
				ImVec4 Color = ImGui::GetStyle().Colors[ImGuiCol_Text];
				const char *Str = GetList()->at(i).c_str();
				bool isError = false;
				if (strncmp(Str, "###", 3) == 0)
				{
					isError = true;
					Color = {1, 0.3f, 0.3f, 1};
					Str += 3;
				}
				else if (strncmp(Str, "##@", 3) == 0)
				{
					Color = {1, 1, 0, 1};
					Str += 3;
				}

				if ((bOnlyError && !isError) || !filter.PassFilter(Str))
					continue;

				bool isSelected = (selectedLine == i);

				// Highlight selected line background
				if (isSelected)
					ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);

				ImGui::PushStyleColor(ImGuiCol_Text, Color);
				char selId[32];
				snprintf(selId, sizeof(selId), "##logline_%d", i);
				if (ImGui::Selectable(selId, isSelected, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, 0)))
				{
					selectedLine = (isSelected ? -1 : i);
				}
				ImGui::PopStyleColor(); // Text
				if (isSelected)
					ImGui::PopStyleColor(); // Header

                // Draw inside the row instead of SameLine after its full width.
                const ImVec2 row = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(row, ImGui::GetColorU32(Color), Str);

				// Right-click context menu
				if (ImGui::BeginPopupContextItem(selId))
				{
					if (ImGui::MenuItem("Copy line"))
					{
						os_clipboard::copy_to_clipboard(Str);
					}
					if (ImGui::MenuItem("Copy filtered messages"))
					{
						NeedCopy = true;
					}
					ImGui::EndPopup();
				}

				visibleIndex++;
			}
			if (NeedCopy)
			{
                // Assemble once after the loop, including a context-menu request.
				if (NeedCopy)
				{
					for (int i = 0; i < (int)GetList()->size(); i++)
					{
						const char *Str = GetList()->at(i).c_str();
						if (strncmp(Str, "###", 3) == 0) Str += 3;
						else if (strncmp(Str, "##@", 3) == 0) Str += 3;
						if ((!bOnlyError || strncmp(GetList()->at(i).c_str(), "###", 3) == 0) && filter.PassFilter(Str))
							CopyLog.append(Str).append("\r\n");
					}
				}
				os_clipboard::copy_to_clipboard(CopyLog.c_str());
			}
			if (bAutoScroll && followTail)
				ImGui::SetScrollHereY(1.f);
		}
		ImGui::EndChild();
		ImGui::End();
	}
}

void UILogForm::Destroy()
{
	xr_delete(List);
}

xr_vector<xr_string> *UILogForm::GetList()
{
	if (!List)
		List = xr_new<xr_vector<xr_string>>();
	return List;
}
