//---------------------------------------------------------------------------

#include "stdafx.h"
#pragma hdrstop

#include "UI_ToolsCustom.h"
#include "EditObject.h"
#include "EditMesh.h"
#include "ui_main.h"
#include "library.h"
#include "d3dutils.h"

//------------------------------------------------------------------------------
CToolCustom *Tools = 0;
//------------------------------------------------------------------------------
#define CHECK_SNAP(R, A, C)   \
    {                         \
        R += A;               \
        if (fabsf(R) >= C)    \
        {                     \
            A = snapto(R, C); \
            R = 0;            \
        }                     \
        else                  \
        {                     \
            A = 0;            \
        }                     \
    }

CToolCustom::CToolCustom()
{
    m_bReady = false;
    m_CtrlRotateSnapAngle = deg2rad(15.f);
    m_ShiftFineFactor = 0.1f;
    m_Action = etaSelect;
    m_Settings.assign(etfNormalAlign | etfGSnap | etfOSnap | etfMTSnap | etfVSnap | etfASnap | etfMSnap);
    m_Axis = etAxisZX;
    m_HoverAxis = etAxisUndefined;
    m_GizmoScale = 1.0f;
    fFogness = 0.9f;
    dwFogColor = 0xffffffff;
    m_pAxisMoveObject = NULL;
    m_axis_xform = Fidentity;
}
//---------------------------------------------------------------------------

CToolCustom::~CToolCustom()
{
}
//---------------------------------------------------------------------------

bool CToolCustom::OnCreate()
{
    m_bReady = true;

    SetAction(etaSelect);

    return true;
}

void CToolCustom::OnDestroy()
{
    Lib.RemoveEditObject(m_pAxisMoveObject);
    VERIFY(m_bReady);
    m_bReady = false;
    m_CtrlRotateSnapAngle = deg2rad(15.f);
    m_ShiftFineFactor = 0.1f;
}
//---------------------------------------------------------------------------

void CToolCustom::SetAction(ETAction action)
{
    switch (action)
    {
    case etaSelect:
        m_bHiddenMode = false;
        break;
    case etaAdd:
    case etaMove:
    case etaRotate:
    case etaScale:
        m_bHiddenMode = true;
        break;
    }
    m_Action = action;
    /*  switch(m_Action)
      {
      case etaSelect:
          UI->GetD3DWindow()->Cursor = crCross;
      break;
      case etaAdd:
          UI->GetD3DWindow()->Cursor = crArrow;
      break;
      case etaMove:
          if(!EPrefs->tools_show_move_axis)
              UI->GetD3DWindow()->Cursor = crSizeAll;
          else
              UI->GetD3DWindow()->Cursor = crHandPoint;

      break;
      case etaRotate:
          UI->GetD3DWindow()->Cursor = crSizeWE;
      break;
      case etaScale:
          UI->GetD3DWindow()->Cursor = crVSplit;
      break;
      default:
          UI->GetD3DWindow()->Cursor = crHelp;
      }*/

    if (m_Action == etaMove)
    {
        if (!m_pAxisMoveObject && EPrefs->tools_show_move_axis)
        {
            m_pAxisMoveObject = Lib.CreateEditObject("editor\\axis");
            m_Axis = etAxisUndefined;
        }
    }
    else
    {
        if (m_pAxisMoveObject)
            Lib.RemoveEditObject(m_pAxisMoveObject);
    }
    UI->RedrawScene();
    ExecCommand(COMMAND_REFRESH_UI_BAR);
}

void CToolCustom::SetAxis(ETAxis axis)
{
    m_Axis = axis;
    UI->RedrawScene();
    ExecCommand(COMMAND_REFRESH_UI_BAR);
}

void CToolCustom::SetSettings(u32 mask, BOOL val)
{
    m_Settings.set(mask, val);
    UI->RedrawScene();
    ExecCommand(COMMAND_REFRESH_UI_BAR);
}

bool CToolCustom::MouseStart(TShiftState Shift)
{
    switch (m_Action)
    {
    case etaSelect:
        break;
    case etaAdd:
        break;
    case etaMove:
        if (etAxisY == m_Axis)
        {
            m_MoveXVector.set(0, 0, 0);
            m_MoveYVector.set(0, 1, 0);
        }
        else
        {
            m_MoveXVector.set(EDevice.m_Camera.GetRight());
            m_MoveXVector.y = 0;
            m_MoveYVector.set(EDevice.m_Camera.GetDirection());
            m_MoveYVector.y = 0;
            m_MoveXVector.normalize_safe();
            m_MoveYVector.normalize_safe();
        }
        m_MoveReminder.set(0, 0, 0);
        m_MovedAmount.set(0, 0, 0);
        break;
    case etaRotate:
        m_RotateCenter.set(0, 0, 0);
        m_RotateVector.set(0, 0, 0);
        m_fRotateSnapValue = 0.0f;
        m_RotateAmount = 0.0f;

        if (etAxisX == m_Axis)
            m_RotateVector.set(1, 0, 0);
        else if (etAxisY == m_Axis)
            m_RotateVector.set(0, 1, 0);
        else if (etAxisZ == m_Axis)
            m_RotateVector.set(0, 0, 1);
        else if (etAxisCAM == m_Axis)
            m_RotateVector.set(EDevice.m_Camera.GetDirection());
        break;
    case etaScale:
        m_ScaleAmount.set(0, 0, 0);
        break;
    }

    if (m_Action == etaMove || m_Action == etaRotate || m_Action == etaScale)
    {
        ETAxis picked = PickGizmo(UI->m_CurrentRStart, UI->m_CurrentRDir);
        if (picked != etAxisUndefined)
        {
            SetAxis(picked);
        }
    }

    return m_bHiddenMode;
}

bool CToolCustom::MouseEnd(TShiftState Shift)
{
    switch (m_Action)
    {
    case etaSelect:
        break;
    case etaAdd:
        break;
    case etaMove:
        break;
    case etaRotate:
        break;
    case etaScale:
        break;
    }
    return true;
}

void CToolCustom::MouseMove(TShiftState Shift)
{
    if (!m_bHiddenMode && (m_Action == etaMove || m_Action == etaRotate || m_Action == etaScale))
    {
        ETAxis prevHover = m_HoverAxis;
        m_HoverAxis = PickGizmo(UI->m_CurrentRStart, UI->m_CurrentRDir);
        if (prevHover != m_HoverAxis)
        {
            UI->RedrawScene();
        }
    }

    switch (m_Action)
    {
    case etaSelect:
        break;
    case etaAdd:
        break;
    case etaMove:
    {
        float mouseSM = UI->m_MouseSM;
        float moveSnap = m_MoveSnap;
        if (Shift & ssShift)
        {
            mouseSM *= m_ShiftFineFactor;
            moveSnap *= m_ShiftFineFactor;
        }

        m_MovedAmount.mul(m_MoveXVector, mouseSM * UI->m_DeltaCpH.x);
        m_MovedAmount.mad(m_MoveYVector, -mouseSM * UI->m_DeltaCpH.y);

        if (m_Settings.is(etfMSnap))
        {
            CHECK_SNAP(m_MoveReminder.x, m_MovedAmount.x, moveSnap);
            CHECK_SNAP(m_MoveReminder.y, m_MovedAmount.y, moveSnap);
            CHECK_SNAP(m_MoveReminder.z, m_MovedAmount.z, moveSnap);
        }

        if (!(etAxisX == m_Axis) && !(etAxisZX == m_Axis) && !(etAxisXY == m_Axis) && !(etAxisCAM == m_Axis))
            m_MovedAmount.x = 0.f;
        if (!(etAxisZ == m_Axis) && !(etAxisZX == m_Axis) && !(etAxisYZ == m_Axis) && !(etAxisCAM == m_Axis))
            m_MovedAmount.z = 0.f;
        if (!(etAxisY == m_Axis) && !(etAxisXY == m_Axis) && !(etAxisYZ == m_Axis) && !(etAxisCAM == m_Axis))
            m_MovedAmount.y = 0.f;
    }
    break;
    case etaRotate:
    {
        float mouseSR = UI->m_MouseSR;
        if (Shift & ssShift)
            mouseSR *= m_ShiftFineFactor;

        m_RotateAmount = -UI->m_DeltaCpH.x * mouseSR;

        bool bSnap = m_Settings.is(etfASnap) || (Shift & ssCtrl);
        if (bSnap)
        {
            float snapAngle = (Shift & ssCtrl) ? m_CtrlRotateSnapAngle : m_RotateSnapAngle;
            if (Shift & ssShift)
                snapAngle *= m_ShiftFineFactor;
            CHECK_SNAP(m_fRotateSnapValue, m_RotateAmount, snapAngle);
        }
    }
    break;
    case etaScale:
    {
        float mouseSS = UI->m_MouseSS;
        if (Shift & ssShift)
            mouseSS *= m_ShiftFineFactor;

        float dy = UI->m_DeltaCpH.x * mouseSS;
        if (dy > 1.f)
            dy = 1.f;
        else if (dy < -1.f)
            dy = -1.f;

        m_ScaleAmount.set(dy, dy, dy);

        if (m_Settings.is(etfNUScale) && m_Axis != etAxisCAM)
        {
            if (!(etAxisX == m_Axis) && !(etAxisZX == m_Axis) && !(etAxisXY == m_Axis))
                m_ScaleAmount.x = 0.f;
            if (!(etAxisZ == m_Axis) && !(etAxisZX == m_Axis) && !(etAxisYZ == m_Axis))
                m_ScaleAmount.z = 0.f;
            if (!(etAxisY == m_Axis) && !(etAxisXY == m_Axis) && !(etAxisYZ == m_Axis))
                m_ScaleAmount.y = 0.f;
        }
    }
    break;
    }
}

void CToolCustom::GetCurrentFog(u32 &fog_color, float &s_fog, float &e_fog)
{
    s_fog = psDeviceFlags.is(rsFog) ? (1.0f - fFogness) * 0.85f * UI->ZFar() : 0.99f * UI->ZFar();
    e_fog = psDeviceFlags.is(rsFog) ? 0.91f * UI->ZFar() : UI->ZFar();
    fog_color = dwFogColor;
}

void CToolCustom::RenderEnvironment()
{
}

void CToolCustom::Clear()
{
    ClearDebugDraw();
}

void CToolCustom::Render()
{
    // render errors
    EDevice.SetShader(EDevice.m_SelectionShader);
    RCache.set_xform_world(Fidentity);
    EDevice.RenderNearer(0.0003f);
    EDevice.SetRS(D3DRS_CULLMODE, D3DCULL_NONE);
    xr_string temp;
    int cnt = 0;
    for (SDebugDraw::PointIt vit = m_DebugDraw.m_Points.begin(); vit != m_DebugDraw.m_Points.end(); ++vit)
    {
        LPCSTR s = NULL;
        if (vit->i)
        {
            temp.sprintf("P: %d", cnt++);
            s = temp.c_str();
        }

        if (vit->descr.size())
        {
            s = vit->descr.c_str();
        }
        DU_impl.dbgDrawVert(vit->p[0], vit->c, s ? s : "");
    }
    EDevice.SetShader(EDevice.m_SelectionShader);
    cnt = 0;
    for (SDebugDraw::LineIt eit = m_DebugDraw.m_Lines.begin(); eit != m_DebugDraw.m_Lines.end(); eit++)
    {
        if (eit->i)
            temp.sprintf("L: %d", cnt++);
        DU_impl.dbgDrawEdge(eit->p[0], eit->p[1], eit->c, eit->i ? temp.c_str() : "");
    }
    EDevice.SetShader(EDevice.m_SelectionShader);
    cnt = 0;
    for (SDebugDraw::FaceIt fwit = m_DebugDraw.m_WireFaces.begin(); fwit != m_DebugDraw.m_WireFaces.end(); fwit++)
    {
        if (fwit->i)
            temp.sprintf("F: %d", cnt++);
        DU_impl.dbgDrawFace(fwit->p[0], fwit->p[1], fwit->p[2], fwit->c, fwit->i ? temp.c_str() : "");
    }
    cnt = 0;
    if (!m_DebugDraw.m_SolidFaces.empty())
    {
        EDevice.SetShader(EDevice.m_SelectionShader);
        DU_impl.DD_DrawFace_begin(FALSE);
        for (SDebugDraw::FaceIt fsit = m_DebugDraw.m_SolidFaces.begin(); fsit != m_DebugDraw.m_SolidFaces.end(); fsit++)
            DU_impl.DD_DrawFace_push(fsit->p[0], fsit->p[1], fsit->p[2], fsit->c);
        DU_impl.DD_DrawFace_end();
    }
    EDevice.SetShader(EDevice.m_SelectionShader);
    cnt = 0;
    for (SDebugDraw::OBBVecIt oit = m_DebugDraw.m_OBB.begin(); oit != m_DebugDraw.m_OBB.end(); oit++)
    {
        temp.sprintf("OBB: %d", cnt++);
        DU_impl.DrawOBB(Fidentity, *oit, 0x2F00FF00, 0xFF00FF00);
        DU_impl.OutText(oit->m_translate, temp.c_str(), 0xffff0000, 0x0000000);
    }
    EDevice.SetRS(D3DRS_CULLMODE, D3DCULL_CCW);
    EDevice.ResetNearer();

    EDevice.SetRS(D3DRS_CULLMODE, D3DCULL_CCW);
    EDevice.ResetNearer();

    RenderGizmo();
}

float CToolCustom::CalculateGizmoScale(const Fvector &pivot)
{
    float dist = EDevice.m_Camera.GetPosition().distance_to(pivot);
    float scale = dist * 0.12f;
    if (scale < 0.05f) scale = 0.05f;
    return scale;
}

static float RayDistanceToSegment(const Fvector &rayStart, const Fvector &rayDir, const Fvector &p0, const Fvector &p1, Fvector &outClosestOnSegment)
{
    Fvector u = rayDir;
    Fvector v = Fvector().sub(p1, p0);
    Fvector w = Fvector().sub(rayStart, p0);
    float a = u.dotproduct(u);
    float b = u.dotproduct(v);
    float c = v.dotproduct(v);
    float d = u.dotproduct(w);
    float e = v.dotproduct(w);
    float D = a * c - b * b;
    float sc, sN, sD = D;
    float tc, tN, tD = D;

    if (D < EPS_S)
    {
        sN = 0.0f;
        sD = 1.0f;
        tN = e;
        tD = c;
    }
    else
    {
        sN = (b * e - c * d);
        tN = (a * e - b * d);
        if (sN < 0.0f)
        {
            sN = 0.0f;
            tN = e;
            tD = c;
        }
    }

    if (tN < 0.0f)
    {
        tN = 0.0f;
        if (-d < 0.0f)
            sN = 0.0f;
        else if (-d > a)
            sN = sD;
        else
        {
            sN = -d;
            sD = a;
        }
    }
    else if (tN > tD)
    {
        tN = tD;
        if ((-d + b) < 0.0f)
            sN = 0.0f;
        else if ((-d + b) > a)
            sN = sD;
        else
        {
            sN = (-d + b);
            sD = a;
        }
    }

    sc = (abs(sN) < EPS_S ? 0.0f : sN / sD);
    tc = (abs(tN) < EPS_S ? 0.0f : tN / tD);

    Fvector dP = Fvector().sub(Fvector().mad(rayStart, u, sc), Fvector().mad(p0, v, tc));
    outClosestOnSegment.mad(p0, v, tc);
    return dP.magnitude();
}

static bool RayPlaneIntersection(const Fvector &rayStart, const Fvector &rayDir, const Fvector &planePoint, const Fvector &planeNormal, Fvector &outHit)
{
    float denom = planeNormal.dotproduct(rayDir);
    if (abs(denom) > EPS_S)
    {
        float t = planeNormal.dotproduct(Fvector().sub(planePoint, rayStart)) / denom;
        if (t >= 0.0f)
        {
            outHit.mad(rayStart, rayDir, t);
            return true;
        }
    }
    return false;
}

ETAxis CToolCustom::PickGizmo(const Fvector &start, const Fvector &dir)
{
    if (!GetSelectionPosition(m_axis_xform))
        return etAxisUndefined;

    Fvector center = m_axis_xform.c;
    float scale = CalculateGizmoScale(center);

    Fvector axisX = Fvector().set(scale, 0, 0);
    Fvector axisY = Fvector().set(0, scale, 0);
    Fvector axisZ = Fvector().set(0, 0, scale);

    Fvector endX = Fvector().add(center, axisX);
    Fvector endY = Fvector().add(center, axisY);
    Fvector endZ = Fvector().add(center, axisZ);

    float pickRadius = scale * 0.15f;
    float bestDist = 1e9f;
    ETAxis bestAxis = etAxisUndefined;

    Fvector hitCam;
    if (RayPlaneIntersection(start, dir, center, EDevice.m_Camera.GetDirection(), hitCam))
    {
        float d = hitCam.distance_to(center);
        if (d <= pickRadius * 1.5f && d < bestDist)
        {
            bestDist = d;
            bestAxis = etAxisCAM;
        }
    }

    if (m_Action == etaMove || m_Action == etaScale)
    {
        float quadSize = scale * 0.4f;

        Fvector hitZX;
        if (RayPlaneIntersection(start, dir, center, Fvector().set(0, 1, 0), hitZX))
        {
            Fvector local = Fvector().sub(hitZX, center);
            if (local.x >= 0.0f && local.x <= quadSize && local.z >= 0.0f && local.z <= quadSize)
            {
                float d = local.magnitude();
                if (d < bestDist)
                {
                    bestDist = d;
                    bestAxis = etAxisZX;
                }
            }
        }

        Fvector hitXY;
        if (RayPlaneIntersection(start, dir, center, Fvector().set(0, 0, 1), hitXY))
        {
            Fvector local = Fvector().sub(hitXY, center);
            if (local.x >= 0.0f && local.x <= quadSize && local.y >= 0.0f && local.y <= quadSize)
            {
                float d = local.magnitude();
                if (d < bestDist)
                {
                    bestDist = d;
                    bestAxis = etAxisXY;
                }
            }
        }

        Fvector hitYZ;
        if (RayPlaneIntersection(start, dir, center, Fvector().set(1, 0, 0), hitYZ))
        {
            Fvector local = Fvector().sub(hitYZ, center);
            if (local.y >= 0.0f && local.y <= quadSize && local.z >= 0.0f && local.z <= quadSize)
            {
                float d = local.magnitude();
                if (d < bestDist)
                {
                    bestDist = d;
                    bestAxis = etAxisYZ;
                }
            }
        }

        Fvector dummy;
        float dX = RayDistanceToSegment(start, dir, center, endX, dummy);
        if (dX <= pickRadius && dX < bestDist)
        {
            bestDist = dX;
            bestAxis = etAxisX;
        }

        float dY = RayDistanceToSegment(start, dir, center, endY, dummy);
        if (dY <= pickRadius && dY < bestDist)
        {
            bestDist = dY;
            bestAxis = etAxisY;
        }

        float dZ = RayDistanceToSegment(start, dir, center, endZ, dummy);
        if (dZ <= pickRadius && dZ < bestDist)
        {
            bestDist = dZ;
            bestAxis = etAxisZ;
        }
    }
    else if (m_Action == etaRotate)
    {
        float rRadius = scale * 2.0f;
        float ringThick = scale * 0.15f;

        Fvector hitX;
        if (RayPlaneIntersection(start, dir, center, Fvector().set(1, 0, 0), hitX))
        {
            float d = abs(hitX.distance_to(center) - rRadius);
            if (d <= ringThick && d < bestDist)
            {
                bestDist = d;
                bestAxis = etAxisX;
            }
        }

        Fvector hitY;
        if (RayPlaneIntersection(start, dir, center, Fvector().set(0, 1, 0), hitY))
        {
            float d = abs(hitY.distance_to(center) - rRadius);
            if (d <= ringThick && d < bestDist)
            {
                bestDist = d;
                bestAxis = etAxisY;
            }
        }

        Fvector hitZ;
        if (RayPlaneIntersection(start, dir, center, Fvector().set(0, 0, 1), hitZ))
        {
            float d = abs(hitZ.distance_to(center) - rRadius);
            if (d <= ringThick && d < bestDist)
            {
                bestDist = d;
                bestAxis = etAxisZ;
            }
        }

        float screenRadius = rRadius * 1.2f; //   RenderGizmo
        if (RayPlaneIntersection(start, dir, center, EDevice.m_Camera.GetDirection(), hitCam))
        {
            float d = abs(hitCam.distance_to(center) - screenRadius);
            if (d <= ringThick && d < bestDist)
            {
                bestDist = d;
                bestAxis = etAxisCAM;
            }
        }
    }

    return bestAxis;
}

void CToolCustom::RenderGizmo()
{
    if (!GetSelectionPosition(m_axis_xform))
        return;

    Fvector center = m_axis_xform.c;
    m_GizmoScale = CalculateGizmoScale(center);
    float scale = m_GizmoScale;

    EDevice.RenderNearer(0.0001f);

    ETAxis active = (m_bHiddenMode) ? m_Axis : m_HoverAxis;

    u32 clrX = (active == etAxisX) ? 0xFFFFFF00 : 0xFFFF3333;
    u32 clrY = (active == etAxisY) ? 0xFFFFFF00 : 0xFF33FF33;
    u32 clrZ = (active == etAxisZ) ? 0xFFFFFF00 : 0xFF3388FF;
    u32 clrZX = (active == etAxisZX) ? 0xFFFFFF00 : 0x6000FFFF;
    u32 clrXY = (active == etAxisXY) ? 0xFFFFFF00 : 0x60FFFF00;
    u32 clrYZ = (active == etAxisYZ) ? 0xFFFFFF00 : 0x60FF00FF;
    u32 clrCam = (active == etAxisCAM) ? 0xFFFFFF00 : 0x80FFFFFF;

    if (m_Action == etaMove)
    {
        Fvector endX = Fvector().add(center, Fvector().set(scale, 0, 0));
        Fvector endY = Fvector().add(center, Fvector().set(0, scale, 0));
        Fvector endZ = Fvector().add(center, Fvector().set(0, 0, scale));

        DU_impl.DrawLine(center, endX, clrX);
        DU_impl.DrawLine(center, endY, clrY);
        DU_impl.DrawLine(center, endZ, clrZ);

        float coneH = scale * 0.2f;
        float coneR = scale * 0.05f;
        DU_impl.DrawCone(Fidentity, endX, Fvector().set(1, 0, 0), coneH, coneR, clrX, clrX, TRUE, TRUE);
        DU_impl.DrawCone(Fidentity, endY, Fvector().set(0, 1, 0), coneH, coneR, clrY, clrY, TRUE, TRUE);
        DU_impl.DrawCone(Fidentity, endZ, Fvector().set(0, 0, 1), coneH, coneR, clrZ, clrZ, TRUE, TRUE);

        float qSize = scale * 0.45f;
        Fvector pZX_o = Fvector().add(center, Fvector().set(-qSize * 0.5f, 0, -qSize * 0.5f));
        DU_impl.DrawRectangle(pZX_o, Fvector().set(qSize, 0, 0), Fvector().set(0, 0, qSize), clrZX, clrZX, TRUE, TRUE);

        DU_impl.DrawRectangle(center, Fvector().set(qSize * 0.8f, 0, 0), Fvector().set(0, qSize * 0.8f, 0), clrXY, clrXY, TRUE, TRUE);
        DU_impl.DrawRectangle(center, Fvector().set(0, qSize * 0.8f, 0), Fvector().set(0, 0, qSize * 0.8f), clrYZ, clrYZ, TRUE, TRUE);

        //DU_impl.DrawPivot(center, scale * 10.0f);

        if (m_bHiddenMode && m_MovedAmount.square_magnitude() > EPS_S)
        {
            string128 buf;
            DU_impl.OutText(Fvector().add(center, Fvector().set(0, scale * 1.5f, 0)), buf, 0xFFFFFF00, 0xFF000000);
        }
    }
    else if (m_Action == etaRotate)
    {
        float rRadius = scale * 2.0f;

        DU_impl.DrawCircle(center, Fvector().set(1, 0, 0), rRadius, clrX, FALSE, 64);
        DU_impl.DrawCircle(center, Fvector().set(0, 1, 0), rRadius, clrY, FALSE, 64);
        DU_impl.DrawCircle(center, Fvector().set(0, 0, 1), rRadius, clrZ, FALSE, 64);
        DU_impl.DrawCircle(center, EDevice.m_Camera.GetDirection(), rRadius * 1.2f, clrCam, FALSE, 64);

        if (m_bHiddenMode && UI->IsMouseCaptured() && abs(m_RotateAmount) > EPS_S)
        {
            Fvector n = Fvector().set(0, 0, 0);
            if (m_Axis == etAxisX)       n.set(1, 0, 0);
            else if (m_Axis == etAxisY)  n.set(0, 1, 0);
            else if (m_Axis == etAxisZ)  n.set(0, 0, 1);
            else if (m_Axis == etAxisCAM) n.set(EDevice.m_Camera.GetDirection());

            Fvector startDir;
            if (m_Axis == etAxisY)
                startDir.set(1, 0, 0);
            else if (m_Axis == etAxisCAM)
                startDir.set(EDevice.m_Camera.GetRight());
            else
                startDir.set(0, 1, 0);

            float sectorRadius = (m_Axis == etAxisCAM) ? rRadius * 1.2f : rRadius;
            DU_impl.DrawCircleSector(center, n, startDir, m_RotateAmount, sectorRadius, 0x40FFFF00, 0xFFFFFF00, 48);

            string128 buf;
            DU_impl.OutText(Fvector().add(center, Fvector().set(0, scale * 2.3f, 0)), buf, 0xFFFFFF00, 0xFF000000);
        }
    }
    else if (m_Action == etaScale)
    {
        Fvector endX = Fvector().add(center, Fvector().set(scale, 0, 0));
        Fvector endY = Fvector().add(center, Fvector().set(0, scale, 0));
        Fvector endZ = Fvector().add(center, Fvector().set(0, 0, scale));

        DU_impl.DrawLine(center, endX, clrX);
        DU_impl.DrawLine(center, endY, clrY);
        DU_impl.DrawLine(center, endZ, clrZ);

        float boxSz = scale * 0.08f;
        Fvector bSize = Fvector().set(boxSz, boxSz, boxSz);
        DU_impl.DrawBox(endX, bSize, TRUE, TRUE, clrX, clrX);
        DU_impl.DrawBox(endY, bSize, TRUE, TRUE, clrY, clrY);
        DU_impl.DrawBox(endZ, bSize, TRUE, TRUE, clrZ, clrZ);

        float qSize = scale * 0.45f;
        Fvector pZX_o = Fvector().add(center, Fvector().set(-qSize * 0.5f, 0, -qSize * 0.5f));
        DU_impl.DrawRectangle(pZX_o, Fvector().set(qSize, 0, 0), Fvector().set(0, 0, qSize), clrZX, clrZX, TRUE, TRUE);

        DU_impl.DrawRectangle(center, Fvector().set(qSize * 0.8f, 0, 0), Fvector().set(0, qSize * 0.8f, 0), clrXY, clrXY, TRUE, TRUE);
        DU_impl.DrawRectangle(center, Fvector().set(0, qSize * 0.8f, 0), Fvector().set(0, 0, qSize * 0.8f), clrYZ, clrYZ, TRUE, TRUE);

        Fvector uSize = Fvector().set(boxSz * 1.5f, boxSz * 1.5f, boxSz * 1.5f);
        DU_impl.DrawBox(center, uSize, TRUE, TRUE, clrCam, clrCam);

        if (m_bHiddenMode && m_ScaleAmount.square_magnitude() > EPS_S)
        {
            string128 buf;
            DU_impl.OutText(Fvector().add(center, Fvector().set(0, scale * 1.5f, 0)), buf, 0xFFFFFF00, 0xFF000000);
        }
    }

    EDevice.ResetNearer();
}
//------------------------------------------------------------------------------