#include "stdafx.h"
#include <unordered_map>
#include <string>

// ------------------------------------------------------------------
// Словарь подсказок для кнопок топбара.
// Ключ — имя кнопки (то самое "Name" из UITopBarForm_ButtonList.h),
// значение — текст, который увидит пользователь при наведении.
// Если для кнопки нет записи — подсказка просто не покажется.
// ------------------------------------------------------------------
static const char* GetTopBarTooltip(const char* name)
{
	static const std::unordered_map<std::string, const char*> tooltips = {
		{"Undo",       "Undo last action"},
		{"Redo",       "Redo last undone action"},
		{"Zoom",       "Zoom extents (show whole scene)"},
		{"ZoomSel",    "Zoom to selected object"},

		{"Select",     "Selection mode"},
		{"Add",        "Add objects mode"},
		{"Move",       "Move mode"},
		{"Rotate",     "Rotate mode"},
		{"Scale",      "Scale mode"},

		{"X",          "Constrain transform to X axis"},
		{"Y",          "Constrain transform to Y axis"},
		{"Z",          "Constrain transform to Z axis"},
		{"ZX",         "Constrain transform to ZX plane"},

		{"CsLocal",    "Local coordinate system"},
		{"NuScale",    "Non-uniform scale"},
		{"GSnap",      "Snap to grid"},
		{"OSnap",      "Snap to object"},
		{"MoveToSnap", "Move object to snap point"},
		{"NSnap",      "Align to normal"},
		{"VSnap",      "Snap to vertex"},
		{"ASnap",      "Angle snap"},
		{"MSnap",      "Magnet snap"},

		{"CameraP",    "Camera: plane move"},
		{"CameraA",    "Camera: arcball (orbit)"},
		{"CameraF",    "Camera: free fly"},

		{"ViewB1",     "Back view"},
		{"ViewB2",     "Bottom view"},
		{"ViewF",      "Front view"},
		{"ViewL",      "Left view"},
		{"ViewR",      "Right view"},
		{"ViewT",      "Top view"},
		{"ViewX",      "Reset view"},

		{"RunInGame",  "Play in editor"},
	};
	auto it = tooltips.find(name);
	return it != tooltips.end() ? it->second : nullptr;
}

// Небольшой хелпер, чтобы не дублировать одну и ту же конструкцию
// if (ImGui::IsItemHovered()) if (tip) SetTooltip(...) в каждом макросе.
static void ShowTopBarTooltipIfHovered(const char* name)
{
	if (ImGui::IsItemHovered())
	{
		if (const char* tip = GetTopBarTooltip(name))
			ImGui::SetTooltip("%s", tip);
	}
}

UITopBarForm::UITopBarForm()
{

#define ADD_BUTTON_IMAGE_T1(Class, Name)
#define ADD_BUTTON_IMAGE_T2(Class, Name)
#define ADD_BUTTON_IMAGE_S(Name)                                      \
	m_t##Name = EDevice.Resources->_CreateTexture("ed\\bar\\" #Name); \
	m_time##Name = 0;
#define ADD_BUTTON_IMAGE_D(Name)                                      \
	m_t##Name = EDevice.Resources->_CreateTexture("ed\\bar\\" #Name); \
	m_b##Name = false;
#include "UITopBarForm_ButtonList.h"
		
		int selCount = Scene->SelectionCount(true, OBJCLASS_DUMMY);
		if (selCount > 0)
		{
			xr_string text;
			text.sprintf("Selected: %d", selCount);
			float textWidth = ImGui::CalcTextSize(text.c_str()).x;
			float availableWidth = ImGui::GetContentRegionAvail().x;
			if (availableWidth > textWidth + 10.0f)
			{
				ImGui::SameLine(ImGui::GetWindowWidth() - textWidth - 10.0f);
				ImGui::Text("%s", text.c_str());
			}
		}
	RefreshBar();
}

UITopBarForm::~UITopBarForm()
{}

void UITopBarForm::Draw()
{
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + UI->GetMenuBarHeight()));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, UIToolBarSize));
	ImGui::SetNextWindowViewport(viewport->ID);

	ImGuiWindowFlags window_flags = 0 | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 2));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(2, 2));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
	ImGui::Begin("TOOLBAR", NULL, window_flags);
	{
#define ADD_BUTTON_IMAGE_S(Name)                                                                                                                                                             \
	m_t##Name->Load();                                                                                                                                                                       \
	if (ImGui::ImageButton(m_t##Name->surface_get(), ImVec2(20, 20), ImVec2(m_time##Name > EDevice.TimerAsync() ? 0.5 : 0, 0), ImVec2(m_time##Name > EDevice.TimerAsync() ? 1 : 0.5, 1), 0)) \
	{                                                                                                                                                                                        \
		m_time##Name = EDevice.TimerAsync() + 130;                                                                                                                                           \
		Click##Name();                                                                                                                                                                       \
	}                                                                                                                                                                                        \
	ShowTopBarTooltipIfHovered(#Name);                                                                                                                                                       \
	ImGui::SameLine();
#define ADD_BUTTON_IMAGE_D(Name)                                                                                                         \
	m_t##Name->Load();                                                                                                                   \
	if (ImGui::ImageButton(m_t##Name->surface_get(), ImVec2(20, 20), ImVec2(m_b##Name ? 0.5 : 0, 0), ImVec2(m_b##Name ? 1 : 0.5, 1), 0)) \
	{                                                                                                                                    \
		m_b##Name = !m_b##Name;                                                                                                          \
		Click##Name();                                                                                                                   \
	}                                                                                                                                    \
	ShowTopBarTooltipIfHovered(#Name);                                                                                                   \
	ImGui::SameLine();
#define ADD_BUTTON_IMAGE_P(Name)                                                                                                         \
	m_t##Name->Load();                                                                                                                   \
	if (ImGui::ImageButton(m_t##Name->surface_get(), ImVec2(20, 20), ImVec2(m_b##Name ? 0.5 : 0, 0), ImVec2(m_b##Name ? 1 : 0.5, 1), 0)) \
	{                                                                                                                                    \
		Click##Name();                                                                                                                   \
	}                                                                                                                                    \
	ShowTopBarTooltipIfHovered(#Name);                                                                                                   \
	ImGui::SameLine();
#define ADD_BUTTON_IMAGE_T1(Class, Name)                                 \
	ImGui::PushID("" #Class);                                            \
	ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor::HSV(0, 0, 0)); \
	if (ImGui::Button("" #Name, ImVec2(20, 20)))                         \
	{                                                                    \
		Click##Class##Name();                                            \
	}                                                                    \
	ShowTopBarTooltipIfHovered(#Name);                                   \
	ImGui::SameLine();                                                   \
	ImGui::PopStyleColor(1);                                             \
	ImGui::PopID();
#define ADD_BUTTON_IMAGE_T2(Class, Name)         \
	ImGui::PushID("" #Class);                    \
	if (ImGui::Button("" #Name, ImVec2(20, 20))) \
	{                                            \
		Click##Class##Name();                    \
	}                                            \
	ShowTopBarTooltipIfHovered(#Name);           \
	ImGui::SameLine();                           \
	ImGui::PopID();
#define ADD_BUTTON_IMAGE_T1_1(Class, Name, T)                            \
	ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor::HSV(0, 0, 0)); \
	if (ImGui::Button("" #T, ImVec2(20, 20)))                            \
	{                                                                    \
		Click##Class##Name();                                            \
	}                                                                    \
	ShowTopBarTooltipIfHovered(#Name);                                   \
	ImGui::SameLine();                                                   \
	ImGui::PopStyleColor(1);
#include "UITopBarForm_ButtonList.h"
		
		int selCount = Scene->SelectionCount(true, OBJCLASS_DUMMY);
		if (selCount > 0)
		{
			xr_string text;
			text.sprintf("Selected: %d", selCount);
			float textWidth = ImGui::CalcTextSize(text.c_str()).x;
			float availableWidth = ImGui::GetContentRegionAvail().x;
			if (availableWidth > textWidth + 10.0f)
			{
				ImGui::SameLine(ImGui::GetWindowWidth() - textWidth - 10.0f);
				ImGui::Text("%s", text.c_str());
			}
		}
	}
	ImGui::End();
	ImGui::PopStyleVar(5);
}
void UITopBarForm::RefreshBar()
{
	{
		m_bSelect = false;
		m_bAdd = false;
		m_bMove = false;
		m_bRotate = false;
		m_bScale = false;

		switch (Tools->GetAction())
		{
		case etaSelect:
			m_bSelect = true;
			break;
		case etaAdd:
			m_bAdd = true;
			break;
		case etaMove:
			m_bMove = true;
			break;
		case etaRotate:
			m_bRotate = true;
			break;
		case etaScale:
			m_bScale = true;
			break;
		default:
			THROW;
		}
	}
	{
		m_bX = false;
		m_bY = false;
		m_bZ = false;
		m_bZX = false;
		// axis
		switch (Tools->GetAxis())
		{
		case etAxisX:
			m_bX = true;
			break;
		case etAxisY:
			m_bY = true;
			break;
		case etAxisZ:
			m_bZ = true;
			break;
		case etAxisZX:
			m_bZX = true;
			break;
		default:
			Msg("! [TopBar] Unknown State: Action[%d] Axis[%d]", Tools->GetAction(), Tools->GetAxis());
		}
	}
	// settings
	m_bCsLocal = Tools->GetSettings(etfCSParent);
	m_bNuScale = Tools->GetSettings(etfNUScale);
	m_bNSnap = Tools->GetSettings(etfNormalAlign);
	m_bGSnap = Tools->GetSettings(etfGSnap);
	m_bOSnap = Tools->GetSettings(etfOSnap);
	m_bMoveToSnap = Tools->GetSettings(etfMTSnap);
	m_bVSnap = Tools->GetSettings(etfVSnap);
	m_bASnap = Tools->GetSettings(etfASnap);
	m_bMSnap = Tools->GetSettings(etfMSnap);
}

void UITopBarForm::ClickUndo()
{
	ExecCommand(COMMAND_UNDO);
}

void UITopBarForm::ClickRedo()
{
	ExecCommand(COMMAND_REDO);
}
void UITopBarForm::ClickZoom()
{
	ExecCommand(COMMAND_ZOOM_EXTENTS, FALSE);
}

void UITopBarForm::ClickRunInGame()
{
	// Сначала получаем полный путь через алиас $maps$
	string_path full_map_path;
	FS.update_path(full_map_path, "$maps$", "ingame.level");

	// Сохраняем именно по полному пути, а не относительным именем
	ExecCommand(COMMAND_SAVE_INGAME, xr_string(full_map_path));

	char module_path[MAX_PATH];
	GetModuleFileNameA(NULL, module_path, MAX_PATH);
	std::string exe_path(module_path);
	std::string exe_dir = exe_path.substr(0, exe_path.find_last_of("\\/"));
	std::string sdk_root_path = exe_dir.substr(0, exe_dir.find_last_of("\\/")) + "\\";

	std::string bat_path = sdk_root_path + "ingame.bat";
	std::string map_path = full_map_path;

	bool hideAIMap =
		!Scene->GetTool(OBJCLASS_AIMAP) ||
		!Scene->GetTool(OBJCLASS_AIMAP)->IsVisible();
	bool hideSectors =
		!Scene->GetTool(OBJCLASS_SECTOR) ||
		!Scene->GetTool(OBJCLASS_SECTOR)->IsVisible();
	bool hidePortals =
		!Scene->GetTool(OBJCLASS_PORTAL) ||
		!Scene->GetTool(OBJCLASS_PORTAL)->IsVisible();
	bool hideDetails =
		!Scene->GetTool(OBJCLASS_DO) ||
		!Scene->GetTool(OBJCLASS_DO)->IsVisible();
	bool hideSpawn =
		!Scene->GetTool(OBJCLASS_SPAWNPOINT) ||
		!Scene->GetTool(OBJCLASS_SPAWNPOINT)->IsVisible();
	bool hideSceneObj =
		!Scene->GetTool(OBJCLASS_SCENEOBJECT) ||
		!Scene->GetTool(OBJCLASS_SCENEOBJECT)->IsVisible();

	std::string params = "\"" + map_path + "\"";
	if (hideAIMap)    params += " -hide_aimap";
	if (hideSectors)  params += " -hide_sectors";
	if (hidePortals)  params += " -hide_portals";
	if (hideDetails)  params += " -hide_details";
	if (hideSpawn)    params += " -hide_spawn";
	if (hideSceneObj) params += " -hide_sceneobj";

	ShellExecuteA(
		NULL,
		"open",
		bat_path.c_str(),
		params.c_str(),
		sdk_root_path.c_str(),
		SW_SHOW
	);
}

void UITopBarForm::ClickZoomSel()
{
	ExecCommand(COMMAND_ZOOM_EXTENTS, TRUE);
}
void UITopBarForm::ClickSelect()
{
	ExecCommand(COMMAND_CHANGE_ACTION, etaSelect);
	m_bSelect = true;
	m_bAdd = false;
	m_bMove = false;
	m_bRotate = false;
	m_bScale = false;
}
void UITopBarForm::ClickAdd()
{
	ExecCommand(COMMAND_CHANGE_ACTION, etaAdd);
	m_bSelect = false;
	m_bAdd = true;
	m_bMove = false;
	m_bRotate = false;
	m_bScale = false;
}
void UITopBarForm::ClickMove()
{
	ExecCommand(COMMAND_CHANGE_ACTION, etaMove);
	m_bSelect = false;
	m_bAdd = false;
	m_bMove = true;
	m_bRotate = false;
	m_bScale = false;
}
void UITopBarForm::ClickRotate()
{
	ExecCommand(COMMAND_CHANGE_ACTION, etaRotate);
	m_bSelect = false;
	m_bAdd = false;
	m_bMove = false;
	m_bRotate = true;
	m_bScale = false;
}
void UITopBarForm::ClickScale()
{
	ExecCommand(COMMAND_CHANGE_ACTION, etaScale);
	m_bSelect = false;
	m_bAdd = false;
	m_bMove = false;
	m_bRotate = false;
	m_bScale = true;
}
void UITopBarForm::ClickX()
{
	// Если X уже выбран, то при повторном клике сбрасываем в Undefined
	ETAxis newAxis = (Tools->GetAxis() == etAxisX) ? etAxisUndefined : etAxisX;
	ExecCommand(COMMAND_CHANGE_AXIS, newAxis);
	RefreshBar(); // Обновляем состояние кнопок (подсветку)
}

void UITopBarForm::ClickY()
{
	ETAxis newAxis = (Tools->GetAxis() == etAxisY) ? etAxisUndefined : etAxisY;
	ExecCommand(COMMAND_CHANGE_AXIS, newAxis);
	RefreshBar();
}

void UITopBarForm::ClickZ()
{
	ETAxis newAxis = (Tools->GetAxis() == etAxisZ) ? etAxisUndefined : etAxisZ;
	ExecCommand(COMMAND_CHANGE_AXIS, newAxis);
	RefreshBar();
}

void UITopBarForm::ClickZX()
{
	ETAxis newAxis = (Tools->GetAxis() == etAxisZX) ? etAxisUndefined : etAxisZX;
	ExecCommand(COMMAND_CHANGE_AXIS, newAxis);
	RefreshBar();
}

void UITopBarForm::ClickCsLocal() { ExecCommand(COMMAND_SET_SETTINGS, etfCSParent, m_bCsLocal); }
void UITopBarForm::ClickNuScale() { ExecCommand(COMMAND_SET_SETTINGS, etfNUScale, m_bNuScale); }
void UITopBarForm::ClickGSnap() { ExecCommand(COMMAND_SET_SETTINGS, etfGSnap, m_bGSnap); }
void UITopBarForm::ClickOSnap() { ExecCommand(COMMAND_SET_SETTINGS, etfOSnap, m_bOSnap); }
void UITopBarForm::ClickMoveToSnap() { ExecCommand(COMMAND_SET_SETTINGS, etfMTSnap, m_bMoveToSnap); }
void UITopBarForm::ClickNSnap() { ExecCommand(COMMAND_SET_SETTINGS, etfNormalAlign, m_bNSnap); }
void UITopBarForm::ClickVSnap() { ExecCommand(COMMAND_SET_SETTINGS, etfVSnap, m_bVSnap); }
void UITopBarForm::ClickASnap() { ExecCommand(COMMAND_SET_SETTINGS, etfASnap, m_bASnap); }
void UITopBarForm::ClickMSnap() { ExecCommand(COMMAND_SET_SETTINGS, etfMSnap, m_bMSnap); }

void UITopBarForm::ClickCameraP()
{
	EDevice.m_Camera.SetStyle(csPlaneMove);
	UI->RedrawScene();
}
void UITopBarForm::ClickCameraA()
{
	EDevice.m_Camera.SetStyle(cs3DArcBall);
	UI->RedrawScene();
}
void UITopBarForm::ClickCameraF()
{
	EDevice.m_Camera.SetStyle(csFreeFly);
	UI->RedrawScene();
}

void UITopBarForm::ClickViewB1()
{
	EDevice.m_Camera.ViewBack();
	UI->RedrawScene();
}
void UITopBarForm::ClickViewB2()
{
	EDevice.m_Camera.ViewBottom();
	UI->RedrawScene();
}
void UITopBarForm::ClickViewF()
{
	EDevice.m_Camera.ViewFront();
	UI->RedrawScene();
}
void UITopBarForm::ClickViewL()
{
	EDevice.m_Camera.ViewLeft();
	UI->RedrawScene();
}
void UITopBarForm::ClickViewR()
{
	EDevice.m_Camera.ViewRight();
	UI->RedrawScene();
}
void UITopBarForm::ClickViewT()
{
	EDevice.m_Camera.ViewTop();
	UI->RedrawScene();
}
void UITopBarForm::ClickViewX()
{
	EDevice.m_Camera.ViewReset();
	UI->RedrawScene();
}
