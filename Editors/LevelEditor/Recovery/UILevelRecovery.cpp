#include "stdafx.h"
#include "UILevelRecovery.h"
#include "LevelRecovery.h"
#include <chrono>
#include <future>
#include <memory>

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
bool Busy() { return job.valid(); }

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
