#include "stdafx.h"
#include "UIRenderForm.h"
#include "ui_main.h"
UIRenderForm::UIRenderForm()
{
	m_mouse_position.set(0, 0);
	m_mouse_down = false;
	m_mouse_move = false;
	m_shiftstate_down = false;
}

UIRenderForm::~UIRenderForm()
{
}

void UIRenderForm::Draw()
{

	const bool visible = ImGui::Begin("Render", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (visible && UI)
    {
        int quality = UI->GetViewportScale() > 1.75f ? 2 : UI->GetViewportScale() > 1.1f ? 1 : 0;
        const char* options[] = { "Off", "SSAA 1.5x", "SSAA 2x" };
        const float scales[] = { 1.f, 1.5f, 2.f };
        ImGui::SetNextItemWidth(130.f*XrUIManager::GetUIScale());
        if (ImGui::Combo("Anti-aliasing", &quality, options, 3)) UI->SetViewportScale(scales[quality]);
        if (Core.DebugMode)
        {
            if (ImGui::GetContentRegionAvail().x > ImGui::CalcTextSize("SDK DEBUG").x + ImGui::GetStyle().ItemSpacing.x)
                ImGui::SameLine();
            ImGui::TextDisabled("SDK DEBUG");
        }
    }
	if (!visible || !UI || !UI->RT->pSurface)
	{
		if (UI && m_mouse_down)
			UI->MouseRelease(ssNone, m_mouse_position.x, m_mouse_position.y);
		m_mouse_down = false;
		m_mouse_move = false;
		m_shiftstate_down = false;
		ImGui::End();
		return;
	}
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
		if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
			ShiftState |= ssMiddle;
		// VERIFY(!(ShiftState & ssLeft && ShiftState & ssRight));
		ImDrawList *draw_list = ImGui::GetWindowDrawList();
		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size = ImGui::GetContentRegionAvail();
		ImVec2 mouse_pos = ImGui::GetIO().MousePos;
		if (mouse_pos.x < canvas_pos.x)
		{
			mouse_pos.x = canvas_pos.x;
		}
		if (mouse_pos.y < canvas_pos.y)
		{
			mouse_pos.y = canvas_pos.y;
		}

		if (mouse_pos.x > canvas_pos.x + canvas_size.x)
		{
			mouse_pos.x = canvas_pos.x + canvas_size.x;
		}
		if (mouse_pos.y > canvas_pos.y + canvas_size.y)
		{
			mouse_pos.y = canvas_pos.y + canvas_size.y;
		}

		if (canvas_size.x < 32.0f)
			canvas_size.x = 32.0f;
		if (canvas_size.y < 32.0f)
			canvas_size.y = 32.0f;
		UI->RTSize.set(canvas_size.x, canvas_size.y);
		ImGui::InvisibleButton("canvas", canvas_size,
			ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		const bool cursor_in_zone = ImGui::IsItemHovered() && !ImGui::GetIO().AppFocusLost;

		bool curent_shiftstate_down = m_shiftstate_down;
		const bool buttons_down = (ShiftState & (ssLeft | ssRight | ssMiddle)) != 0;
		const bool mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
			ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
		const bool focused = ImGui::IsWindowFocused() && !ImGui::GetIO().AppFocusLost;

		// Finish captured drags even when the pointer leaves the canvas or focus is lost.
		if (m_mouse_down && (!buttons_down || !focused ||
			(EDevice.m_Camera.IsMoving() && EDevice.m_Camera.MoveEnd(TShiftState(ShiftState)))))
		{
			UI->MouseRelease(focused ? TShiftState(ShiftState) : ssNone,
				mouse_pos.x - canvas_pos.x, mouse_pos.y - canvas_pos.y);
			m_mouse_down = false;
			m_mouse_move = false;
			m_shiftstate_down = false;
		}
		else if (!m_mouse_down && mouse_clicked && cursor_in_zone)
		{
			ImGui::SetWindowFocus();
			UI->MousePress(TShiftState(ShiftState), mouse_pos.x - canvas_pos.x, mouse_pos.y - canvas_pos.y);
			m_mouse_down = true;
			m_shiftstate_down = (ShiftState & (ssShift | ssCtrl | ssAlt | ssMiddle)) != 0;
		}
		else if (m_mouse_down && focused)
		{
			UI->MouseMove(TShiftState(ShiftState), mouse_pos.x - canvas_pos.x, mouse_pos.y - canvas_pos.y);
			m_mouse_move = true;
			m_shiftstate_down = m_shiftstate_down || (ShiftState & (ssShift | ssCtrl | ssAlt | ssMiddle));
		}

		if (cursor_in_zone && (!m_mouse_down || EDevice.m_Camera.IsMoving()))
			EDevice.m_Camera.Zoom(ImGui::GetIO().MouseWheel);

		m_mouse_position.set(mouse_pos.x - canvas_pos.x, mouse_pos.y - canvas_pos.y);

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
        static bool overlayAttempted = false;
		if (!sdk_overlay_texture && !overlayAttempted)
		{
            overlayAttempted = true;
			u32 mem = 0;
			sdk_overlay_texture = RImplementation.texture_load("ui\\ui_sdk_overlay", mem);
		}

		if (sdk_overlay_texture)
		{
			const float overlay_aspect = 256.f / 256.f;
			const float margin = 12.f; // Чуть уменьшил отступ от края

			// Уменьшаем размер рамки на экране
			const float overlay_width = 40.f;
			const float overlay_height = overlay_width / overlay_aspect; // Вычислится автоматически

			const ImVec2 overlay_size = { overlay_width, overlay_height };
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
