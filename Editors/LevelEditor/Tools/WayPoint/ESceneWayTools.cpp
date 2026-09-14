#include "stdafx.h"

CCustomObject *ESceneWayTool::CreateObject(LPVOID data, LPCSTR name)
{
    CWayObject *O = xr_new<CWayObject>(data, name);
    O->FParentTools = this;
    UIWayTool *frame = (UIWayTool *)pForm;
    if (frame && frame->IsAutoFlag())
    {
        for (WPIt it = O->GetWayPoints().begin(); it != O->GetWayPoints().end(); ++it)
        {
            (*it)->SetFlag(0x1);
        }
    }
    return O;
}
