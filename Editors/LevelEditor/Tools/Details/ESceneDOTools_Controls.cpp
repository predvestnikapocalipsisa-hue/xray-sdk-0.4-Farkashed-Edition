#include "stdafx.h"
#include "ESceneDOTools.h"
#include "UI/Tools/UIDOTool.h"

class TUI_ControlDOAdd : public TUI_CustomControl
{
public:
	TUI_ControlDOAdd(int st, int act, ESceneToolBase *parent) : TUI_CustomControl(st, act, parent) {}

	virtual bool Start(TShiftState Shift)
	{
		EDetailManager *DM = (EDetailManager*)parent_tool;
		if (!DM->m_Base.Valid()) return false;
		SRayPickInfo pinf;
		if (Scene->RayPickObject(UI->ZFar(), UI->m_CurrentRStart, UI->m_CurrentRDir, OBJCLASS_SCENEOBJECT, &pinf, DM->GetSnapList()))
		{
			UIDOTool* tool = (UIDOTool*)DM->pForm;
			u32 clr = (Shift & ssShift) ? 0 : tool->GetBrushColor();
			float radius = tool->GetBrushRadius();
			DM->PaintBrush(pinf.pt, radius, clr);
			return true;
		}
		return false;
	}

	virtual void Move(TShiftState Shift)
	{
		EDetailManager *DM = (EDetailManager*)parent_tool;
		if (!DM->m_Base.Valid()) return;
		SRayPickInfo pinf;
		if (Scene->RayPickObject(UI->ZFar(), UI->m_CurrentRStart, UI->m_CurrentRDir, OBJCLASS_SCENEOBJECT, &pinf, DM->GetSnapList()))
		{
			UIDOTool* tool = (UIDOTool*)DM->pForm;
			u32 clr = (Shift & ssShift) ? 0 : tool->GetBrushColor();
			float radius = tool->GetBrushRadius();
			DM->PaintBrush(pinf.pt, radius, clr);
		}
	}

	virtual bool End(TShiftState Shift)
	{
		EDetailManager *DM = (EDetailManager*)parent_tool;
		if (DM->m_Base.Valid())
		{
			DM->m_Base.SaveImage();
			UI->RedrawScene();
			Scene->UndoSave();
		}
		return true;
	}
};

void EDetailManager::CreateControls()
{
	AddControl(xr_new<TUI_CustomControl>(estDefault, etaSelect, this));
	AddControl(xr_new<TUI_ControlDOAdd>(estDefault, etaAdd, this));
	// frame
	pForm = xr_new<UIDOTool>();
	((UIDOTool *)pForm)->DM = this;
}

void EDetailManager::RemoveControls()
{
	inherited::RemoveControls();
}
