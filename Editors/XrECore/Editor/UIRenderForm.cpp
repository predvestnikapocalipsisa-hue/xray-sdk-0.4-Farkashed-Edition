#include "stdafx.h"
#include "UIRenderForm.h"
#include "ui_main.h"
UIRenderForm::UIRenderForm()
{
	m_mouse_down = false;
	m_mouse_move = false;
	m_shiftstate_down = false;
}

UIRenderForm::~UIRenderForm()
{
}

void UIRenderForm::Draw()
{

	ImGui::Begin("Render");
	if (UI && UI->RT->pSurface)
	{
		int ShiftState = ssNone;

		if (ImGui::GetIO().KeyShift)
			ShiftState |= ssShift;
		if (ImGui::GetIO().KeyCtrl)
			ShiftState |= ssCtrl;
		if (ImGui::GetIO().KeyAlt)
			ShiftState |= ssAlt;

		if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
			ShiftState |= ssLeft;
		if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
			ShiftState |= ssRight;
		// VERIFY(!(ShiftState & ssLeft && ShiftState & ssRight));
		ImDrawList *draw_list = ImGui::GetWindowDrawList();
		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size = ImGui::GetContentRegionAvail();
		ImVec2 mouse_pos = ImGui::GetIO().MousePos;
		bool cursor_in_zone = true;
		if (mouse_pos.x < canvas_pos.x)
		{
			cursor_in_zone = false;
			mouse_pos.x = canvas_pos.x;
		}
		if (mouse_pos.y < canvas_pos.y)
		{
			cursor_in_zone = false;
			mouse_pos.y = canvas_pos.y;
		}

		if (mouse_pos.x > canvas_pos.x + canvas_size.x)
		{
			cursor_in_zone = false;
			mouse_pos.x = canvas_pos.x + canvas_size.x;
		}
		if (mouse_pos.y > canvas_pos.y + canvas_size.y)
		{
			cursor_in_zone = false;
			mouse_pos.y = canvas_pos.y + canvas_size.y;
		}

		bool curent_shiftstate_down = m_shiftstate_down;
		if (ImGui::IsWindowFocused())
		{

			if ((ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right)) && !m_mouse_down && cursor_in_zone)
			{
				UI->MousePress(TShiftState(ShiftState), mouse_pos.x - canvas_pos.x, mouse_pos.y - canvas_pos.y);
				m_mouse_down = true;
			}

			else if ((ImGui::IsMouseReleased(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Right)) && m_mouse_down)
			{
				if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
				{
					UI->MouseRelease(TShiftState(ShiftState), mouse_pos.x - canvas_pos.x, mouse_pos.y - canvas_pos.y);
					m_mouse_down = false;
					m_mouse_move = false;
					m_shiftstate_down = false;
				}
			}
			else if (m_mouse_down)
			{
				UI->MouseMove(TShiftState(ShiftState), mouse_pos.x - canvas_pos.x, mouse_pos.y - canvas_pos.y);
				m_mouse_move = true;
				m_shiftstate_down = m_shiftstate_down || (ShiftState & (ssShift | ssCtrl | ssAlt));
			}
		}
		else if (m_mouse_down)
		{
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
			{
				UI->MouseRelease(TShiftState(ShiftState), mouse_pos.x - canvas_pos.x, mouse_pos.y - canvas_pos.y);
				m_mouse_down = false;
				m_mouse_move = false;
				m_shiftstate_down = false;
			}
		}
		m_mouse_position.set(mouse_pos.x - canvas_pos.x, mouse_pos.y - canvas_pos.y);

		if (canvas_size.x < 32.0f)
			canvas_size.x = 32.0f;
		if (canvas_size.y < 32.0f)
			canvas_size.y = 32.0f;
		UI->RTSize.set(canvas_size.x, canvas_size.y);

		ImGui::InvisibleButton("canvas", canvas_size);
		if (!m_OnContextMenu.empty() && !curent_shiftstate_down)
		{
			if (ImGui::BeginPopupContextItem("Menu"))
			{
				m_OnContextMenu();
				ImGui::EndPopup();
			}
		}

		draw_list->AddImage(UI->RT->pSurface, canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y));

		static void* sdk_overlay_texture = nullptr;
		if (!sdk_overlay_texture)
		{
			u32 mem = 0;
			sdk_overlay_texture = RImplementation.texture_load("ui\\ui_sdk_overlay", mem);
		}

		if (sdk_overlay_texture)
		{
			const float overlay_aspect = 2560.f / 1020.f;
			const float margin = 18.f;

			const float max_width = _min(153.3f, canvas_size.x * 0.106f);
			const float max_height = canvas_size.y * 0.08f;

			const float overlay_width = _min(max_width, max_height * overlay_aspect);
			const ImVec2 overlay_size = { overlay_width, overlay_width / overlay_aspect };
			const ImVec2 overlay_pos =
			{
				canvas_pos.x + canvas_size.x - overlay_size.x - margin,
				canvas_pos.y + canvas_size.y - overlay_size.y - margin
			};

			draw_list->AddImage((ImTextureID)sdk_overlay_texture, overlay_pos,
				ImVec2(overlay_pos.x + overlay_size.x, overlay_pos.y + overlay_size.y),
				ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), IM_COL32(255, 255, 255, 185));
		}
	}
	ImGui::End();
}
