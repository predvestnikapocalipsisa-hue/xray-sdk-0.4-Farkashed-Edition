#pragma once
class EDetailManager;
class UIDOTool : public UIToolCustom
{
public:
	UIDOTool();
	virtual ~UIDOTool();
	virtual void Draw();
	virtual void OnDrawUI();
	EDetailManager *DM;
	float GetBrushRadius() const { return m_BrushRadius; }
	u32 GetBrushColor() const;

private:
	bool m_DOShuffle;
	char m_NewMapName[256];
	int m_NewMapResolution;
	float m_NewMapColor[4];
	float m_AutoSlopeDeg;
	float m_AutoGrassColor[4];

	float m_BrushRadius;
	float m_BrushColor[4];
	bool m_EraserMode;
};