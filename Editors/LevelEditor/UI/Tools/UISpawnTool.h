#pragma once
class UISpawnTool : public UIToolCustom
{
public:
	UISpawnTool();
	virtual ~UISpawnTool();
	virtual void Draw();
	void SelByRefObject(bool flag);
	void MultiSelByRefObject(bool clear_prev);
	IC const char *Current() { return m_Current; }
	IC void SetAttachObject(bool AttachObject) { m_AttachObject = AttachObject; }
	IC bool IsAttachObject() const { return m_AttachObject; }

	void SelectRef(const char* caption)
	{
		m_SpawnList->SelectItem(caption);
		ExecCommand(COMMAND_RENDER_FOCUS);
	}

	// Auto-shape settings
	IC bool  IsAutoShape()       const { return m_AutoShape; }
	IC bool  IsAutoShapeSphere() const { return m_AutoShapeIsSphere; }
	IC float GetAutoShapeSize()  const { return m_AutoShapeSize; }

	IC void  SetAutoShape(bool val)        { m_AutoShape = val; }
	IC void  SetAutoShapeSphere(bool val)  { m_AutoShapeIsSphere = val; }
	IC void  SetAutoShapeSize(float val)   { m_AutoShapeSize = val; }

private:
	void RefreshList();
	void OnItemFocused(ListItem *item);
	const char *m_Current;
	UIItemListForm *m_SpawnList;
	float m_selPercent;
	bool m_AttachObject;

	// Auto-shape
	bool  m_AutoShape;         // whether to auto-attach a shape on placement
	bool  m_AutoShapeIsSphere; // true = sphere, false = box
	float m_AutoShapeSize;     // radius for sphere / half-extent for box
};