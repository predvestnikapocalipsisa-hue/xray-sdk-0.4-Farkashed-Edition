#include "stdafx.h"
#include "UILevelRecovery.h"
#include "LevelRecovery.h"
#include "NativeRecoveryIO.h"
#include "../../../XrCore/lzhuf.h"
#include "../UI/UISceneTabBar.h"
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <set>
#include <shobjidl.h>
#pragma comment(lib, "ole32.lib")

namespace UILevelRecovery
{
namespace
{
bool open = false;
char source[2048] = "";
char destination[2048] = "";
std::string status;
std::string treeExport;
std::string sceneExport;
std::shared_ptr<LevelRecovery::StaticLibrary> staticLibrary;
struct Result
{
    std::shared_ptr<LevelRecovery::Level> level;
    std::string message;
    std::string treeExport;
    std::string sceneExport;
    std::shared_ptr<LevelRecovery::StaticLibrary> staticLibrary;
};
std::future<Result> job;
std::shared_ptr<LevelRecovery::Level> level;
struct CompiledScene
{
    std::string name;
    std::vector<LevelRecovery::NativeIO::Instance> objects;
    std::array<float, 3> cameraTarget{};
    size_t missingTextures = 0;
    LevelRecovery::Gameplay gameplay;
};
bool Busy() { return job.valid(); }

std::vector<uint8_t> DecodeArchiveIndex(std::vector<uint8_t>& encoded)
{
    u8* bytes = nullptr;
    unsigned size = 0;
    if (!_decompressLZBounded(&bytes, &size, encoded.data(), static_cast<unsigned>(encoded.size()), 32 * 1024 * 1024))
        return {};
    const auto freeBytes = [](u8* pointer) { xr_free(pointer); };
    std::unique_ptr<u8, decltype(freeBytes)> buffer(bytes, freeBytes);
    return {bytes, bytes + size};
}

struct CompiledLoadProgress
{
    SPBItem* item = UI->ProgressStart(7.f, "Loading compiled location...");
    ~CompiledLoadProgress() { UI->ProgressEnd(item); }

    void Stage(float completed, const char* text)
    {
        item->progress = completed;
        item->Info(text);
    }
};

std::string ConvertPath(const std::string& text, UINT from, UINT to)
{
    const int n=MultiByteToWideChar(from,from==CP_UTF8 ? MB_ERR_INVALID_CHARS : 0,text.data(),int(text.size()),nullptr,0);
    if (!n) throw std::runtime_error("Invalid path encoding");
    std::wstring wide(n,L'\0');
    MultiByteToWideChar(from,from==CP_UTF8 ? MB_ERR_INVALID_CHARS : 0,text.data(),int(text.size()),&wide[0],n);
    BOOL substituted=FALSE;
    const bool ansi=to==CP_ACP && GetACP()!=CP_UTF8;
    const DWORD flags=ansi ? WC_NO_BEST_FIT_CHARS : 0;
    const int size=WideCharToMultiByte(to,flags,wide.data(),n,nullptr,0,nullptr,ansi ? &substituted : nullptr);
    if (!size || substituted) throw std::runtime_error("SDK cannot represent this path in its current Windows code page");
    std::string result(size,'\0');
    WideCharToMultiByte(to,flags,wide.data(),n,&result[0],size,nullptr,ansi ? &substituted : nullptr);
    if (substituted) throw std::runtime_error("SDK cannot represent this path in its current Windows code page");
    return result;
}

std::string FolderPathUtf8(const std::wstring& path)
{
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(),
        int(path.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("Invalid level folder path");
    std::string result(size, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), int(path.size()),
        &result[0], size, nullptr, nullptr)) throw std::runtime_error("Invalid level folder path");
    return result;
}

std::wstring PickLevelFolder()
{
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        throw std::runtime_error("Could not open the level folder picker");
    dialog->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Select the folder containing level and level.geom");
    std::wstring path;
    if (SUCCEEDED(dialog->Show(EDevice.m_hWnd)))
    {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)))
        {
            PWSTR value = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &value)))
            {
                path = value;
                CoTaskMemFree(value);
            }
            item->Release();
        }
    }
    dialog->Release();
    return path;
}

struct DeferredBuffers
{
    u32 flags = EPrefs->object_flags.get();
    DeferredBuffers()
    {
        EPrefs->object_flags.set(epoDeffLoadRB, TRUE);
        EPrefs->object_flags.set(epoDeffLoadCF, TRUE);
    }
    ~DeferredBuffers() { EPrefs->object_flags.assign(flags); }
};

void OpenRecoveredScene(const CompiledScene& prepared, CompiledLoadProgress& progress)
{
    if (Scene->locked()) throw std::runtime_error("Wait until the editor finishes working on the current scene");
    auto* tool = Scene->GetOTool(OBJCLASS_SCENEOBJECT);
    if (!tool || !tool->IsEditable()) throw std::runtime_error("Enable editing of static objects first");
    FS.get_path(_objects_)->rescan_path_cb();
    FS.get_path("$game_textures$")->rescan_path_cb();
    struct DeleteObject { void operator()(CCustomObject* object) const { xr_delete(object); } };
    std::vector<std::unique_ptr<CCustomObject, DeleteObject>> objects;
    objects.reserve(prepared.objects.size());
    progress.Stage(4.f, "Loading recovered objects");
    ULONGLONG nextUpdate = GetTickCount64() + 33;
    {
        // Resolve each model before clearing the current scene, and keep loaded references alive.
        DeferredBuffers deferred;
        for (const auto& instance : prepared.objects)
        {
            std::unique_ptr<CCustomObject, DeleteObject> object(tool->CreateObject(nullptr, instance.name.c_str()));
            auto* sceneObject = static_cast<CSceneObject*>(object.get());
            if (!sceneObject->SetReference(instance.reference.c_str()))
                throw std::runtime_error("Could not load recovered object " + instance.reference);
            const auto& placement = instance.placement;
            Fvector position, rotation, scale;
            position.set(placement.position[0], placement.position[1], placement.position[2]);
            rotation.set(placement.rotation[0], placement.rotation[1], placement.rotation[2]);
            scale.set(placement.scale[0], placement.scale[1], placement.scale[2]);
            sceneObject->SetPosition(position);
            sceneObject->SetRotation(rotation);
            sceneObject->SetScale(scale);
            objects.push_back(std::move(object));
            const auto now = GetTickCount64();
            if (now >= nextUpdate)
            {
                nextUpdate = now + 33;
                progress.item->Update(4.f + float(objects.size()) / float(prepared.objects.size()));
            }
        }
    }
    progress.Stage(5.f, "Restoring patrol paths, spawn and sounds");
    for (const auto& warning : prepared.gameplay.warnings) ELog.Msg(mtInformation, "Recovery: %s", warning.c_str());
    const auto getTool = [](ObjClassID id) -> ESceneCustomOTool*
    {
        auto* result = Scene->GetOTool(id);
        if (result && result->IsEnabled() && result->IsEditable()) return result;
        ELog.Msg(mtInformation, "Recovery: enable the %s tool to import its objects.", result ? result->ClassDesc() : "required");
        return nullptr;
    };
    size_t wayCount = 0, spawnCount = 0, globalSpawnCount = 0, soundCount = 0;
    if (!prepared.gameplay.ways.empty()) if (auto* wayTool = getTool(OBJCLASS_WAY))
        for (const auto& way : prepared.gameplay.ways)
        {
            std::unique_ptr<CCustomObject, DeleteObject> object(wayTool->CreateObject(nullptr, way.name.c_str()));
            static_cast<CWayObject*>(object.get())->ImportCompiledPath(way);
            objects.push_back(std::move(object));
            ++wayCount;
        }
    if (!prepared.gameplay.spawns.empty() || !prepared.gameplay.respawns.empty()) if (auto* spawnTool = getTool(OBJCLASS_SPAWNPOINT))
    {
        const auto less = [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; };
        std::set<std::string, decltype(less)> importedGlobal(less);
        size_t visited = 0;
        nextUpdate = GetTickCount64() + 33;
        for (const auto& record : prepared.gameplay.spawns)
        {
            const auto now = GetTickCount64();
            if (now >= nextUpdate)
            {
                nextUpdate = now + 33;
                progress.item->Update(5.f + float(visited) / float(prepared.gameplay.spawns.size()));
            }
            ++visited;
            const bool actor = _stricmp(record.section.c_str(), "actor") == 0;
            const std::string key = record.section + '\n' + (actor ? std::string() : record.name);
            if (!record.global && !record.respawn && (actor || !record.name.empty()) && importedGlobal.count(key)) continue;
            const auto name = record.name.empty() ? record.section + "_" + std::to_string(spawnCount) : record.name;
            try
            {
                // An entity factory assertion must not abort the entire level import.
                xrEditorOperationScope recovery;
                std::unique_ptr<CCustomObject, DeleteObject> object(spawnTool->CreateObject(nullptr, name.c_str()));
                if (!static_cast<CSpawnPoint*>(object.get())->ImportCompiledSpawn(record))
                    throw std::runtime_error("missing SDK section or incompatible entity state");
                objects.push_back(std::move(object));
                ++spawnCount;
                if (record.global)
                {
                    ++globalSpawnCount;
                    importedGlobal.insert(key);
                }
            }
            catch (const xrEditorOperationError& e) { ELog.Msg(mtInformation, "Recovery: skipped spawn %s: %s", name.c_str(), e.details); }
            catch (const std::runtime_error& e) { ELog.Msg(mtInformation, "Recovery: skipped spawn %s: %s", name.c_str(), e.what()); }
        }
        size_t index = 0;
        for (const auto& record : prepared.gameplay.respawns)
        {
            const auto name = "recovered_respawn_" + std::to_string(index++);
            std::unique_ptr<CCustomObject, DeleteObject> object(spawnTool->CreateObject((void*)RPOINT_CHOOSE_NAME, name.c_str()));
            static_cast<CSpawnPoint*>(object.get())->ImportCompiledRespawn(record);
            objects.push_back(std::move(object));
            ++spawnCount;
        }
    }
    if (!prepared.gameplay.sounds.empty()) if (auto* soundTool = getTool(OBJCLASS_SOUND_SRC))
        for (const auto& record : prepared.gameplay.sounds)
        {
            const auto name = "recovered_sound_" + std::to_string(soundCount);
            std::unique_ptr<CCustomObject, DeleteObject> object(soundTool->CreateObject(nullptr, name.c_str()));
            static_cast<ESoundSource*>(object.get())->ImportCompiledSound(record);
            objects.push_back(std::move(object));
            ++soundCount;
        }
    progress.Stage(6.f, "Opening the recovered scene");
    if (!ExecCommand(COMMAND_CLEAR)) throw std::runtime_error("Could not clear the current scene");
    if (globalSpawnCount) CLE_Visual::g_NoToAll_MissingVisual = true;
    for (auto& object : objects) Scene->AppendObject(object.release(), false);
    size_t navigationCount = 0;
    if (!prepared.gameplay.navigation.nodes.empty())
    {
        auto* aiMap = static_cast<ESceneAIMapTool*>(Scene->GetTool(OBJCLASS_AIMAP));
        if (aiMap && aiMap->IsEnabled() && aiMap->IsEditable())
        {
            aiMap->ImportCompiledNavigation(prepared.gameplay.navigation);
            navigationCount = prepared.gameplay.navigation.nodes.size();
        }
        else ELog.Msg(mtInformation, "Recovery: enable the AI Map tool to import navigation nodes.");
    }
    Scene->SynchronizeObjects();
    UISceneTabBar::ReplaceActiveScene("", prepared.name.c_str());
    Scene->UndoSave();
    const auto& target = prepared.cameraTarget;
    Fbox view;
    view.min.set(target[0] - 16.f, target[1] - 2.f, target[2] - 16.f);
    view.max.set(target[0] + 16.f, target[1] + 6.f, target[2] + 16.f);
    auto& camera = EDevice.m_Camera;
    const auto position = camera.GetPosition();
    camera.Set(0.f, -0.65f, 0.f, position.x, position.y, position.z);
    camera.ZoomExtents(view);
    ExecCommand(COMMAND_UPDATE_PROPERTIES);
    UI->RedrawScene();
    ELog.Msg(mtInformation, "Recovered gameplay: %zu patrol paths, %zu spawn/respawn points (%zu from all.spawn), %zu static sounds, %zu AI nodes.",
        wayCount, spawnCount, globalSpawnCount, soundCount, navigationCount);
    progress.item->Update(progress.item->max);
}
void ImportTrees()
{
    if (Scene->locked() || Scene->ObjCount(OBJCLASS_SCENEOBJECT))
        throw std::runtime_error("Import vegetation into a scene without existing static objects");
    const auto plan=LevelRecovery::BuildTreeLibrary(*level);
    for (const auto& instance : plan.instances)
        for (auto i : instance.meshes)
        {
            LevelRecovery::Placement placement;
            if (!LevelRecovery::TreePlacement(*level,level->meshes[i],placement))
                throw std::runtime_error("Some source transforms cannot be represented by SDK objects. Review tree-recovery.txt before importing the partial selection manually.");
        }
    string_path objectsRoot;
    FS.update_path(objectsRoot,_objects_,"");
    LevelRecovery::InstallTreeLibrary(*level,ConvertPath(objectsRoot,CP_ACP,CP_UTF8));
    FS.get_path(_objects_)->rescan_path_cb();
    // Use the actual SDK reader before modifying the scene. Defer render and pick
    // buffers while validating; restore preferences even if a load throws.
    struct DeferredBuffers
    {
        u32 flags;
        DeferredBuffers() : flags(EPrefs->object_flags.get())
        {
            EPrefs->object_flags.set(epoDeffLoadRB,TRUE);
            EPrefs->object_flags.set(epoDeffLoadCF,TRUE);
        }
        ~DeferredBuffers() { EPrefs->object_flags.assign(flags); }
    };
    {
        DeferredBuffers deferred;
        for (size_t i=0; i<plan.models.size(); ++i)
        {
            string64 name;
            xr_sprintf(name,"tree_%05u.object",unsigned(i));
            const std::string path=std::string(objectsRoot)+level->libraryName+"\\"+name;
            CEditableObject object("recovery_preflight");
            if (!object.Load(path.c_str())) throw std::runtime_error("SDK reader rejected recovered model "+std::to_string(i));
        }
    }
    const auto selection=ConvertPath(treeExport+"\\trees.selection",CP_UTF8,CP_ACP);
    const bool loaded=Scene->LoadSelection(selection.c_str());
    Scene->UndoSave();
    ExecCommand(COMMAND_CHANGE_ACTION,etaSelect);
    ExecCommand(COMMAND_UPDATE_PROPERTIES);
    UI->RedrawScene();
    if (!loaded) throw std::runtime_error("Selection import did not complete; use Undo to remove imported objects");
    status="Vegetation imported as shared object references. Map recovered materials before Build or Make Game. Static geometry and collision have not been imported.";
}
void ImportGeometry()
{
    if (!staticLibrary || Scene->locked() || Scene->ObjCount(OBJCLASS_SCENEOBJECT))
        throw std::runtime_error("Import recovered geometry into a scene without existing static objects");
    string_path root;
    FS.update_path(root,_objects_,"");
    const auto utf8Root=ConvertPath(root,CP_ACP,CP_UTF8);
    LevelRecovery::InstallStaticLibrary(*level,*staticLibrary,utf8Root);
    const auto trees=LevelRecovery::BuildTreeLibrary(*level);
    if (!trees.models.empty()) LevelRecovery::InstallTreeLibrary(*level,utf8Root);
    FS.get_path(_objects_)->rescan_path_cb();
    struct DeferredBuffers
    {
        u32 flags=EPrefs->object_flags.get();
        DeferredBuffers() { EPrefs->object_flags.set(epoDeffLoadRB,TRUE); EPrefs->object_flags.set(epoDeffLoadCF,TRUE); }
        ~DeferredBuffers() { EPrefs->object_flags.assign(flags); }
    };
    {
        DeferredBuffers deferred;
        const auto validate=[&](const std::string& library,const char* prefix,size_t count)
        {
            for (size_t i=0; i<count; ++i)
            {
                string64 file;
                xr_sprintf(file,"%s_%05u.object",prefix,unsigned(i));
                const std::string path=std::string(root)+library+"\\"+file;
                CEditableObject object("recovery_geometry_preflight");
                if (!object.Load(path.c_str())) throw std::runtime_error("SDK reader rejected "+path);
            }
        };
        validate(LevelRecovery::StaticLibraryName(*level),"static",staticLibrary->models.size());
        validate(level->libraryName,"tree",trees.models.size());
    }
    const auto selection=ConvertPath(sceneExport+"\\geometry.selection",CP_UTF8,CP_ACP);
    const bool loaded=Scene->LoadSelection(selection.c_str());
    Scene->UndoSave(); ExecCommand(COMMAND_CHANGE_ACTION,etaSelect);
    ExecCommand(COMMAND_UPDATE_PROPERTIES); UI->RedrawScene();
    if (!loaded) throw std::runtime_error("Geometry import did not complete; use Undo to remove imported objects");
    status="Render geometry imported. Static groups are surface candidates requiring review, not proven author objects. Materials remain unmapped. Original CFORM was kept separately and was not converted to colliders.";
}
}
void Open() { open = true; }
bool CanOpenCompiledLevel() { return !Busy(); }

void OpenCompiledLevel()
{
    if (Busy()) return;
    try
    {
        const auto folder = PickLevelFolder();
        if (folder.empty()) return;
        if (!Scene->IfModified()) return;
        const auto path = FolderPathUtf8(folder);
        string_path root;
        FS.update_path(root, _objects_, "");
        const auto objectsRoot = ConvertPath(root, CP_ACP, CP_UTF8);
        FS.update_path(root, "$game_textures$", "");
        const auto texturesRoot = ConvertPath(root, CP_ACP, CP_UTF8);
        CompiledLoadProgress progress;
        const char* stages[] = {
            "Reading level geometry and collision", "Grouping static surfaces",
            "Preparing preview textures", "Preparing the recovered object library"
        };
        std::atomic<int> currentStage{0};
        progress.Stage(0.f, stages[0]);
        auto preparation = std::async(std::launch::async, [&]
        {
            auto data = LevelRecovery::Read(path);
            currentStage.store(1);
            LevelRecovery::GroupConnectedSurfaces(data);
            const auto separator = data.source.find_last_of("\\/");
            auto name = data.source.substr(separator == std::string::npos ? 0 : separator + 1);
            const auto target = LevelRecovery::PreviewCameraTarget(data);
            currentStage.store(2);
            const auto missing = LevelRecovery::NativeIO::PreparePreviewTextures(data, texturesRoot);
            currentStage.store(3);
            return CompiledScene{std::move(name), LevelRecovery::NativeIO::PrepareSceneImport(data, objectsRoot), target,
                missing, LevelRecovery::ReadGameplay(path, DecodeArchiveIndex)};
        });
        int displayedStage = 0;
        while (preparation.wait_for(std::chrono::milliseconds(33)) != std::future_status::ready)
        {
            const int stage = currentStage.load();
            if (stage != displayedStage)
            {
                displayedStage = stage;
                progress.Stage(float(stage), stages[stage]);
            }
            else UI->ProgressDraw();
        }
        const auto prepared = preparation.get();
        OpenRecoveredScene(prepared, progress);
        ELog.Msg(mtInformation, "Recovered %zu objects from %s.", prepared.objects.size(), prepared.name.c_str());
        if (prepared.missingTextures)
            ELog.Msg(mtInformation, "%zu missing base textures use a placeholder.", prepared.missingTextures);
    }
    catch (const std::exception& e) { ELog.DlgMsg(mtError, "%s", e.what()); }
}

void Shutdown()
{
    // Worker accesses only its own data, and must finish before DLL unload.
    if (job.valid()) job.wait();
}
void Draw()
{
    if (job.valid() && job.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        try
        {
            auto result = job.get();
            level = std::move(result.level);
            status = std::move(result.message);
            if (!result.treeExport.empty()) treeExport=std::move(result.treeExport);
            if (!result.sceneExport.empty()) sceneExport=std::move(result.sceneExport);
            if (result.staticLibrary) staticLibrary=std::move(result.staticLibrary);
        }
        catch (const std::exception& e) { status = e.what(); }
    }
    if (!open) return;
    ImGui::SetNextWindowSize(ImVec2(760, 620), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Level Recovery (experimental)", &open))
    {
        ImGui::TextWrapped("Recover editable static surface candidates and whole vegetation. Repeated geometry is verified before sharing. Physics remains separate; authored static object boundaries require review.");
        if (!Busy())
        {
            ImGui::InputText("Source level folder", source, sizeof(source));
            if (ImGui::Button("Read and analyze") && source[0])
            {
                const std::string path = source;
                level.reset();
                treeExport.clear();
                sceneExport.clear(); staticLibrary.reset();
                status = "Reading geometry and analyzing shared edges...";
                job = std::async(std::launch::async, [path]()
                {
                    auto data = std::make_shared<LevelRecovery::Level>(LevelRecovery::Read(path));
                    LevelRecovery::GroupConnectedSurfaces(*data);
                    return Result{data, LevelRecovery::Describe(*data)};
                });
            }
            ImGui::InputText("New export folder", destination, sizeof(destination));
            if (level && ImGui::Button("Export recovery package") && destination[0])
            {
                const std::string path = destination;
                const auto data = level;
                status = "Exporting render geometry, collision and provenance...";
                job = std::async(std::launch::async, [data, path]()
                {
                    LevelRecovery::Export(*data, path);
                    return Result{data, LevelRecovery::Describe(*data) + "\nExport completed: " + path};
                });
            }
            if (level && !Busy() && !level->treePrototypes.empty() && ImGui::Button("Export native tree library") && destination[0])
            {
                const auto data=level;
                const std::string path=destination;
                status="Writing native tree objects and instance selection...";
                job=std::async(std::launch::async,[data,path]()
                {
                    LevelRecovery::ExportTreeLibrary(*data,path);
                    return Result{data,"Native tree library exported: "+path+"\nReview tree-recovery.txt for placement exclusions. Use Import vegetation to install models and load placements.",path};
                });
            }
            if (level && !Busy() && !treeExport.empty() && ImGui::Button("Import vegetation into scene"))
            {
                try { ImportTrees(); }
                catch (const std::exception& e) { status=e.what(); }
            }
            if (level && !Busy() && ImGui::Button("Export native geometry scene") && destination[0])
            {
                const auto data=level;
                const std::string path=destination;
                status="Recovering static candidates, verifying repeated models and exporting geometry...";
                job=std::async(std::launch::async,[data,path]()
                {
                    auto plan=std::make_shared<LevelRecovery::StaticLibrary>(LevelRecovery::BuildStaticLibrary(*data));
                    LevelRecovery::ExportSceneLibrary(*data,*plan,path);
                    return Result{data,"Native geometry exported: "+path+"\nStatic candidates: "+std::to_string(plan->instances.size())+
                        "; static models: "+std::to_string(plan->models.size())+". Review scene-recovery.txt before import.","",path,plan};
                });
            }
            if (level && !Busy() && staticLibrary && !sceneExport.empty() && ImGui::Button("Import recovered geometry into scene"))
            {
                try { ImportGeometry(); }
                catch (const std::exception& e) { status=e.what(); }
            }
        }
        else ImGui::TextUnformatted("Working in background. You can close this window.");
        ImGui::Separator();
        ImGui::TextWrapped("Surface candidates are connected geometry, not guaranteed original objects. Terrain triangles are retained. Render geometry never creates colliders. Unknown layouts stop recovery with an error.");
        ImGui::BeginChild("recovery_status", ImVec2(0, 0), true);
        ImGui::TextUnformatted(status.c_str());
        ImGui::EndChild();
    }
    ImGui::End();
}
}
