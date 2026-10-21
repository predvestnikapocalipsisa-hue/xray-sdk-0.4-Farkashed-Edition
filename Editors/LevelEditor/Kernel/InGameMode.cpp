#include "stdafx.h"

// A read-only preview of the editable scene. Collision lives only in memory;
// no level compiler, game executable, temporary map or spawn data is needed.
class CInGameMode
{
public:
    CDB::MODEL collision;
    CDB::COLLIDER query;
    Fvector savedPosition, savedHPB, position, angles, spawn;
    ECameraStyle savedStyle;
    bool savedGrid = false;
    bool grounded = false;
    bool firstFrame = true;
    float verticalSpeed = 0.f;
    POINT savedCursor = {};

    static constexpr float eyeHeight = 1.7f;
    static constexpr float radius = 0.3f;
    static constexpr float stepHeight = 0.35f;

    bool Ray(const Fvector& start, const Fvector& direction, float range,
        float& distance, Fvector* normal = nullptr)
    {
        query.ray_options(CDB::OPT_ONLYNEAREST); // double-sided, independent of render visibility
        query.ray_query(&collision, start, direction, range);
        if (!query.r_count()) return false;
        const CDB::RESULT& hit = *query.r_begin();
        distance = hit.range;
        if (normal) normal->mknormal(hit.verts[0], hit.verts[1], hit.verts[2]);
        return true;
    }

    bool Floor(const Fvector& eye, float above, float below, float& height)
    {
        const Fvector down = {0.f, -1.f, 0.f};
        Fvector start = eye, normal;
        start.y += above - eyeHeight;
        float distance;
        if (!Ray(start, down, above + below, distance, &normal) || _abs(normal.y) < 0.65f)
            return false;
        height = start.y - distance + eyeHeight;
        return true;
    }

    // Sweep several body heights and lateral offsets, then resolve X/Z separately
    // so walls allow sliding. Small fixed steps keep thin walls from being skipped.
    bool CanMove(const Fvector& from, const Fvector& to)
    {
        Fvector direction;
        direction.sub(to, from);
        const float length = direction.magnitude();
        if (length < EPS_S) return true;
        direction.div(length);
        Fvector side = {direction.z, 0.f, -direction.x};
        const float levels[] = {stepHeight + 0.05f, 0.9f, eyeHeight + 0.1f};
        for (float level : levels)
            for (int offset = -1; offset <= 1; ++offset)
            {
                Fvector start;
                start.mad(from, side, offset * radius * 0.85f);
                start.y += level - eyeHeight;
                float distance;
                if (Ray(start, direction, length + radius, distance)) return false;
            }
        return true;
    }

    void Update(float dt, bool jump, bool running, float forward, float strafe)
    {
        if (jump && grounded) { verticalSpeed = 5.5f; grounded = false; }
        Fvector movement;
        movement.set(-sinf(angles.x) * forward + cosf(angles.x) * strafe, 0.f,
            cosf(angles.x) * forward + sinf(angles.x) * strafe);
        if (movement.square_magnitude() > 1.f) movement.normalize();
        movement.mul(running ? 6.f : 3.f);
        // Cap catch-up after stalls; never teleport a controller through geometry.
        dt = _min(_max(dt, 0.f), 0.1f);
        while (dt > EPS_S)
        {
            const float step = _min(dt, 1.f / 120.f);
            dt -= step;
            for (int axis = 0; axis < 2; ++axis)
            {
                Fvector next = position;
                if (axis == 0) next.x += movement.x * step;
                else next.z += movement.z * step;
                if (!CanMove(position, next)) continue;
                float floor;
                if (grounded && Floor(next, stepHeight, stepHeight + 0.08f, floor))
                    next.y = floor;
                position = next;
            }
            verticalSpeed -= 9.81f * step;
            const float fall = verticalSpeed * step;
            if (fall > 0.f)
            {
                const Fvector up = {0.f, 1.f, 0.f};
                Fvector start = position;
                start.y += 0.1f;
                float distance;
                if (Ray(start, up, fall + 0.1f, distance)) verticalSpeed = 0.f;
                else position.y += fall;
                grounded = false;
            }
            else
            {
                float floor;
                if (Floor(position, 0.04f, -fall + 0.08f, floor))
                {
                    position.y = floor;
                    verticalSpeed = 0.f;
                    grounded = true;
                }
                else { position.y += fall; grounded = false; }
            }
            if (position.y < spawn.y - 100.f)
            {
                position = spawn;
                verticalSpeed = 0.f;
                grounded = true;
            }
        }
    }
};

void CLevelMain::StartInGame()
{
    StartOGSRPreview();
}

void CLevelMain::StartLocalInGame()
{
    if (IsInGame() || GetEState() != esEditScene || Scene->locked() ||
        IsMouseCaptured() || IsMouseInUse() || EDevice.m_Camera.IsMoving()) return;

    CDB::Collector geometry;
    ObjectList objects;
    if (Scene->GetTool(OBJCLASS_SCENEOBJECT)->IsVisible())
        Scene->GetQueryObjects(objects, OBJCLASS_SCENEOBJECT, -1, 1, -1);
    if (Scene->GetTool(OBJCLASS_GROUP)->IsVisible())
        Scene->GetQueryObjects(objects, OBJCLASS_GROUP, -1, 1, -1);
    for (CCustomObject* object : objects)
    {
        if (!object->Visible()) continue;
        if (object->FClassID == OBJCLASS_GROUP)
        {
            ObjectList children;
            static_cast<CGroupObject*>(object)->GetObjects(children);
            objects.splice(objects.end(), children);
            continue;
        }
        if (object->FClassID != OBJCLASS_SCENEOBJECT) continue;
        CSceneObject* instance = static_cast<CSceneObject*>(object);
        EditMeshVec* meshes = instance->Meshes();
        if (!meshes) continue;
        for (CEditableMesh* mesh : *meshes)
            for (u32 face = 0; face < mesh->GetFCount(); ++face)
            {
                const Fvector* local[3];
                Fvector world[3], normal;
                mesh->GetFacePT(face, local);
                for (int i = 0; i < 3; ++i) instance->_Transform().transform_tiny(world[i], *local[i]);
                normal.mknormal_non_normalized(world[0], world[1], world[2]);
                if (normal.square_magnitude() > EPS_S * EPS_S)
                    geometry.add_face_D(world[0], world[1], world[2], 0);
            }
    }
    if (geometry.getTS() < 2)
    {
        ELog.DlgMsg(mtInformation, "InGame needs visible scene geometry. Open a location and place the editor camera above the ground.");
        return;
    }

    CInGameMode* mode = xr_new<CInGameMode>();
    mode->collision.build(geometry.getV(), (int)geometry.getVS(), geometry.getT(), (int)geometry.getTS());
    mode->savedPosition = EDevice.m_Camera.GetPosition();
    mode->savedHPB = EDevice.m_Camera.GetHPB();
    mode->savedStyle = EDevice.m_Camera.GetStyle();
    mode->position = mode->savedPosition;
    mode->angles = mode->savedHPB;
    mode->angles.z = 0.f;
    clamp(mode->angles.y, -1.5f, 1.5f);
    float floor;
    if (!mode->Floor(mode->position, CInGameMode::eyeHeight, ZFar(), floor))
    {
        xr_delete(mode);
        ELog.DlgMsg(mtInformation, "No walkable ground below the camera. Move the editor camera above the location and press F5 again.");
        return;
    }
    mode->position.y = floor;
    mode->spawn = mode->position;
    mode->grounded = true;
    GetCursorPos(&mode->savedCursor);
    mode->savedGrid = psDeviceFlags.is(rsDrawGrid);
    psDeviceFlags.set(rsDrawGrid, FALSE);
    EDevice.m_Camera.SetStyle(csFreeFly);
    m_InGame = mode;
    EDevice.m_Camera.Set(mode->angles, mode->position);
    RedrawScene();
}

void CLevelMain::StopInGame()
{
    StopOGSRPreview();
    if (!m_InGame) return;
    CInGameMode* mode = m_InGame;
    m_InGame = nullptr;
    psDeviceFlags.set(rsDrawGrid, mode->savedGrid);
    EDevice.m_Camera.SetStyle(mode->savedStyle);
    EDevice.m_Camera.Set(mode->savedHPB, mode->savedPosition);
    SetCursorPos(mode->savedCursor.x, mode->savedCursor.y);
    xr_delete(mode);
    RedrawScene();
}

void CLevelMain::DrawInGame()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
        (!m_InGame->firstFrame && ImGui::IsKeyPressed(ImGuiKey_F5, false)))
    {
        StopInGame();
        return;
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::Begin("InGame preview", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::SetWindowFocus();
    RTSize.set(_max(viewport->Size.x, 32.f), _max(viewport->Size.y, 32.f));
    if (RT && RT->pSurface) ImGui::Image(RT->pSurface, viewport->Size);
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    CInGameMode& mode = *m_InGame;
    const POINT center = {(LONG)(viewport->Pos.x + viewport->Size.x * 0.5f),
        (LONG)(viewport->Pos.y + viewport->Size.y * 0.5f)};
    POINT mouse;
    GetCursorPos(&mouse);
    if (!mode.firstFrame)
    {
        mode.angles.x -= (mouse.x - center.x) * 0.002f;
        mode.angles.y -= (mouse.y - center.y) * 0.002f;
        clamp(mode.angles.y, -1.5f, 1.5f);
        const float forward = float(ImGui::IsKeyDown(ImGuiKey_W)) - float(ImGui::IsKeyDown(ImGuiKey_S));
        const float strafe = float(ImGui::IsKeyDown(ImGuiKey_D)) - float(ImGui::IsKeyDown(ImGuiKey_A));
        mode.Update(io.DeltaTime, ImGui::IsKeyPressed(ImGuiKey_Space, false), io.KeyShift, forward, strafe);
        if (ImGui::IsKeyPressed(ImGuiKey_R, false))
        {
            mode.position = mode.spawn;
            mode.verticalSpeed = 0.f;
            mode.grounded = true;
        }
    }
    mode.firstFrame = false;
    SetCursorPos(center.x, center.y);
    EDevice.m_Camera.Set(mode.angles, mode.position);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(viewport->Pos.x + 12, viewport->Pos.y + 12),
        ImVec2(viewport->Pos.x + 670, viewport->Pos.y + 48), IM_COL32(0, 0, 0, 180), 5.f);
    draw->AddText(ImVec2(viewport->Pos.x + 22, viewport->Pos.y + 22), IM_COL32_WHITE,
        "InGame | WASD: move | Shift: run | Space: jump | R: respawn | Esc/F5: exit");
    ImGui::End();
    ImGui::PopStyleVar(2);
    RedrawScene();
}
