#include "stdafx.h"

TUI_ControlSpawnAdd::TUI_ControlSpawnAdd(int st, int act, ESceneToolBase *parent) : TUI_CustomControl(st, act, parent)
{
}

bool TUI_ControlSpawnAdd::AppendCallback(SBeforeAppendCallbackParams *p)
{
    LPCSTR ref_name = ((UISpawnTool *)parent_tool->pForm)->Current();
    if (!ref_name)
    {
        ELog.DlgMsg(mtInformation, "Nothing selected.");
        return false;
    }
    if (Scene->LevelPrefix().c_str())
    {
        p->name_prefix = Scene->LevelPrefix().c_str();
        p->name_prefix += "_";
    }
    p->name_prefix += ref_name;
    p->data = (void *)ref_name;
    return (0 != p->name_prefix.length());
}

bool TUI_ControlSpawnAdd::AfterAppendCallback(TShiftState Shift, CCustomObject *obj)
{
    UISpawnTool *F = (UISpawnTool *)parent_tool->pForm;

    // Only do auto-shape if the option is enabled
    if (!F->IsAutoShape())
        return true;

    CSpawnPoint *sp = dynamic_cast<CSpawnPoint *>(obj);
    if (!sp)
        return true;

    // Check that this spawn entity actually supports an attached shape
    if (!sp->m_SpawnData.Valid() || !sp->m_SpawnData.m_Data->shape())
        return true; // no shape slot -- skip silently

    // Create the shape object (not yet added to scene -- AttachObject will handle that)
    CCustomObject *shapeObj = Scene->GetOTool(OBJCLASS_SHAPE)->CreateObject(0, 0);
    if (!shapeObj)
        return true;

    CEditShape *shape = dynamic_cast<CEditShape *>(shapeObj);
    if (!shape)
    {
        xr_delete(shapeObj);
        return true;
    }

    float sz = F->GetAutoShapeSize();

    if (F->IsAutoShapeSphere())
    {
        Fsphere S;
        S.P.set(0, 0, 0);
        S.R = sz;
        shape->add_sphere(S);
    }
    else
    {
        Fmatrix M;
        M.identity();
        M.scale(sz, sz, sz);
        shape->add_box(M);
    }

    // AttachObject: adds shapeObj to scene, links it to sp
    if (!sp->AttachObject(shapeObj))
    {
        // If attach failed (e.g. shape already attached), add to scene standalone
        Scene->AppendObject(shapeObj);
    }

    return true;
}

bool TUI_ControlSpawnAdd::Start(TShiftState Shift)
{
    UISpawnTool *F = (UISpawnTool *)parent_tool->pForm;
    if (F->IsAttachObject())
    {
        CCustomObject *from = Scene->RayPickObject(UI->ZFar(), UI->m_CurrentRStart, UI->m_CurrentRDir, OBJCLASS_DUMMY, 0, 0);
        if (from->FClassID != OBJCLASS_SPAWNPOINT)
        {
            ObjectList lst;
            int cnt = Scene->GetQueryObjects(lst, OBJCLASS_SPAWNPOINT, 1, 1, 0);
            if (1 != cnt)
                ELog.DlgMsg(mtError, "Select one shape.");
            else
            {
                CSpawnPoint *base = dynamic_cast<CSpawnPoint *>(lst.back());
                R_ASSERT(base);
                if (base->AttachObject(from))
                {
                    if (!(Shift & ssAlt))
                    {
                        F->SetAttachObject(false);
                        ResetActionToSelect();
                    }
                }
                else
                {
                    ELog.DlgMsg(mtError, "Attach impossible.");
                }
            }
        }
        else
        {
            ELog.DlgMsg(mtError, "Attach impossible.");
        }
    }
    else
    {
        DefaultAddObject(Shift,
            TBeforeAppendCallback(this, &TUI_ControlSpawnAdd::AppendCallback),
            TAfterAppendCallback(this, &TUI_ControlSpawnAdd::AfterAppendCallback));
    }
    return false;
}