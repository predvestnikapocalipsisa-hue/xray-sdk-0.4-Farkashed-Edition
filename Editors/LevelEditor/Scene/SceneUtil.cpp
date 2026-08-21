#include "stdafx.h"

CCustomObject *EScene::FindObjectByName(LPCSTR name, ObjClassID classfilter)
{
    if (!name)
        return NULL;

    CCustomObject *object = 0;
    if (classfilter == OBJCLASS_DUMMY)
    {
        SceneToolsMapPairIt _I = m_SceneTools.begin();
        SceneToolsMapPairIt _E = m_SceneTools.end();
        for (; _I != _E; ++_I)
        {
            ESceneCustomOTool *mt = dynamic_cast<ESceneCustomOTool *>(_I->second);

            if (mt && (0 != (object = mt->FindObjectByName(name))))
                return object;
        }
    }
    else
    {
        ESceneCustomOTool *mt = GetOTool(classfilter);
        VERIFY(mt);
        if (mt && (0 != (object = mt->FindObjectByName(name))))
            return object;
    }
    return object;
}

CCustomObject *EScene::FindObjectByName(LPCSTR name, CCustomObject *pass_object)
{
    CCustomObject *object = 0;
    SceneToolsMapPairIt _I = m_SceneTools.begin();
    SceneToolsMapPairIt _E = m_SceneTools.end();
    for (; _I != _E; _I++)
    {
        ESceneCustomOTool *mt = dynamic_cast<ESceneCustomOTool *>(_I->second);
        if (mt && (0 != (object = mt->FindObjectByName(name, pass_object))))
            return object;
    }
    return 0;
}

bool EScene::FindDuplicateName()
{
    // find duplicate name
    SceneToolsMapPairIt _I = m_SceneTools.begin();
    SceneToolsMapPairIt _E = m_SceneTools.end();
    for (; _I != _E; _I++)
    {
        ESceneCustomOTool *mt = dynamic_cast<ESceneCustomOTool *>(_I->second);
        if (mt)
        {
            ObjectList &lst = mt->GetObjects();
            for (ObjectIt _F = lst.begin(); _F != lst.end(); _F++)
                if (FindObjectByName((*_F)->GetName(), *_F))
                {
                    ELog.DlgMsg(mtError, "Duplicate object name already exists: '%s'", (*_F)->GetName());
                    return true;
                }
        }
    }
    return false;
}

void EScene::GenObjectName(ObjClassID cls_id, char *buffer, const char *pref)
{
    xr_string base_name = "";
    if (pref) {
        base_name = pref;
        if (base_name.length() == 0) {
            ESceneCustomOTool* ot = GetOTool(cls_id);
            VERIFY(ot);
            base_name = ot->ClassName();
        } else {
            size_t last_underscore = base_name.find_last_of('_');
            if (last_underscore != xr_string::npos && last_underscore < base_name.length() - 1) {
                bool is_num = true;
                for (size_t k = last_underscore + 1; k < base_name.length(); ++k) {
                    if (!isdigit(base_name[k])) {
                        is_num = false;
                        break;
                    }
                }
                if (is_num) {
                    base_name = base_name.substr(0, last_underscore);
                }
            }
        }
    }

    for (int i = 0; true; i++)
    {
        bool result;
        xr_string temp;
        if (!base_name.empty())
        {
            if (i == 0)
            {
                temp = base_name;                     
            }
            else            
                temp.sprintf("%s_%02d", base_name.c_str(), i - 1);            
        }
        else        
            temp.sprintf("%02d", i);        

        FindObjectByNameCB(temp.c_str(), result);

        if (!result)
        {
            xr_strcpy(buffer, 256, temp.c_str());
            return;
        }
    }
}
