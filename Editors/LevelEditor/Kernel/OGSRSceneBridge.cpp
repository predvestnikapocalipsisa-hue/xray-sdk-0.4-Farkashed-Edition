#include "stdafx.h"
#include "../../../SDKRuntime/InGame/SharedScene.h"
#include "OGSRPreviewGeometry.h"
#include "Tools/Details/ESceneDOTools.h"
#include "Tools/AIMap/ESceneAIMapTools.h"
#include "Entry/Spawn/SpawnPoint.h"
#include "Entry/Shape/EShape.h"
#include "Entry/WayPoint/WayPoint.h"
#include <memory>
#include "../../../SDKRuntime/InGame/NavigationPreparation.h"
#include "../../XrEUI/SDKMessageBox.h"

bool& CLevelMain::AutoRepairOGSRNavigation() { static bool enabled = false; return enabled; }

namespace
{
    struct PreviewSession
    {
        SDKInGame::SharedScene mapping;
        HANDLE process = nullptr;
        HANDLE ready = nullptr;
        HWND window = nullptr;
        HWND parent = nullptr;
        ~PreviewSession() { if (process) CloseHandle(process); if (ready) CloseHandle(ready); }
    };
    std::unique_ptr<PreviewSession> preview;

    std::wstring FindBridgeHost()
    {
        wchar_t module[MAX_PATH]{};
        GetModuleFileNameW(nullptr, module, MAX_PATH);
        std::wstring directory(module);
        for (unsigned i = 0; i < 5; ++i)
        {
            const size_t separator = directory.find_last_of(L"\\/");
            if (separator == std::wstring::npos) break;
            directory.resize(separator);
            const std::wstring candidate = directory + L"\\InGame\\bin\\SDKSceneHost.exe";
            if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) return candidate;
        }
        string_path root;
        FS.update_path(root, "$sdk_root$", "InGame\\bin\\SDKSceneHost.exe");
        const int length = MultiByteToWideChar(CP_ACP, 0, root, -1, nullptr, 0);
        std::vector<wchar_t> wide(length);
        MultiByteToWideChar(CP_ACP, 0, root, -1, wide.data(), length);
        return GetFileAttributesW(wide.data()) != INVALID_FILE_ATTRIBUTES ? wide.data() : std::wstring();
    }

    struct CaptureCancelled {};

    std::vector<uint8_t> CaptureScene(xr_string& navigationReport)
    {
        SDKInGame::SceneBuilder builder;
        const Fvector& position = EDevice.m_Camera.GetPosition();
        const Fvector& angles = EDevice.m_Camera.GetHPB();
        builder.info.camera = {position.x, position.y, position.z};
        builder.info.angles = {angles.x, angles.y, angles.z};
        string_path textures;
        FS.update_path(textures, "$game_textures$", "");
        const xr_string utf8 = XrUIManager::ConvertCP1251ToUTF8(textures);
        if (utf8.size() >= sizeof(builder.info.textureRoot)) throw std::runtime_error("SDK texture path is too long");
        std::memcpy(builder.info.textureRoot, utf8.c_str(), utf8.size() + 1);
        string_path dataRoot;
        FS.update_path(dataRoot, "$game_data$", "");
        const xr_string dataUTF8 = XrUIManager::ConvertCP1251ToUTF8(dataRoot);
        if (dataUTF8.size() >= sizeof(builder.info.sdkDataRoot)) throw std::runtime_error("SDK resource path is too long");
        std::memcpy(builder.info.sdkDataRoot, dataUTF8.c_str(), dataUTF8.size()+1);
        ObjectList objects;
        SExportStreams routes;
        if (Scene->GetTool(OBJCLASS_SCENEOBJECT)->IsVisible())
            Scene->GetQueryObjects(objects, OBJCLASS_SCENEOBJECT, -1, 1, -1);
        if (Scene->GetTool(OBJCLASS_GROUP)->IsVisible())
            Scene->GetQueryObjects(objects, OBJCLASS_GROUP, -1, 1, -1);
        if (Scene->GetTool(OBJCLASS_SPAWNPOINT)->IsVisible())
            Scene->GetQueryObjects(objects, OBJCLASS_SPAWNPOINT, -1, 1, -1);
        // Way visibility is an editor display preference; scripted Spawn
        // objects still need their routes when that overlay is hidden.
        Scene->GetQueryObjects(objects, OBJCLASS_WAY, -1, 1, -1);
        xr_set<CCustomObject*> visited;
        xr_vector<CSceneObject*> sceneObjects;
        for (CCustomObject* object : objects)
        {
            if (!object->Visible() || !visited.insert(object).second) continue;
            if (object->FClassID == OBJCLASS_GROUP)
            {
                ObjectList children;
                static_cast<CGroupObject*>(object)->GetObjects(children);
                objects.splice(objects.end(), children);
                continue;
            }
            if (object->FClassID == OBJCLASS_SPAWNPOINT && Scene->GetTool(OBJCLASS_SPAWNPOINT)->IsVisible())
            {
                auto* point = static_cast<CSpawnPoint*>(object);
                if (!point->m_SpawnData.Valid()) continue; // multiplayer/environment editor helpers
                auto* source = point->m_SpawnData.m_Data;
                struct EntityCopy
                {
                    ISE_Abstract* entity;
                    ~EntityCopy() { XrSE_Factory::destroy_entity(entity); }
                } copy{XrSE_Factory::create_entity(source->name())};
                if (!copy.entity) throw std::runtime_error(std::string("Cannot prepare spawn: ") + object->GetName());
                NET_Packet original;
                source->Spawn_Write(original, TRUE);
                if (!copy.entity->Spawn_Read(original)) throw std::runtime_error("Cannot clone SDK spawn data");
                copy.entity->position() = object->GetPosition();
                copy.entity->angle() = object->GetRotation();
                copy.entity->set_name_replace(object->GetName());
                if (auto* targetShape = copy.entity->shape())
                {
                    auto* attached = dynamic_cast<CEditShape*>(point->m_AttachedObject);
                    if (!attached || attached->GetShapes().empty())
                        throw std::runtime_error(std::string("Spawn needs an attached shape: ") + object->GetName());
                    auto shapes = attached->GetShapes();
                    Fmatrix scale; scale.scale(attached->GetScale());
                    for (auto& shape : shapes)
                        if (shape.type == CShapeData::cfSphere)
                        { scale.transform_tiny(shape.data.sphere.P); shape.data.sphere.R *= attached->GetScale().x; }
                        else shape.data.box.mulA_43(scale);
                    targetShape->assign_shapes(shapes.data(), u32(shapes.size()));
                }
                SDKInGame::Spawn spawn{};
                spawn.format = Core.SocSdk ? 0 : 1;
                const auto& pos = object->GetPosition(); const auto& rot = object->GetRotation();
                spawn.position = {pos.x, pos.y, pos.z}; spawn.angles = {rot.x, rot.y, rot.z};
                if (xr_strlen(source->name()) >= sizeof(spawn.section) || xr_strlen(object->GetName()) >= sizeof(spawn.name))
                    throw std::runtime_error("Spawn section/name is too long");
                strcpy_s(spawn.section, source->name()); strcpy_s(spawn.name, object->GetName());
                NET_Packet packet; copy.entity->Spawn_Write(packet, TRUE);
                builder.AddSpawn(spawn, packet.B.data, packet.B.count);
                continue;
            }
            if (object->FClassID == OBJCLASS_WAY)
            {
                auto* route = static_cast<CWayObject*>(object);
                if (route->GetWayPoints().empty()) continue;
                if (!route->ExportGame(&routes)) throw std::runtime_error("Cannot export InGame Way route");
                continue;
            }
            if (object->FClassID != OBJCLASS_SCENEOBJECT) continue;
            CSceneObject* instance = static_cast<CSceneObject*>(object);
            CEditableObject* reference = instance->GetReference();
            if (!reference) continue;
            sceneObjects.push_back(instance);
        }
        xr_vector<Fvector> shadowVertices;
        xr_vector<CDB::TRI> shadowTriangles;
        for (auto* instance : sceneObjects)
            for (auto* mesh : instance->GetReference()->Meshes())
                for (const auto& entry : mesh->GetSurfFaces())
                {
                    auto* surface = entry.first;
                    const auto policy = SDKPreviewExport::Policy(*surface);
                    if (!policy.collision && !(policy.visible && policy.shadow)) continue;
                    auto* material = GMLib.GetMaterialByID(surface->_GameMtl());
                    if (!material) throw std::runtime_error(std::string("Undefined game material: ")+surface->_GameMtlName());
                    const u32 materialIndex = GMLib.GetMaterialIdx(surface->_GameMtlName());
                    if (materialIndex >= 0x4000) throw std::runtime_error("Too many SDK collision materials");
                    const u32 flags = materialIndex | (material->Flags.test(SGameMtl::flSuppressShadows) ? 0x4000u : 0u) |
                        (material->Flags.test(SGameMtl::flSuppressWallmarks) ? 0x8000u : 0u);
                    for (u32 face : entry.second)
                    {
                        const Fvector* points[3]; mesh->GetFacePT(face,points);
                        Fvector world[3], normal;
                        for (unsigned i=0;i<3;++i) instance->_Transform().transform_tiny(world[i],*points[i]);
                        if (SDKPreviewExport::Mirrored(instance->_Transform())) std::swap(world[1],world[2]);
                        normal.mknormal_non_normalized(world[0],world[1],world[2]);
                        if (normal.square_magnitude() <= EPS_S*EPS_S) continue;
                        if (policy.collision)
                        {
                            const u32 first = u32(builder.vertices.size());
                            for (const auto& v : world) builder.vertices.push_back({v.x,v.y,v.z});
                            builder.triangles.push_back({{first,first+1,first+2},flags});
                        }
                        if (policy.visible && policy.shadow && !material->Flags.test(SGameMtl::flSuppressShadows))
                        {
                            CDB::TRI triangle{};
                            for (unsigned i=0;i<3;++i) triangle.verts[i]=u32(shadowVertices.size())+i;
                            for (const auto& v : world) shadowVertices.push_back(v);
                            shadowTriangles.push_back(triangle);
                        }
                    }
                }
        SDKPreviewExport::Lighting lighting;
        lighting.Build(shadowVertices,shadowTriangles);
        for (auto* instance : sceneObjects)
        {
            if (UI->NeedAbort()) throw CaptureCancelled{};
            CMemoryWriter ogf;
            if (!SDKPreviewExport::Export(*instance,ogf,lighting)) continue;
            SDKInGame::Instance visual{};
            Fmatrix identity; identity.identity();
            std::memcpy(visual.transform,&identity,sizeof(identity));
            visual.mesh=builder.AddMesh(ogf.pointer(),ogf.size());
            builder.instances.push_back(visual);
        }
        Msg("OGSR static lighting: %zu cached sky visibility samples",lighting.Samples());
        if (builder.triangles.size() < 2)
            throw std::runtime_error("InGame needs at least two collision faces. Keep ground collision enabled.");
        auto* aiMap = static_cast<ESceneAIMapTool*>(Scene->GetTool(OBJCLASS_AIMAP));
        if (!builder.spawns.empty() && aiMap && !aiMap->Nodes().empty())
        {
            builder.info.navigationCell = aiMap->NavigationCellSize();
            xr_map<SAINode*, uint32_t> indices;
            for (auto* node : aiMap->Nodes()) indices.emplace(node, uint32_t(indices.size()));
            for (auto* node : aiMap->Nodes())
            {
                SDKInGame::NavigationNode n{};
                n.position = {node->Pos.x, node->Pos.y, node->Pos.z};
                n.normal = {node->Plane.n.x, node->Plane.n.y, node->Plane.n.z};
                for (unsigned i = 0; i < 4; ++i)
                { auto found = indices.find(node->n[i]); n.links[i] = found == indices.end() ? UINT32_MAX : found->second; }
                builder.navigation.push_back(n);
            }
        }
        auto navigation = SDKInGame::PrepareNavigation(builder.navigation);
        if (navigation.HasIssues())
        {
            navigationReport = navigation.Report().c_str();
            Msg("! OGSR navigation: %s", navigationReport.c_str());
            if (navigation.nodes.empty())
                throw std::runtime_error(std::string(navigationReport.c_str()) + "\nNo usable navigation nodes remain. Repair or regenerate the AI Map in the SDK.");
            if (!CLevelMain::AutoRepairOGSRNavigation())
            {
                xr_string prompt = navigationReport;
                prompt += "\nApply this repair to the exported snapshot and continue?";
                const char* captions[5] = { "Fix snapshot", "Cancel", nullptr, nullptr, nullptr };
                if (SDKDialogs::Show(nullptr, prompt.c_str(), "OGSR navigation report", MB_YESNO | MB_ICONWARNING, captions) != IDYES)
                    throw CaptureCancelled{};
            }
            builder.navigation = std::move(navigation.nodes);
        }
        if (routes.patrolpath.chunk)
        {
            CMemoryWriter paths;
            paths.w_chunk(WAY_PATROLPATH_CHUNK, routes.patrolpath.stream.pointer(), routes.patrolpath.stream.size());
            builder.patrols.assign(paths.pointer(), paths.pointer()+paths.size());
        }
        Msg("OGSR snapshot: %zu spawn objects, %zu navigation nodes, %d Way routes", builder.spawns.size(), builder.navigation.size(), routes.patrolpath.chunk);
        auto* details = static_cast<EDetailManager*>(Scene->GetTool(OBJCLASS_DO));
        if (details && details->IsVisible() && details->dtSlots && !details->objects.empty())
        {
            CMemoryWriter grass;
            DetailHeader header = details->dtH;
            header.version = DETAIL_VERSION;
            header.object_count = details->objects.size();
            grass.w_chunk(0, &header, sizeof(header));
            grass.open_chunk(1);
            for (u32 i = 0; i < header.object_count; ++i)
            {
                auto* model = static_cast<EDetail*>(details->objects[i]);
                model->DefferedLoad();
                if (!model->number_vertices || !model->number_indices)
                    throw std::runtime_error(std::string("Cannot prepare grass model: ") + model->GetName());
                grass.open_chunk(i);
                model->Export(grass, model->GetTextureName(), Fvector2{0.f, 0.f}, Fvector2{1.f, 1.f}, false);
                grass.close_chunk();
            }
            grass.close_chunk();
            // No offline lighting pass is needed: preview foliage receives
            // daylight rather than the zeroed lighting of unbuilt detail slots.
            const size_t count = size_t(header.size_x) * header.size_z;
            std::vector<DetailSlot> slots(details->dtSlots, details->dtSlots + count);
            for (size_t i=0;i<count;++i)
            {
                auto& slot=slots[i];
                Fvector position{(int(i%header.size_x)-header.offs_x+.5f)*DETAIL_SLOT_SIZE,slot.r_ybase()+.3f,(int(i/header.size_x)-header.offs_z+.5f)*DETAIL_SLOT_SIZE};
                const auto sample=lighting.Sample(position,Fvector{0,1,0});
                slot.c_hemi=u32(_min(15.f,sample.hemi*15.f)); slot.c_dir=u32(sample.sun*15.f);
            }
            grass.w_chunk(2, slots.data(), slots.size() * sizeof(DetailSlot));
            builder.details.assign(grass.pointer(), grass.pointer() + grass.size());
            Msg("OGSR preview: %u grass models, %u x %u detail slots", header.object_count, header.size_x, header.size_z);
        }
        return builder.Finish();
    }
}

void PollOGSRPreview()
{
    if (preview && WaitForSingleObject(preview->process, 0) == WAIT_OBJECT_0)
    {
        DWORD code = 0;
        GetExitCodeProcess(preview->process, &code);
        preview.reset();
        Msg("OGSR InGame finished (exit code %lu).", code);
    }
}

bool DrawOGSRPreviewViewport()
{
    if (!preview) return false;
    const bool visible = ImGui::Begin("Render", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (visible && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
    {
        ImGui::TextUnformatted("OGSR InGame | I: inventory | Shift+F10: ImGui | ~: console | F5: return");
        const ImVec2 position = ImGui::GetCursorScreenPos();
        ImVec2 size = ImGui::GetContentRegionAvail();
        size.x = _max(32.f, size.x); size.y = _max(32.f, size.y);
        ImGui::Dummy(size);
        POINT origin{LONG(position.x), LONG(position.y)};
        ScreenToClient(preview->parent, &origin);
        SetWindowPos(preview->window, HWND_TOP, origin.x, origin.y, int(size.x), int(size.y),
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    else ShowWindow(preview->window, SW_HIDE);
    ImGui::End();
    return true;
}

void CLevelMain::StopOGSRPreview()
{
    if (!preview) return;
    if (WaitForSingleObject(preview->process, 0) == WAIT_TIMEOUT)
    {
        if (preview->window) PostMessage(preview->window, WM_CLOSE, 0, 0);
        const DWORD pid = GetProcessId(preview->process);
        if (!preview->window) EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
            DWORD owner = 0;
            GetWindowThreadProcessId(window, &owner);
            if (owner == DWORD(parameter)) PostMessage(window, WM_CLOSE, 0, 0);
            return TRUE;
        }, LPARAM(pid));
        if (WaitForSingleObject(preview->process, 3000) == WAIT_TIMEOUT)
        {
            TerminateProcess(preview->process, 124);
            WaitForSingleObject(preview->process, 3000);
        }
    }
    preview.reset();
}

void CLevelMain::StartOGSRPreview()
{
    if (preview)
    {
        if (WaitForSingleObject(preview->process, 0) == WAIT_TIMEOUT) { StopOGSRPreview(); return; }
        preview.reset();
    }
    if (IsInGame() || GetEState() != esEditScene || Scene->locked() || IsMouseCaptured() || IsMouseInUse()) return;
    const HWND parent = GetActiveWindow();
    SetWindowLongPtr(parent, GWL_STYLE, GetWindowLongPtr(parent, GWL_STYLE) | WS_CLIPCHILDREN);
    std::wstring host = FindBridgeHost();
    if (host.empty()) { ELog.DlgMsg(mtError, "OGSR InGame is missing. Run Build-InGame.ps1 -SDKRoot <SDK folder>."); return; }
    const std::wstring bin = host.substr(0, host.find_last_of(L"\\/"));
    const std::wstring runtime = bin.substr(0, bin.find_last_of(L"\\/"));
    const std::wstring engine = bin + L"\\xrEngine.exe";
    if (GetFileAttributesW(engine.c_str()) == INVALID_FILE_ATTRIBUTES)
    { ELog.DlgMsg(mtError, "OGSR xrEngine.exe is missing from InGame/bin."); return; }
    SPBItem* progress = ProgressStart(2, "Preparing OGSR InGame...");
    struct ProgressScope { SPBItem*& item; ~ProgressScope() { UI->ProgressEnd(item); } } scope{progress};
    try
    {
        auto session = std::make_unique<PreviewSession>();
        session->parent = parent;
        xr_string navigationReport;
        const auto bytes = CaptureScene(navigationReport);
        const std::wstring name = L"Local\\XRaySDK.Preview." + std::to_wstring(GetCurrentProcessId()) +
            L"." + std::to_wstring(GetTickCount64());
        if (!session->mapping.Create(name, bytes)) throw std::runtime_error("Cannot publish the current scene");
        session->ready = CreateEventW(nullptr, TRUE, FALSE, (name + L".Ready").c_str());
        if (!session->ready) throw std::runtime_error("Cannot create OGSR startup event");
        progress->Update(1);
        std::wstring command = L"\"" + engine + L"\" -use-work-dir -fsltx fsgame.runtime.ltx -ltx sdk-preview.ltx -sdk-preview " + name +
            L" -sdk-preview-parent " + std::to_wstring(uintptr_t(parent)) +
            L" -sdk-play -start server(all/single/alife/new) client(localhost)";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(engine.c_str(), &command[0], nullptr, nullptr, FALSE, 0, nullptr, runtime.c_str(), &startup, &process))
            throw std::runtime_error("Cannot start OGSR InGame");
        CloseHandle(process.hThread);
        session->process = process.hProcess;
        const HANDLE waitHandles[] = {session->ready, session->process};
        const ULONGLONG deadline = GetTickCount64() + 180000;
        DWORD wait;
        while ((wait = WaitForMultipleObjects(2, waitHandles, FALSE, 33)) == WAIT_TIMEOUT && GetTickCount64() < deadline)
            ProgressDraw();
        if (wait != WAIT_OBJECT_0)
        {
            if (wait != WAIT_OBJECT_0 + 1)
            {
                TerminateProcess(session->process, 124);
                WaitForSingleObject(session->process, 5000);
            }
            throw std::runtime_error("OGSR could not load the scene. See InGame/_appdata_/logs for details.");
        }
        EnumChildWindows(parent, [](HWND window, LPARAM parameter) -> BOOL {
            auto* session = reinterpret_cast<PreviewSession*>(parameter);
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            if (pid == GetProcessId(session->process)) { session->window = window; return FALSE; }
            return TRUE;
        }, LPARAM(session.get()));
        if (!session->window) {
            TerminateProcess(session->process, 125);
            WaitForSingleObject(session->process, 5000);
            throw std::runtime_error("OGSR did not create the SDK viewport");
        }
        preview = std::move(session);
        progress->Update(2);
        Msg("OGSR InGame started. I: inventory, 1/2/3: weapons, R: reload, F5: return to SDK. Shift+F10: ImGui, ~: console.");
    }
    catch (const CaptureCancelled&) { Msg("OGSR snapshot cancelled by user."); }
    catch (const std::exception& error) { ELog.DlgMsg(mtError, "OGSR InGame: %s", error.what()); }
}

void CLevelMain::ValidateOGSRBridge()
{
    if (IsInGame() || GetEState() != esEditScene || Scene->locked()) return;
    const std::wstring host = FindBridgeHost();
    if (host.empty())
    {
        ELog.DlgMsg(mtInformation, "Build the OGSR bridge first: Build-InGame.ps1 -BridgeOnly");
        return;
    }
    SPBItem* progress = ProgressStart(2, "Checking OGSR scene bridge...");
    struct ProgressScope { SPBItem*& item; ~ProgressScope() { UI->ProgressEnd(item); } } scope{progress};
    try
    {
        xr_string navigationReport;
        const auto bytes = CaptureScene(navigationReport);
        SDKInGame::SceneView check;
        const char* error;
        if (!check.Open(bytes.data(), bytes.size(), error)) throw std::runtime_error(error);
        progress->Update(1);
        const std::wstring name = L"Local\\XRaySDK.Scene." + std::to_wstring(GetCurrentProcessId()) +
            L"." + std::to_wstring(GetTickCount64());
        SDKInGame::SharedScene mapping;
        if (!mapping.Create(name, bytes)) throw std::runtime_error("Cannot publish the scene mapping");
        std::wstring command = L"\"" + host + L"\" --mapping \"" + name + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(host.c_str(), &command[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &process)) throw std::runtime_error("Cannot start the OGSR bridge host");
        CloseHandle(process.hThread);
        const ULONGLONG deadline = GetTickCount64() + 120000;
        DWORD wait;
        while ((wait = WaitForSingleObject(process.hProcess, 33)) == WAIT_TIMEOUT && GetTickCount64() < deadline)
            ProgressDraw();
        DWORD code = 1;
        if (wait == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &code);
        else { TerminateProcess(process.hProcess, 124); WaitForSingleObject(process.hProcess, 5000); }
        CloseHandle(process.hProcess);
        if (code != 0) throw std::runtime_error("OGSR rejected the scene or the bridge check timed out");
        progress->Update(2);
        xr_string report = "OGSR validated the collision snapshot from the SDK scene.\nPress F5 to start the scene preview.\n";
        if (!navigationReport.empty()) report += "\nApplied export-only repair:\n" + navigationReport;
        ELog.DlgMsg(mtInformation, "%s", report.c_str());
    }
    catch (const CaptureCancelled&) { Msg("OGSR snapshot cancelled by user."); }
    catch (const std::exception& error) { ELog.DlgMsg(mtError, "OGSR scene bridge: %s", error.what()); }
}

namespace { bool showOGSRSceneOptions=false; }
void CLevelMain::OpenOGSRSceneOptions() { showOGSRSceneOptions=true; }
void CLevelMain::DrawOGSRSceneOptions()
{
    if (!showOGSRSceneOptions) return;
    ImGui::SetNextWindowSize(ImVec2(820,560),ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("InGame materials and lighting",&showOGSRSceneOptions)) { ImGui::End(); return; }
    auto& settings=SDKPreviewExport::Settings();
    ImGui::TextWrapped("Applies when starting or validating InGame. Source scene is unchanged. Options last for this editor session.");
    ImGui::Checkbox("Hide technical surfaces by default",&settings.hideTechnical);
    ImGui::Checkbox("Disable technical surface collision by default",&settings.disableTechnicalCollision);
    ImGui::Checkbox("Generate quick static sky lighting",&settings.bakeLighting);
    ImGui::SliderFloat("Lighting sample spacing (m)",&settings.cellSize,.25f,4.f,"%.2f");
    ImGui::SliderFloat("Outdoor ambient cap",&settings.ambientCap,.1f,1.f,"%.2f");
    ImGui::TextWrapped("Preview vertex sky occlusion and sun visibility, not full xrLC lightmaps. Recomputed automatically on each start. Grass slots use the same bake.");
    if (ImGui::Button("Reset surface overrides")) settings.overrides.clear();
    ImGui::Separator();
    ObjectList objects;
    Scene->GetQueryObjects(objects,OBJCLASS_SCENEOBJECT,-1,-1,-1);
    Scene->GetQueryObjects(objects,OBJCLASS_GROUP,-1,-1,-1);
    xr_set<CCustomObject*> visited;
    std::map<xr_string,CSurface*> surfaces;
    for (auto* object : objects)
    {
        if (!visited.insert(object).second) continue;
        if (object->FClassID==OBJCLASS_GROUP)
        {
            ObjectList children; static_cast<CGroupObject*>(object)->GetObjects(children);
            objects.splice(objects.end(),children); continue;
        }
        if (object->FClassID!=OBJCLASS_SCENEOBJECT) continue;
        auto* reference=static_cast<CSceneObject*>(object)->GetReference();
        if (!reference) continue;
        for (auto* mesh : reference->Meshes())
            for (const auto& entry : mesh->GetSurfFaces()) surfaces.emplace(SDKPreviewExport::Key(*entry.first),entry.first);
    }
    static ImGuiTextFilter filter;
    filter.Draw("Filter surfaces");
    if (ImGui::BeginChild("Surface policies",ImVec2(0,0),true))
        for (const auto& entry : surfaces)
        {
            if (!filter.PassFilter(entry.first.c_str())) continue;
            ImGui::PushID(entry.first.c_str());
            auto policy=SDKPreviewExport::Policy(*entry.second);
            bool changed=ImGui::Checkbox("Render",&policy.visible);
            ImGui::SameLine(); changed=ImGui::Checkbox("Collision",&policy.collision)||changed;
            ImGui::SameLine();
            ImGui::TextUnformatted(SDKPreviewExport::Technical(*entry.second) ? "Technical" : "Surface");
            ImGui::TextWrapped("%s",entry.first.c_str());
            if (changed) settings.overrides[entry.first]=policy;
            ImGui::Separator(); ImGui::PopID();
        }
    ImGui::EndChild(); ImGui::End();
}
