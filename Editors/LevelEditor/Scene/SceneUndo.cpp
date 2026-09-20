#include "stdafx.h"

//------------------------------------------------------------------------------
// Undo Commands Implementation
//------------------------------------------------------------------------------

class UndoTransformCommand : public IUndoCommand
{
    xr_string m_name;
    xr_vector<ObjectTransformState> m_transforms;
public:
    UndoTransformCommand(LPCSTR name, const xr_vector<ObjectTransformState>& data)
        : m_name(name), m_transforms(data) {}
    virtual ~UndoTransformCommand() {}

    virtual LPCSTR GetName() const override { return m_name.c_str(); }

    virtual void Undo() override
    {
        for (const auto& item : m_transforms)
        {
            CCustomObject* obj = Scene->FindObjectByName(item.name.c_str(), item.classID);
            if (obj)
            {
                obj->PPosition = item.pos_before;
                obj->PRotation = item.rot_before;
                obj->PScale = item.scale_before;
                obj->UpdateTransform();
            }
        }
        UI->RedrawScene();
        ExecCommand(COMMAND_UPDATE_PROPERTIES);
    }

    virtual void Redo() override
    {
        for (const auto& item : m_transforms)
        {
            CCustomObject* obj = Scene->FindObjectByName(item.name.c_str(), item.classID);
            if (obj)
            {
                obj->PPosition = item.pos_after;
                obj->PRotation = item.rot_after;
                obj->PScale = item.scale_after;
                obj->UpdateTransform();
            }
        }
        UI->RedrawScene();
        ExecCommand(COMMAND_UPDATE_PROPERTIES);
    }
};

struct SerializedObjectChunk
{
    ObjClassID classID;
    shared_str name;
    u8* data;
    u32 size;

    SerializedObjectChunk() : classID(OBJCLASS_DUMMY), data(nullptr), size(0) {}
    ~SerializedObjectChunk() { xr_free(data); }

    SerializedObjectChunk(const SerializedObjectChunk& other)
    {
        classID = other.classID;
        name = other.name;
        size = other.size;
        if (size)
        {
            data = (u8*)xr_malloc(size);
            CopyMemory(data, other.data, size);
        }
        else
        {
            data = nullptr;
        }
    }

    SerializedObjectChunk& operator=(const SerializedObjectChunk& other)
    {
        if (this != &other)
        {
            xr_free(data);
            classID = other.classID;
            name = other.name;
            size = other.size;
            if (size)
            {
                data = (u8*)xr_malloc(size);
                CopyMemory(data, other.data, size);
            }
            else
            {
                data = nullptr;
            }
        }
        return *this;
    }

    SerializedObjectChunk(SerializedObjectChunk&& other) noexcept
    {
        classID = other.classID;
        name = std::move(other.name);
        data = other.data;
        size = other.size;
        other.data = nullptr;
        other.size = 0;
    }

    SerializedObjectChunk& operator=(SerializedObjectChunk&& other) noexcept
    {
        if (this != &other)
        {
            xr_free(data);
            classID = other.classID;
            name = std::move(other.name);
            data = other.data;
            size = other.size;
            other.data = nullptr;
            other.size = 0;
        }
        return *this;
    }
};

static void SerializeObject(CCustomObject* obj, SerializedObjectChunk& chunk)
{
    chunk.classID = obj->FClassID;
    chunk.name = obj->GetName();
    CMemoryWriter writer;
    obj->SaveStream(writer);
    chunk.size = writer.size();
    if (chunk.size)
    {
        chunk.data = (u8*)xr_malloc(chunk.size);
        CopyMemory(chunk.data, writer.pointer(), chunk.size);
    }
    else
    {
        chunk.data = nullptr;
    }
}

static CCustomObject* DeserializeObject(const SerializedObjectChunk& chunk)
{
    ESceneCustomOTool* tool = Scene->GetOTool(chunk.classID);
    if (!tool) return nullptr;
    CCustomObject* obj = tool->CreateObject(0, chunk.name.c_str());
    if (obj)
    {
        if (chunk.size > 0 && chunk.data)
        {
            IReader reader(chunk.data, chunk.size);
            obj->LoadStream(reader);
        }
        Scene->AppendObject(obj, false);
    }
    return obj;
}

class UndoCreateCommand : public IUndoCommand
{
    xr_vector<SerializedObjectChunk> m_objects;
public:
    UndoCreateCommand(const ObjectList& created_objects)
    {
        for (CCustomObject* obj : created_objects)
        {
            SerializedObjectChunk chunk;
            SerializeObject(obj, chunk);
            m_objects.push_back(std::move(chunk));
        }
    }

    virtual ~UndoCreateCommand() {}
    virtual LPCSTR GetName() const override { return "Create Object(s)"; }

    virtual void Undo() override
    {
        for (const auto& chunk : m_objects)
        {
            CCustomObject* obj = Scene->FindObjectByName(chunk.name.c_str(), chunk.classID);
            if (obj)
            {
                Scene->RemoveObject(obj, false, true);
                xr_delete(obj);
            }
        }
        UI->RedrawScene();
        ExecCommand(COMMAND_UPDATE_PROPERTIES);
    }

    virtual void Redo() override
    {
        for (const auto& chunk : m_objects)
        {
            DeserializeObject(chunk);
        }
        UI->RedrawScene();
        ExecCommand(COMMAND_UPDATE_PROPERTIES);
    }
};

class UndoDeleteCommand : public IUndoCommand
{
    xr_vector<SerializedObjectChunk> m_objects;
public:
    UndoDeleteCommand(const ObjectList& deleted_objects)
    {
        for (CCustomObject* obj : deleted_objects)
        {
            SerializedObjectChunk chunk;
            SerializeObject(obj, chunk);
            m_objects.push_back(std::move(chunk));
        }
    }

    virtual ~UndoDeleteCommand() {}
    virtual LPCSTR GetName() const override { return "Delete Object(s)"; }

    virtual void Undo() override
    {
        for (const auto& chunk : m_objects)
        {
            DeserializeObject(chunk);
        }
        UI->RedrawScene();
        ExecCommand(COMMAND_UPDATE_PROPERTIES);
    }

    virtual void Redo() override
    {
        for (const auto& chunk : m_objects)
        {
            CCustomObject* obj = Scene->FindObjectByName(chunk.name.c_str(), chunk.classID);
            if (obj)
            {
                Scene->RemoveObject(obj, false, true);
                xr_delete(obj);
            }
        }
        UI->RedrawScene();
        ExecCommand(COMMAND_UPDATE_PROPERTIES);
    }
};

class UndoSnapshotCommand : public IUndoCommand
{
    UndoItem m_snapshot;
public:
    UndoSnapshotCommand()
    {
        CMemoryWriter undo_writer;
        Scene->SaveStream(undo_writer, true, true);
        m_snapshot.m_Size = undo_writer.size();
        if (m_snapshot.m_Size)
        {
            m_snapshot.m_Data = (u8*)xr_malloc(m_snapshot.m_Size);
            CopyMemory(m_snapshot.m_Data, undo_writer.pointer(), m_snapshot.m_Size);
        }
        else
        {
            m_snapshot.m_Data = nullptr;
        }
    }

    virtual ~UndoSnapshotCommand()
    {
        xr_free(m_snapshot.m_Data);
    }

    virtual LPCSTR GetName() const override { return "Full Scene Snapshot"; }

    virtual void Undo() override
    {
        if (m_snapshot.m_Size > 0 && m_snapshot.m_Data)
        {
            Scene->Unload(TRUE);
            IReader r(m_snapshot.m_Data, m_snapshot.m_Size, 0);
            Scene->LoadStream(r, true);
        }
    }

    virtual void Redo() override
    {
        Undo();
    }
};

//------------------------------------------------------------------------------
// EScene Methods
//------------------------------------------------------------------------------

void EScene::UndoClear()
{
    for (auto* cmd : m_RedoStack)
        delete cmd;
    m_RedoStack.clear();

    for (auto* cmd : m_UndoStack)
        delete cmd;
    m_UndoStack.clear();
}

void EScene::UndoSaveCommand(IUndoCommand* cmd)
{
    if (UI->GetEState() != esEditScene)
    {
        delete cmd;
        return;
    }
    Modified();
    UI->RedrawScene();

    if (0 == EPrefs->scene_undo_level)
    {
        delete cmd;
        return;
    }

    for (auto* c : m_RedoStack)
        delete c;
    m_RedoStack.clear();

    m_UndoStack.push_back(cmd);

    while (m_UndoStack.size() > EPrefs->scene_undo_level)
    {
        delete m_UndoStack.front();
        m_UndoStack.pop_front();
    }
}

void EScene::UndoSave()
{
    UndoSaveCommand(xr_new<UndoSnapshotCommand>());
}

void EScene::UndoSaveTransform(LPCSTR action_name, const xr_vector<ObjectTransformState>& states)
{
    if (states.empty()) return;
    UndoSaveCommand(xr_new<UndoTransformCommand>(action_name, states));
}

void EScene::UndoSaveCreate(CCustomObject* object)
{
    if (!object) return;
    ObjectList lst;
    lst.push_back(object);
    UndoSaveCommand(xr_new<UndoCreateCommand>(lst));
}

void EScene::UndoSaveCreate(const ObjectList& objects)
{
    if (objects.empty()) return;
    UndoSaveCommand(xr_new<UndoCreateCommand>(objects));
}

void EScene::UndoSaveDelete(CCustomObject* object)
{
    if (!object) return;
    ObjectList lst;
    lst.push_back(object);
    UndoSaveCommand(xr_new<UndoDeleteCommand>(lst));
}

void EScene::UndoSaveDelete(const ObjectList& objects)
{
    if (objects.empty()) return;
    UndoSaveCommand(xr_new<UndoDeleteCommand>(objects));
}

bool EScene::Undo()
{
    if (!m_UndoStack.empty())
    {
        IUndoCommand* cmd = m_UndoStack.back();
        m_UndoStack.pop_back();

        cmd->Undo();

        m_RedoStack.push_back(cmd);
        while (m_RedoStack.size() > EPrefs->scene_undo_level)
        {
            delete m_RedoStack.front();
            m_RedoStack.pop_front();
        }

        UI->UpdateScene();
        Modified();
        return true;
    }
    return false;
}

bool EScene::Redo()
{
    if (!m_RedoStack.empty())
    {
        IUndoCommand* cmd = m_RedoStack.back();
        m_RedoStack.pop_back();

        cmd->Redo();

        m_UndoStack.push_back(cmd);
        while (m_UndoStack.size() > EPrefs->scene_undo_level)
        {
            delete m_UndoStack.front();
            m_UndoStack.pop_front();
        }

        UI->UpdateScene();
        Modified();
        return true;
    }
    return false;
}
