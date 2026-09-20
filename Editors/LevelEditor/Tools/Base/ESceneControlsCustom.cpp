#include "stdafx.h"

// Флаг активной трансформации — подавляет Modified()/UPDATE_CAPTION во время движения мыши
static bool s_transformInProgress = false;

bool IsTransformInProgress() { return s_transformInProgress; }

TUI_CustomControl::TUI_CustomControl(int st, int act, ESceneToolBase* parent)
{
    parent_tool = parent;
    VERIFY(parent);
    sub_target = st;
    action = act;
    bBoxSelection = false;
}

bool TUI_CustomControl::Start(TShiftState _Shift)
{
    switch (action)
    {
    case etaSelect: return SelectStart(_Shift);
    case etaAdd:    return AddStart(_Shift);
    case etaMove:   return MovingStart(_Shift);
    case etaRotate: return RotateStart(_Shift);
    case etaScale:  return ScaleStart(_Shift);
    }
    return false;
}

bool TUI_CustomControl::End(TShiftState _Shift)
{
    switch (action)
    {
    case etaSelect: return SelectEnd(_Shift);
    case etaAdd:    return AddEnd(_Shift);
    case etaMove:   return MovingEnd(_Shift);
    case etaRotate: return RotateEnd(_Shift);
    case etaScale:  return ScaleEnd(_Shift);
    }
    return false;
}

void TUI_CustomControl::Move(TShiftState _Shift)
{
    switch (action)
    {
    case etaSelect: SelectProcess(_Shift);  break;
    case etaAdd:    AddProcess(_Shift);     break;
    case etaMove:   MovingProcess(_Shift);  break;
    case etaRotate: RotateProcess(_Shift);  break;
    case etaScale:  ScaleProcess(_Shift);   break;
    }
}

bool TUI_CustomControl::HiddenMode()
{
    switch (action)
    {
    case etaSelect: return false;
    case etaAdd:    return false;
    case etaMove:   return true;
    case etaRotate: return true;
    case etaScale:  return true;
    }
    return false;
}

//------------------------------------------------------------------------------
// add
//------------------------------------------------------------------------------
CCustomObject* TUI_CustomControl::DefaultAddObject(TShiftState Shift, TBeforeAppendCallback before, TAfterAppendCallback after)
{
    if (Shift == ssRBOnly)
    {
        ExecCommand(COMMAND_SHOWCONTEXTMENU, parent_tool->FClassID);
        return 0;
    }
    Fvector p, n;
    CCustomObject* obj = 0;
    if (LUI->PickGround(p, UI->m_CurrentRStart, UI->m_CurrentRDir, 1, &n))
    {
        SBeforeAppendCallbackParams P;
        if (before && !before(&P))
            return 0;

        string256 namebuffer;
        Scene->GenObjectName(parent_tool->FClassID, namebuffer, P.name_prefix.c_str());
        obj = Scene->GetOTool(parent_tool->FClassID)->CreateObject(P.data, namebuffer);
        if (!obj->Valid())
        {
            xr_delete(obj);
            return 0;
        }
        if (after && !after(Shift, obj))
        {
            xr_delete(obj);
            return 0;
        }
        obj->MoveTo(p, n);
        Scene->SelectObjects(false, parent_tool->FClassID);
        Scene->AppendObject(obj);
        if (Shift & ssCtrl)
            ExecCommand(COMMAND_SHOW_PROPERTIES);
        if (!(Shift & ssAlt))
            ResetActionToSelect();
    }
    return obj;
}

bool TUI_CustomControl::AddStart(TShiftState Shift)
{
    DefaultAddObject(Shift, 0);
    return false;
}
void TUI_CustomControl::AddProcess(TShiftState _Shift) {}
bool TUI_CustomControl::AddEnd(TShiftState _Shift) { return true; }

bool TUI_CustomControl::CheckSnapList(TShiftState Shift)
{
    if (MainForm->GetLeftBarForm()->IsSnapListMode())
    {
        CCustomObject* O = Scene->RayPickObject(UI->ZFar(), UI->m_CurrentRStart, UI->m_CurrentRDir, OBJCLASS_SCENEOBJECT, 0, 0);
        if (O)
        {
            if (Scene->FindObjectInSnapList(O))
            {
                if (Shift & ssAlt)
                    Scene->DelFromSnapList(O);
                else if (Shift & ssCtrl)
                    Scene->DelFromSnapList(O);
            }
            else
            {
                if (!(Shift & (ssCtrl | ssAlt)))
                    Scene->AddToSnapList(O);
                else if (Shift & ssCtrl)
                    Scene->AddToSnapList(O);
            }
            return true;
        }
        else
        {
            return false;
        }
    }
    return false;
}

//------------------------------------------------------------------------------
// select
//------------------------------------------------------------------------------
bool TUI_CustomControl::SelectStart(TShiftState Shift)
{
    ObjClassID cls = LTools->CurrentClassID();

    if (CheckSnapList(Shift))
        return false;
    if (Shift == ssRBOnly)
    {
        ExecCommand(COMMAND_SHOWCONTEXTMENU, parent_tool->FClassID);
        return false;
    }
    if (!((Shift & ssCtrl) || (Shift & ssAlt)))
        Scene->SelectObjects(false, cls);

    int cnt = Scene->RaySelect((Shift & ssCtrl) ? -1 : (Shift & ssAlt) ? 0 : 1, parent_tool->FClassID);
    bBoxSelection = ((0 != cnt) && ((Shift & ssCtrl) || (Shift & ssAlt))) || (0 == cnt);
    if (bBoxSelection)
    {
        UI->EnableSelectionRect(true);
        UI->UpdateSelectionRect(UI->m_StartCp, UI->m_CurrentCp);
        return true;
    }
    return false;
}

void TUI_CustomControl::SelectProcess(TShiftState _Shift)
{
    if (bBoxSelection)
        UI->UpdateSelectionRect(UI->m_StartCp, UI->m_CurrentCp);
}

bool TUI_CustomControl::SelectEnd(TShiftState _Shift)
{
    if (bBoxSelection)
    {
        UI->EnableSelectionRect(false);
        bBoxSelection = false;
        Scene->FrustumSelect(_Shift & ssAlt ? 0 : 1, LTools->CurrentClassID());
    }
    return true;
}

//------------------------------------------------------------------------------
// moving
//------------------------------------------------------------------------------
bool TUI_CustomControl::MovingStart(TShiftState Shift)
{
    ObjClassID cls = LTools->CurrentClassID();

    if (Shift == ssRBOnly)
    {
        ExecCommand(COMMAND_SHOWCONTEXTMENU, parent_tool->FClassID);
        return false;
    }
    if (Scene->SelectionCount(true, cls) == 0)
        return false;

    if (Shift & ssCtrl)
    {
        ObjectList lst;
        if (Scene->GetQueryObjects(lst, LTools->CurrentClassID(), 1, 1, 0))
        {
            xr_vector<ObjectTransformState> snapTransforms;
            for (CCustomObject* obj : lst)
            {
                ObjectTransformState state;
                state.name = obj->GetName();
                state.classID = obj->FClassID;
                state.pos_before = obj->GetPosition();
                state.rot_before = obj->GetRotation();
                state.scale_before = obj->GetScale();
                snapTransforms.push_back(state);
            }

            if (lst.size() == 1)
            {
                Fvector p, n;
                UI->IR_GetMousePosReal(EDevice.m_hWnd, UI->m_CurrentCp);
                EDevice.m_Camera.MouseRayFromPoint(UI->m_CurrentRStart, UI->m_CurrentRDir, UI->m_CurrentCp);
                if (LUI->PickGround(p, UI->m_CurrentRStart, UI->m_CurrentRDir, 1, &n))
                {
                    for (ObjectIt _F = lst.begin(); _F != lst.end(); _F++)
                        (*_F)->MoveTo(p, n);
                }
            }
            else
            {
                Fvector p, n;
                Fvector D = { 0, -1, 0 };
                for (ObjectIt _F = lst.begin(); _F != lst.end(); _F++)
                {
                    if (LUI->PickGround(p, (*_F)->GetPosition(), D, 1, &n))
                        (*_F)->MoveTo(p, n);
                }
            }

            bool anyChanged = false;
            for (auto& state : snapTransforms)
            {
                CCustomObject* obj = Scene->FindObjectByName(state.name.c_str(), state.classID);
                if (obj)
                {
                    state.pos_after = obj->GetPosition();
                    state.rot_after = obj->GetRotation();
                    state.scale_after = obj->GetScale();
                    if (!state.pos_before.similar(state.pos_after, EPS))
                        anyChanged = true;
                }
            }
            if (anyChanged)
            {
                Scene->UndoSaveTransform("Move To Ground", snapTransforms);
            }
        }
        return false;
    }
    else
    {
        if (etAxisY == Tools->GetAxis())
        {
            m_MovingXVector.set(0, 0, 0);
            m_MovingYVector.set(0, 1, 0);
        }
        else
        {
            m_MovingXVector.set(EDevice.m_Camera.GetRight());
            m_MovingXVector.y = 0;
            m_MovingYVector.set(EDevice.m_Camera.GetDirection());
            m_MovingYVector.y = 0;
            m_MovingXVector.normalize_safe();
            m_MovingYVector.normalize_safe();
        }
        m_MovingReminder.set(0, 0, 0);
    }

    m_PendingTransforms.clear();
    ObjectList lst;
    if (Scene->GetQueryObjects(lst, LTools->CurrentClassID(), 1, 1, 0))
    {
        for (CCustomObject* obj : lst)
        {
            ObjectTransformState state;
            state.name = obj->GetName();
            state.classID = obj->FClassID;
            state.pos_before = obj->GetPosition();
            state.rot_before = obj->GetRotation();
            state.scale_before = obj->GetScale();
            m_PendingTransforms.push_back(state);
        }
    }

    s_transformInProgress = true;
    return true;
}

bool TUI_CustomControl::DefaultMovingProcess(TShiftState Shift, Fvector& amount)
{
    if ((Shift & ssLeft) || (Shift & ssRight))
    {
        float mouseSM = UI->m_MouseSM;
        float moveSnap = Tools->m_MoveSnap;
        if (Shift & ssShift)
        {
            mouseSM *= Tools->m_ShiftFineFactor;
            moveSnap *= Tools->m_ShiftFineFactor;
        }

        amount.mul(m_MovingXVector, mouseSM * UI->m_DeltaCpH.x);
        amount.mad(amount, m_MovingYVector, -mouseSM * UI->m_DeltaCpH.y);

        if (Tools->GetSettings(etfMSnap))
        {
            CHECK_SNAP(m_MovingReminder.x, amount.x, moveSnap);
            CHECK_SNAP(m_MovingReminder.y, amount.y, moveSnap);
            CHECK_SNAP(m_MovingReminder.z, amount.z, moveSnap);
        }

        if (!(etAxisX == Tools->GetAxis()) && !(etAxisZX == Tools->GetAxis()) && !(etAxisXY == Tools->GetAxis()) && !(etAxisCAM == Tools->GetAxis()))
            amount.x = 0.f;
        if (!(etAxisZ == Tools->GetAxis()) && !(etAxisZX == Tools->GetAxis()) && !(etAxisYZ == Tools->GetAxis()) && !(etAxisCAM == Tools->GetAxis()))
            amount.z = 0.f;
        if (!(etAxisY == Tools->GetAxis()) && !(etAxisXY == Tools->GetAxis()) && !(etAxisYZ == Tools->GetAxis()) && !(etAxisCAM == Tools->GetAxis()))
            amount.y = 0.f;

        return (amount.square_magnitude() > EPS_S);
    }
    return false;
}

void TUI_CustomControl::MovingProcess(TShiftState _Shift)
{
    Fvector amount;
    if (DefaultMovingProcess(_Shift, amount))
    {
        ObjectList lst;
        if (Scene->GetQueryObjects(lst, LTools->CurrentClassID(), 1, 1, 0))
            for (ObjectIt _F = lst.begin(); _F != lst.end(); _F++)
                (*_F)->Move(amount);
        UI->RedrawScene();
    }
}

bool TUI_CustomControl::MovingEnd(TShiftState _Shift)
{
    s_transformInProgress = false;

    bool anyChanged = false;
    for (auto& state : m_PendingTransforms)
    {
        CCustomObject* obj = Scene->FindObjectByName(state.name.c_str(), state.classID);
        if (obj)
        {
            state.pos_after = obj->GetPosition();
            state.rot_after = obj->GetRotation();
            state.scale_after = obj->GetScale();

            if (!state.pos_before.similar(state.pos_after, EPS) ||
                !state.rot_before.similar(state.rot_after, EPS) ||
                !state.scale_before.similar(state.scale_after, EPS))
            {
                anyChanged = true;
            }
        }
    }

    if (anyChanged)
    {
        Scene->UndoSaveTransform("Move Object(s)", m_PendingTransforms);
    }
    m_PendingTransforms.clear();

    ExecCommand(COMMAND_UPDATE_PROPERTIES);
    ExecCommand(COMMAND_UPDATE_CAPTION);
    return true;
}

//------------------------------------------------------------------------------
// rotate
//------------------------------------------------------------------------------
bool TUI_CustomControl::RotateStart(TShiftState Shift)
{
    ObjClassID cls = LTools->CurrentClassID();

    if (Shift == ssRBOnly)
    {
        ExecCommand(COMMAND_SHOWCONTEXTMENU, parent_tool->FClassID);
        return false;
    }
    if (Scene->SelectionCount(true, cls) == 0)
        return false;

    m_RotateVector.set(0, 0, 0);
    if (etAxisX == Tools->GetAxis())
        m_RotateVector.set(1, 0, 0);
    else if (etAxisY == Tools->GetAxis())
        m_RotateVector.set(0, 1, 0);
    else if (etAxisZ == Tools->GetAxis())
        m_RotateVector.set(0, 0, 1);
    else if (etAxisCAM == Tools->GetAxis())
        m_RotateVector.set(EDevice.m_Camera.GetDirection());
    else
        m_RotateVector.set(0, 1, 0);
    m_fRotateSnapAngle = 0;

    m_PendingTransforms.clear();
    ObjectList lst;
    if (Scene->GetQueryObjects(lst, LTools->CurrentClassID(), 1, 1, 0))
    {
        for (CCustomObject* obj : lst)
        {
            ObjectTransformState state;
            state.name = obj->GetName();
            state.classID = obj->FClassID;
            state.pos_before = obj->GetPosition();
            state.rot_before = obj->GetRotation();
            state.scale_before = obj->GetScale();
            m_PendingTransforms.push_back(state);
        }
    }

    s_transformInProgress = true;
    return true;
}

void TUI_CustomControl::RotateProcess(TShiftState _Shift)
{
    if (_Shift & ssLeft)
    {
        float mouseSR = UI->m_MouseSR;
        if (_Shift & ssShift)
            mouseSR *= Tools->m_ShiftFineFactor;

        float amount = -UI->m_DeltaCpH.x * mouseSR;

        bool bSnap = Tools->GetSettings(etfASnap) || (_Shift & ssCtrl);
        if (bSnap)
        {
            float snapAngle = (_Shift & ssCtrl) ? Tools->m_CtrlRotateSnapAngle : Tools->m_RotateSnapAngle;
            if (_Shift & ssShift)
                snapAngle *= Tools->m_ShiftFineFactor;
            CHECK_SNAP(m_fRotateSnapAngle, amount, snapAngle);
        }

        ObjectList lst;
        if (Scene->GetQueryObjects(lst, LTools->CurrentClassID(), 1, 1, 0))
            for (ObjectIt _F = lst.begin(); _F != lst.end(); _F++)
                if (Tools->GetSettings(etfCSParent))
                    (*_F)->RotateParent(m_RotateVector, amount);
                else
                    (*_F)->RotateLocal(m_RotateVector, amount);

        UI->RedrawScene();
    }
}

bool TUI_CustomControl::RotateEnd(TShiftState _Shift)
{
    s_transformInProgress = false;

    bool anyChanged = false;
    for (auto& state : m_PendingTransforms)
    {
        CCustomObject* obj = Scene->FindObjectByName(state.name.c_str(), state.classID);
        if (obj)
        {
            state.pos_after = obj->GetPosition();
            state.rot_after = obj->GetRotation();
            state.scale_after = obj->GetScale();

            if (!state.pos_before.similar(state.pos_after, EPS) ||
                !state.rot_before.similar(state.rot_after, EPS) ||
                !state.scale_before.similar(state.scale_after, EPS))
            {
                anyChanged = true;
            }
        }
    }

    if (anyChanged)
    {
        Scene->UndoSaveTransform("Rotate Object(s)", m_PendingTransforms);
    }
    m_PendingTransforms.clear();

    ExecCommand(COMMAND_UPDATE_PROPERTIES);
    ExecCommand(COMMAND_UPDATE_CAPTION);
    return true;
}

//------------------------------------------------------------------------------
// scale
//------------------------------------------------------------------------------
bool TUI_CustomControl::ScaleStart(TShiftState Shift)
{
    ObjClassID cls = LTools->CurrentClassID();
    if (Shift == ssRBOnly)
    {
        ExecCommand(COMMAND_SHOWCONTEXTMENU, parent_tool->FClassID);
        return false;
    }
    if (Scene->SelectionCount(true, cls) == 0)
        return false;

    m_PendingTransforms.clear();
    ObjectList lst;
    if (Scene->GetQueryObjects(lst, LTools->CurrentClassID(), 1, 1, 0))
    {
        for (CCustomObject* obj : lst)
        {
            ObjectTransformState state;
            state.name = obj->GetName();
            state.classID = obj->FClassID;
            state.pos_before = obj->GetPosition();
            state.rot_before = obj->GetRotation();
            state.scale_before = obj->GetScale();
            m_PendingTransforms.push_back(state);
        }
    }

    s_transformInProgress = true;
    return true;
}

void TUI_CustomControl::ScaleProcess(TShiftState _Shift)
{
    float mouseSS = UI->m_MouseSS;
    if (_Shift & ssShift)
        mouseSS *= Tools->m_ShiftFineFactor;

    float dy = UI->m_DeltaCpH.x * mouseSS;
    if (dy > 1.f)
        dy = 1.f;
    else if (dy < -1.f)
        dy = -1.f;

    Fvector amount;
    amount.set(dy, dy, dy);

    if (Tools->GetSettings(etfNUScale) && Tools->GetAxis() != etAxisCAM)
    {
        if (!(etAxisX == Tools->GetAxis()) && !(etAxisZX == Tools->GetAxis()) && !(etAxisXY == Tools->GetAxis()))
            amount.x = 0.f;
        if (!(etAxisZ == Tools->GetAxis()) && !(etAxisZX == Tools->GetAxis()) && !(etAxisYZ == Tools->GetAxis()))
            amount.z = 0.f;
        if (!(etAxisY == Tools->GetAxis()) && !(etAxisXY == Tools->GetAxis()) && !(etAxisYZ == Tools->GetAxis()))
            amount.y = 0.f;
    }

    ObjectList lst;
    if (Scene->GetQueryObjects(lst, LTools->CurrentClassID(), 1, 1, 0))
        for (ObjectIt _F = lst.begin(); _F != lst.end(); _F++)
            (*_F)->Scale(amount);

    UI->RedrawScene();
}

bool TUI_CustomControl::ScaleEnd(TShiftState _Shift)
{
    s_transformInProgress = false;

    bool anyChanged = false;
    for (auto& state : m_PendingTransforms)
    {
        CCustomObject* obj = Scene->FindObjectByName(state.name.c_str(), state.classID);
        if (obj)
        {
            state.pos_after = obj->GetPosition();
            state.rot_after = obj->GetRotation();
            state.scale_after = obj->GetScale();

            if (!state.pos_before.similar(state.pos_after, EPS) ||
                !state.rot_before.similar(state.rot_after, EPS) ||
                !state.scale_before.similar(state.scale_after, EPS))
            {
                anyChanged = true;
            }
        }
    }

    if (anyChanged)
    {
        Scene->UndoSaveTransform("Scale Object(s)", m_PendingTransforms);
    }
    m_PendingTransforms.clear();

    ExecCommand(COMMAND_UPDATE_PROPERTIES);
    ExecCommand(COMMAND_UPDATE_CAPTION);
    return true;
}