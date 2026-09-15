#include "stdafx.h"

void EScene::UndoClear()
{
	m_RedoStack.clear();
	m_UndoStack.clear();
}

void EScene::UndoSave()
{
	if (UI->GetEState() != esEditScene)
		return;
	Modified();
	UI->RedrawScene();

	if (0 == EPrefs->scene_undo_level)
		return;

	CMemoryWriter undo_writer;          // отдельный буфер, не m_SaveCache!
	SaveStream(undo_writer, true, true);

	UndoItem item;
	item.m_Size = undo_writer.size();
	if (item.m_Size)
	{
		item.m_Data = (u8*)xr_malloc(item.m_Size);
		CopyMemory(item.m_Data, undo_writer.pointer(), item.m_Size);
	}
	undo_writer.free();

	m_UndoStack.push_back(std::move(item));
	m_RedoStack.clear();

	if (m_UndoStack.size() > EPrefs->scene_undo_level)
		m_UndoStack.pop_front();
}

bool EScene::Undo()
{
	if (m_UndoStack.size() > 1)
	{
		m_RedoStack.push_back(std::move(m_UndoStack.back()));
		m_UndoStack.pop_back();

		if (m_RedoStack.size() > EPrefs->scene_undo_level)
			m_RedoStack.pop_front();

		if (!m_UndoStack.empty())
		{
			Unload(TRUE);
			UndoItem& item = m_UndoStack.back();
			IReader r(item.m_Data, item.m_Size, 0);
			LoadStream(r, true);
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
		Unload(TRUE);
		UndoItem& ritem = m_RedoStack.back();
		IReader r(ritem.m_Data, ritem.m_Size, 0);
		LoadStream(r, true);

		m_UndoStack.push_back(std::move(m_RedoStack.back()));
		m_RedoStack.pop_back();

		if (m_UndoStack.size() > EPrefs->scene_undo_level)
			m_UndoStack.pop_front();

		UI->UpdateScene();
		Modified();
		return true;
	}
	return false;
}

//----------------------------------------------------