#include "stdafx.h"
#include "UIDOTool.h"
#include "Tools/Details/ESceneDOTools.h"

UIDOTool::UIDOTool()
{
	m_DOShuffle = false;
	strcpy(m_NewMapName, "detail\\detail_build");
	m_NewMapResolution = 2; // 1024x1024
	m_NewMapColor[0] = 0.f; m_NewMapColor[1] = 0.f; m_NewMapColor[2] = 0.f; m_NewMapColor[3] = 1.f;
	m_AutoSlopeDeg = 35.0f;
	m_AutoGrassColor[0] = 1.f; m_AutoGrassColor[1] = 0.f; m_AutoGrassColor[2] = 0.f; m_AutoGrassColor[3] = 1.f;

	m_BrushRadius = 5.0f;
	m_BrushColor[0] = 1.f; m_BrushColor[1] = 0.f; m_BrushColor[2] = 0.f; m_BrushColor[3] = 1.f;
	m_EraserMode = false;
}

UIDOTool::~UIDOTool()
{
}

u32 UIDOTool::GetBrushColor() const
{
	if (m_EraserMode)
		return 0;
	return color_rgba_f(m_BrushColor[0], m_BrushColor[1], m_BrushColor[2], m_BrushColor[3]);
}

void UIDOTool::Draw()
{
	ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
	if (ImGui::TreeNode("Commands"))
	{
		ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
		{
			if (ImGui::Button("First Initialize", ImVec2(-1, 0)))
			{
				if (DM->Initialize())
					Scene->UndoSave();
			}
			if (ImGui::Button("Reinitialize All", ImVec2(-1, 0)))
			{
				if (DM->Reinitialize())
					Scene->UndoSave();
			}
			if (ImGui::Button("Reinitialize Objects Only", ImVec2(-1, 0)))
			{
				if (DM->UpdateObjects(true, false))
					Scene->UndoSave();
			}
			if (ImGui::Button("Reinitialize Selected Slot Objects", ImVec2(-1, 0)))
			{
				if (DM->UpdateObjects(false, true))
					Scene->UndoSave();
			}
			ImGui::Separator();
			if (ImGui::Button("Update Renderer", ImVec2(-1, 0)))
			{
				DM->InvalidateCache();
				Scene->UndoSave();
			}
			ImGui::Separator();
			if (ImGui::Button("Clear Slots", ImVec2(-1, 0)))
			{
				if (ELog.DlgMsg(mtConfirmation, mbYes | mbNo, "Are you sure to reset slots?") == mrYes)
				{
					DM->ClearSlots();
					Scene->UndoSave();
				}
			}
			if (ImGui::Button("Clear Details", ImVec2(-1, 0)))
			{
				if (ELog.DlgMsg(mtConfirmation, mbYes | mbNo, "Are you sure to clear details?") == mrYes)
				{
					ExecCommand(COMMAND_UPDATE_PROPERTIES);
					DM->Clear();
					Scene->UndoSave();
				}
			}
			ImGui::Separator();
			if (ImGui::Button("Object List", ImVec2(-1, 0)))
			{
				m_DOShuffle = true;
				UIDOShuffle::Show(DM);
			}
		}

		ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
		ImGui::TreePop();
	}

	ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
	if (ImGui::TreeNode("Brush Painting (3D Viewport)"))
	{
		ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
		{
			bool is_painting = (LTools->GetAction() == etaAdd);
			if (is_painting)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
				if (ImGui::Button("Painting Mode: ACTIVE [Click to Stop]", ImVec2(-1, 0)))
				{
					LTools->SetAction(etaSelect);
				}
				ImGui::PopStyleColor();
			}
			else
			{
				if (ImGui::Button("Start Painting Brush [Viewport]", ImVec2(-1, 0)))
				{
					LTools->SetAction(etaAdd);
				}
			}

			ImGui::SliderFloat("Brush Radius (m)", &m_BrushRadius, 0.5f, 50.0f, "%.1f m");
			ImGui::ColorEdit4("Brush Color", m_BrushColor);
			ImGui::Checkbox("Eraser Mode (or Shift+Drag)", &m_EraserMode);

			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Tip: Drag LMB in viewport to paint.");
			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Hold Shift + Drag to erase.");
		}
		ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
		ImGui::TreePop();
	}

	ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
	if (ImGui::TreeNode("Texture Transform"))
	{
		ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
		{
			bool changed = false;
			if (ImGui::DragFloat("Offset X (m)", &DM->m_Base.m_Offset.x, 0.5f, -10000.f, 10000.f, "%.2f")) changed = true;
			if (ImGui::DragFloat("Offset Z (m)", &DM->m_Base.m_Offset.y, 0.5f, -10000.f, 10000.f, "%.2f")) changed = true;
			if (ImGui::DragFloat("Angle (deg)", &DM->m_Base.m_Angle, 1.0f, -360.f, 360.f, "%.1f")) changed = true;

			if (changed)
			{
				if (DM->m_Base.Valid())
				{
					DM->m_Base.CreateRMFromObjects(DM->GetBBox(), *DM->GetSnapList());
					UI->RedrawScene();
				}
			}

			if (ImGui::Button("Reset Transform", ImVec2(-1, 0)))
			{
				DM->m_Base.m_Offset.set(0.f, 0.f);
				DM->m_Base.m_Angle = 0.f;
				if (DM->m_Base.Valid())
				{
					DM->m_Base.CreateRMFromObjects(DM->GetBBox(), *DM->GetSnapList());
					UI->RedrawScene();
				}
			}
		}
		ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
		ImGui::TreePop();
	}

	ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
	if (ImGui::TreeNode("Vegetation Map Creation"))
	{
		ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
		{
			ImGui::InputText("Map Name", m_NewMapName, sizeof(m_NewMapName));
			const char* resolutions[] = { "256x256", "512x512", "1024x1024", "2048x2048" };
			ImGui::Combo("Resolution", &m_NewMapResolution, resolutions, 4);

			u32 res_val = 1024;
			if (m_NewMapResolution == 0) res_val = 256;
			else if (m_NewMapResolution == 1) res_val = 512;
			else if (m_NewMapResolution == 2) res_val = 1024;
			else if (m_NewMapResolution == 3) res_val = 2048;

			ImGui::ColorEdit4("Fill Color", m_NewMapColor);

			if (ImGui::Button("Create Blank Map", ImVec2(-1, 0)))
			{
				u32 clr = color_rgba_f(m_NewMapColor[0], m_NewMapColor[1], m_NewMapColor[2], m_NewMapColor[3]);
				if (DM->CreateNewBaseTexture(m_NewMapName, res_val, res_val, clr))
				{
					Scene->UndoSave();
					LTools->SetAction(etaAdd);
				}
			}

			ImGui::Separator();
			ImGui::Text("Auto-Generate from Terrain:");
			ImGui::SliderFloat("Max Slope (deg)", &m_AutoSlopeDeg, 0.0f, 60.0f, "%.1f deg");
			ImGui::ColorEdit4("Grass Color", m_AutoGrassColor);

			if (ImGui::Button("Auto-Generate Map", ImVec2(-1, 0)))
			{
				u32 g_clr = color_rgba_f(m_AutoGrassColor[0], m_AutoGrassColor[1], m_AutoGrassColor[2], m_AutoGrassColor[3]);
				if (DM->AutoGenerateBaseTexture(m_NewMapName, res_val, res_val, m_AutoSlopeDeg, g_clr))
					Scene->UndoSave();
			}
		}
		ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
		ImGui::TreePop();
	}
}

void UIDOTool::OnDrawUI()
{
	if (m_DOShuffle)
	{
		if (UIDOShuffle::GetResult())
		{
			m_DOShuffle = false;
		}
		UIDOShuffle::Update();
	}
}
