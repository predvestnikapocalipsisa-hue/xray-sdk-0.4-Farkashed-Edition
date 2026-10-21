#include "stdafx.h"
#include "../../XrECore/Editor/SDKScriptHost.h"
#include <map>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace
{
    std::map<u64,xr_string> handles;
    u64 nextHandle = 1;
    unsigned batchDepth = 0;
    bool dirty = false;
    bool checkpointUnsaved = false, checkpointModified = false;
    std::unique_ptr<CMemoryWriter> checkpoint;

    std::string Vector(const Fvector& v)
    {
        std::ostringstream out; out << std::setprecision(9) << '[' << v.x << ',' << v.y << ',' << v.z << ']'; return out.str();
    }
    std::vector<std::string> Split(const std::string& value)
    {
        std::vector<std::string> parts; size_t from=0, at;
        do { at=value.find('\x1f',from); parts.push_back(value.substr(from,at-from)); from=at+1; } while(at!=std::string::npos);
        return parts;
    }
    void RequireScene()
    {
        if (!Scene || !Scene->valid() || Scene->locked()) throw std::runtime_error("Scene is unavailable or locked");
    }
    u64 Handle(CCustomObject* object)
    {
        if (!object->ScriptHandle) object->ScriptHandle=nextHandle++;
        handles[object->ScriptHandle]=object->GetName(); return object->ScriptHandle;
    }
    CCustomObject* Resolve(const std::string& value)
    {
        const u64 id=std::stoull(value);
        auto found=handles.find(id);
        if(found==handles.end()) throw std::runtime_error("Invalid or expired object handle");
        CCustomObject* object=Scene->FindObjectByName(found->second.c_str(),ObjClassID(OBJCLASS_DUMMY));
        if(!object || object->IsDeleted() || object->ScriptHandle!=id) throw std::runtime_error("Object was deleted, renamed or replaced; reacquire it from the scene");
        return object;
    }
    void Changed()
    {
        Scene->Modified(); Scene->OnObjectsUpdate(); UI->UpdateScene();
        if(batchDepth) dirty=true; else Scene->UndoSave();
    }
    std::string Name(const std::string& utf8)
    {
        const std::string name=SDKScript::FromUTF8(utf8);
        if(name.empty() || name.size()>200 || name.find_first_of("\r\n\t\\/\x1f")!=std::string::npos)
            throw std::runtime_error("Object name must be 1-200 characters without separators");
        return name;
    }
    std::string SceneCall(const std::string& op,const std::string& text,double x,double y,double z)
    {
        if(op=="invalidate")
        {
            if(batchDepth) throw std::runtime_error("Load, clear and Undo are not allowed inside a scene transaction; use an operator with undo=False");
            handles.clear(); return "null";
        }
        RequireScene();
        if(op=="scene.begin")
        {
            if(!batchDepth)
            {
                checkpoint.reset(new CMemoryWriter);
                checkpointUnsaved=Scene->m_RTFlags.is(EScene::flRT_Unsaved);
                checkpointModified=Scene->m_RTFlags.is(EScene::flRT_Modified);
                Scene->SaveStream(*checkpoint,true,true); dirty=false;
            }
            ++batchDepth; return "null";
        }
        if(op=="scene.commit")
        {
            if(!batchDepth) throw std::runtime_error("No active transaction");
            if(!--batchDepth) { if(dirty) Scene->UndoSave(); checkpoint.reset(); dirty=false; }
            return "null";
        }
        if(op=="scene.rollback")
        {
            if(!batchDepth) throw std::runtime_error("No active transaction");
            batchDepth=0;
            if(dirty && checkpoint)
            {
                Scene->Unload(TRUE); IReader reader(checkpoint->pointer(),checkpoint->size(),0);
                Scene->LoadStream(reader,true); Scene->OnObjectsUpdate(); UI->UpdateScene(); handles.clear();
                Scene->m_RTFlags.set(EScene::flRT_Unsaved,checkpointUnsaved);
                Scene->m_RTFlags.set(EScene::flRT_Modified,checkpointModified);
            }
            checkpoint.reset(); dirty=false; return "null";
        }
        if(op=="scene.objects" || op=="scene.selected")
        {
            ObjectList objects; Scene->GetQueryObjects(objects,OBJCLASS_DUMMY,op=="scene.selected" ? 1 : -1,-1,-1);
            std::string result="[";
            for(auto* object:objects) if(!object->IsDeleted()) { if(result.size()>1) result+=","; result+=std::to_string(Handle(object)); }
            return result+"]";
        }
        if(op=="scene.find")
        {
            auto* object=Scene->FindObjectByName(SDKScript::FromUTF8(text).c_str(),ObjClassID(OBJCLASS_DUMMY));
            return object && !object->IsDeleted() ? std::to_string(Handle(object)) : "null";
        }
        if(op=="scene.create")
        {
            const auto parts=Split(text); if(parts.size()!=3) throw std::runtime_error("Invalid creation arguments");
            ObjClassID kind;
            if(parts[0]=="object") kind=OBJCLASS_SCENEOBJECT;
            else if(parts[0]=="light") kind=OBJCLASS_LIGHT;
            else if(parts[0]=="shape") kind=OBJCLASS_SHAPE;
            else if(parts[0]=="glow") kind=OBJCLASS_GLOW;
            else if(parts[0]=="way") kind=OBJCLASS_WAY;
            else throw std::runtime_error("Supported object types: object, light, shape, glow, way");
            const std::string reference=SDKScript::FromUTF8(parts[2]);
            if(kind==OBJCLASS_SCENEOBJECT)
            {
                string_path model;
                FS.update_path(model,"$objects$",EFS.ChangeFileExt(reference.c_str(),".object").c_str());
                if(reference.empty() || !FS.exist(model)) throw std::runtime_error("A scene object requires an existing object-library reference");
            }
            string256 generated;
            if(parts[1].empty()) Scene->GenObjectName(kind,generated);
            else { const auto name=Name(parts[1]); strcpy_s(generated,name.c_str()); }
            if(Scene->FindObjectByName(generated,ObjClassID(OBJCLASS_DUMMY))) throw std::runtime_error("Object name already exists");
            auto* tool=Scene->GetOTool(kind);
            if(!tool || !tool->IsEditable()) throw std::runtime_error("Object tool is not editable");
            std::unique_ptr<CCustomObject> object(tool->CreateObject(nullptr,generated));
            if(!object) throw std::runtime_error("Object creation failed");
            if(kind==OBJCLASS_SCENEOBJECT && !static_cast<CSceneObject*>(object.get())->SetReference(reference.c_str())) throw std::runtime_error("Cannot set object reference");
            if(kind==OBJCLASS_SHAPE) { Fmatrix box; box.identity(); static_cast<CEditShape*>(object.get())->add_box(box); }
            Fvector position; position.set(float(x),float(y),float(z));
            if(!_valid(position)) throw std::runtime_error("Invalid creation position");
            object->SetPosition(position);
            const u64 handle=Handle(object.get()); Scene->AppendObject(object.release(),false); Changed(); return std::to_string(handle);
        }
        if(op.compare(0,7,"object.")!=0) throw std::runtime_error("Unknown Level Editor operation");
        const auto parts=Split(text); CCustomObject* object=Resolve(parts[0]);
        if(op=="object.get")
        {
            const char* reference=nullptr;
            if(object->FClassID==OBJCLASS_SCENEOBJECT) reference=static_cast<CSceneObject*>(object)->RefName();
            return "{\"name\":"+SDKScript::Quote(SDKScript::ToUTF8(object->GetName()))+",\"position\":"+Vector(object->GetPosition())+
                ",\"rotation\":"+Vector(object->GetRotation())+",\"scale\":"+Vector(object->GetScale())+",\"selected\":"+(object->Selected()?"true":"false")+
                ",\"visible\":"+(object->Visible()?"true":"false")+",\"class_id\":"+std::to_string(object->FClassID)+",\"reference\":"+(reference?SDKScript::Quote(SDKScript::ToUTF8(reference)):"null")+"}";
        }
        if(op=="object.select") { object->Select(x!=0); UI->UpdateScene(); return "null"; }
        if(!object->Editable()) throw std::runtime_error("Object is locked or not editable");
        Scene->BeforeObjectChange(object);
        if(op=="object.position" || op=="object.rotation" || op=="object.scale")
        {
            Fvector value; value.set(float(x),float(y),float(z));
            if(!_valid(value)) throw std::runtime_error("Invalid transform");
            if(op=="object.position") object->SetPosition(value);
            else if(op=="object.rotation") object->SetRotation(value);
            else { if(x<=0 || y<=0 || z<=0) throw std::runtime_error("Scale must be positive"); object->SetScale(value); }
        }
        else if(op=="object.visible") object->Show(x!=0);
        else if(op=="object.rename")
        {
            if(parts.size()!=2) throw std::runtime_error("Invalid rename arguments");
            const std::string name=Name(parts[1]);
            auto* existing=Scene->FindObjectByName(name.c_str(),ObjClassID(OBJCLASS_DUMMY));
            if(existing && existing!=object) throw std::runtime_error("Object name already exists");
            object->SetName(name.c_str()); handles[object->ScriptHandle]=object->GetName();
        }
        else if(op=="object.delete") { handles.erase(object->ScriptHandle); Scene->RemoveObject(object,false,true); xr_delete(object); }
        else throw std::runtime_error("Unknown object operation");
        Changed(); return "null";
    }
}

void RegisterSDKScriptScene() { SDKScript::SetBackend(SceneCall); }

bool SDKScriptSceneCaptureUndo()
{
    if(!batchDepth) return false;
    dirty=true;
    return true;
}
