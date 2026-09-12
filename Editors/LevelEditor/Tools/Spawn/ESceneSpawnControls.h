#pragma once

// refs
class TfraRPoint;

class TUI_ControlSpawnAdd : public TUI_CustomControl
{
    bool AppendCallback(SBeforeAppendCallbackParams *p);
    bool AfterAppendCallback(TShiftState Shift, CCustomObject *obj);

public:
    TUI_ControlSpawnAdd(int st, int act, ESceneToolBase *parent);
    virtual ~TUI_ControlSpawnAdd() { ; }
    virtual bool Start(TShiftState _Shift);
};