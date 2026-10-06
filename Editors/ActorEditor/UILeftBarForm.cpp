#include "stdafx.h"

UILeftBarForm::UILeftBarForm()
{
	m_RenderMode = Render_Editor;
	m_PickMode = 0;
}

UILeftBarForm::~UILeftBarForm()
{
}

void UILeftBarForm::Draw()
{
	ImGui::Begin("LeftBar", 0);

	ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	if (ImGui::CollapsingHeader("Model"))
	{
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Render Style:");
		if (ImGui::RadioButton("Editor", m_RenderMode == Render_Editor))
		{
			ATools->PhysicsStopSimulate();
			m_RenderMode = Render_Editor;
			ExecCommand(COMMAND_UPDATE_PROPERTIES);
			UI->RedrawScene();
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Engine", m_RenderMode == Render_Engine))
		{
			ATools->PhysicsStopSimulate();
			m_RenderMode = Render_Engine;
			if (!ATools->IsVisualPresent())
				ExecCommand(COMMAND_MAKE_PREVIEW);
			if (!ATools->IsVisualPresent())
				SetRenderMode(false);
			else
				SetRenderMode(true);
			ExecCommand(COMMAND_UPDATE_PROPERTIES);
			UI->RedrawScene();
		}
		ImGui::Spacing();
		if (ImGui::Button("Clip Maker"))
		{
			UIBoneForm::Show();
		}
		static const char *PickModeList[] = {"None", "Surface", "Bone"};
		ImGui::TextUnformatted("Pick mode");
		ImGui::SetNextItemWidth(-1);
		ImGui::Combo("##Pick mode", &m_PickMode, PickModeList, 3, -1);
	}
	ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	if (ImGui::CollapsingHeader("Object Items"))
	{
		ImGui::BeginGroup();
		ATools->m_ObjectItems->Draw();
		ImGui::EndGroup();
	}
	ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	if (ImGui::CollapsingHeader("Item Properties"))
	{
		ImGui::BeginGroup();
		ATools->m_Props->Draw();
		ImGui::EndGroup();
	}
	ImGui::End();
}

void UILeftBarForm::SetRenderMode(bool bEngineMode)
{
	if (ATools->IsVisualPresent() && bEngineMode)
		m_RenderMode = Render_Engine;
	else
		m_RenderMode = Render_Editor;
	ATools->PlayMotion();
}
