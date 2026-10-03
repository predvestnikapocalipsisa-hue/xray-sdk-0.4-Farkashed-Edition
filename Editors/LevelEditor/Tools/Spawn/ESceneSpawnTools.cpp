#include "stdafx.h"

static HMODULE hXRSE_FACTORY = 0;

CEditableObject *ESceneSpawnTool::get_draw_visual(u8 _RP_TeamID, u8 _RP_Type, const GameTypeChooser &_GameType)
{
    if (Core.SocSdk)
        return nullptr;

    CEditableObject *ret = nullptr;

    if (m_draw_RP_visuals.empty())
    {
        m_draw_RP_visuals.push_back(Lib.CreateEditObject("editor\\artefakt_ah"));        // 0
        m_draw_RP_visuals.push_back(Lib.CreateEditObject("editor\\artefakt_cta_blue"));  // 1
        m_draw_RP_visuals.push_back(Lib.CreateEditObject("editor\\artefakt_cta_green")); // 2
        m_draw_RP_visuals.push_back(Lib.CreateEditObject("editor\\telo_ah_cta_blue"));   // 3
        m_draw_RP_visuals.push_back(Lib.CreateEditObject("editor\\telo_ah_cta_green"));  // 4
        m_draw_RP_visuals.push_back(Lib.CreateEditObject("editor\\telo_dm"));            // 5
        m_draw_RP_visuals.push_back(Lib.CreateEditObject("editor\\telo_tdm_blue"));      // 6
        m_draw_RP_visuals.push_back(Lib.CreateEditObject("editor\\telo_tdm_green"));     // 7
        m_draw_RP_visuals.push_back(Lib.CreateEditObject("editor\\spectator"));          // 8
        m_draw_RP_visuals.push_back(Lib.CreateEditObject("editor\\item_spawn"));         // 9
    }

    switch (_RP_Type)
    {
    case rptActorSpawn: // actor spawn
    {
        if (_GameType.MatchType(eGameIDDeathmatch))
        {
            if (_RP_TeamID == 0)
                ret = m_draw_RP_visuals[5];
        }

        if (_GameType.MatchType(eGameIDTeamDeathmatch))
        {
            if (_RP_TeamID == 2)
                ret = m_draw_RP_visuals[7];
            else if (_RP_TeamID == 1)
                ret = m_draw_RP_visuals[6];
        }

        if (_GameType.MatchType(eGameIDCaptureTheArtefact))
        {
            if (!_RP_TeamID)
                Msg("! incorrect ActorRP teamID [%d] for CTA", _RP_TeamID);
            else if (_RP_TeamID == 1)
                ret = m_draw_RP_visuals[4];
            else if (_RP_TeamID == 2)
                ret = m_draw_RP_visuals[3];
        }

        if (_GameType.MatchType(eGameIDArtefactHunt))
        {
            if (!_RP_TeamID)
                ret = m_draw_RP_visuals[8]; // spactator
            else if (_RP_TeamID == 1)
                ret = m_draw_RP_visuals[4];
            else if (_RP_TeamID == 2)
                ret = m_draw_RP_visuals[3];
        }
    }
    break;

    case rptArtefactSpawn: // AF spawn
    {
        if (_GameType.MatchType(eGameIDCaptureTheArtefact))
        {
            if (_RP_TeamID == 1)
                ret = m_draw_RP_visuals[2];
            else if (_RP_TeamID == 2)
                ret = m_draw_RP_visuals[1];
            else
                Msg("! incorrect AF teamID [%d] for CTA", _RP_TeamID);
        }
        else if (_GameType.MatchType(eGameIDArtefactHunt))
            ret = m_draw_RP_visuals[0];
    }
    break;

    case rptItemSpawn:
    {
        ret = m_draw_RP_visuals[9];
    }
    break;
    }

    return ret;
}

void FillSpawnItems(ChooseItemVec &lst, void *param)
{
    LPCSTR gcs = (LPCSTR)param;
    ObjectList objects;
    Scene->GetQueryObjects(objects, OBJCLASS_SPAWNPOINT, -1, -1, -1);

    xr_string itm;
    int cnt = _GetItemCount(gcs);
    for (int k = 0; k < cnt; k++)
    {
        _GetItem(gcs, k, itm);
        for (ObjectIt it = objects.begin(); it != objects.end(); it++)
            if ((*it)->OnChooseQuery(itm.c_str()))
                lst.push_back(SChooseItem((*it)->GetName(), ""));
    }
}

ESceneSpawnTool::ESceneSpawnTool() : ESceneCustomOTool(OBJCLASS_SPAWNPOINT)
{
    m_Flags.zero();
    UIChooseForm::AppendEvents(smSpawnItem, "Select Spawn Item", FillSpawnItems, 0, 0, 0, 0);
    m_Classes.clear();
    CInifile::Root const &data = pSettings->sections();
    for (CInifile::RootCIt it = data.begin(); it != data.end(); it++)
    {
        LPCSTR val;
        if ((*it)->line_exist("$spawn", &val))
        {
            CLASS_ID cls_id = pSettings->r_clsid((*it)->Name, "class");
            shared_str v = pSettings->r_string_wb((*it)->Name, "$spawn");
            m_Classes[cls_id].push_back(SChooseItem(*v, *(*it)->Name));
        }
    }
}

ESceneSpawnTool::~ESceneSpawnTool()
{
    FreeLibrary(hXRSE_FACTORY);
    m_Icons.clear();

    xr_vector<CEditableObject *>::iterator it = m_draw_RP_visuals.begin();
    xr_vector<CEditableObject *>::iterator it_e = m_draw_RP_visuals.end();
    for (; it != it_e; ++it)
    {
        Lib.RemoveEditObject(*it);
    }
    m_draw_RP_visuals.clear();
}

void ESceneSpawnTool::CreateControls()
{
    inherited::CreateDefaultControls(estDefault);
    AddControl(xr_new<TUI_ControlSpawnAdd>(estDefault, etaAdd, this));
    // frame

    pForm = xr_new<UISpawnTool>();
}
//----------------------------------------------------

void ESceneSpawnTool::RemoveControls()
{
    inherited::RemoveControls();
}
//----------------------------------------------------

void ESceneSpawnTool::FillProp(LPCSTR pref, PropItemVec &items)
{
    //.	PHelper().CreateFlag32(items, PrepareKey(pref,"Common\\Show Spawn Type"),	&m_Flags,		flShowSpawnType);
    //.	PHelper().CreateFlag32(items, PrepareKey(pref,"Common\\Trace Visibility"),	&m_Flags,		flPickSpawnType);
    inherited::FillProp(pref, items);
}
//------------------------------------------------------------------------------

ref_shader ESceneSpawnTool::CreateIcon(shared_str name)
{
    ref_shader S;
    if (pSettings->line_exist(name, "$ed_icon"))
    {
        LPCSTR tex_name = pSettings->r_string(name, "$ed_icon");
        S.create("editor\\spawn_icon", tex_name);
        m_Icons[name] = S;
    }
    else
    {
        S = 0;
    }
    return S;
}

ref_shader ESceneSpawnTool::GetIcon(shared_str name)
{
    ShaderPairIt it = m_Icons.find(name);
    if (it == m_Icons.end())
        return CreateIcon(name);
    else
        return it->second;
}

CCustomObject *ESceneSpawnTool::CreateObject(LPVOID data, LPCSTR name)
{
    CSpawnPoint *O = xr_new<CSpawnPoint>(data, name);
    O->FParentTools = this;

    if (data && name)
    {
        if (pSettings->line_exist((LPCSTR)data, "$def_sphere"))
        {
            float size = pSettings->r_float((LPCSTR)data, "$def_sphere");

            CCustomObject *S = Scene->GetOTool(OBJCLASS_SHAPE)->CreateObject(0, 0);
            CEditShape *shape = dynamic_cast<CEditShape *>(S);
            R_ASSERT(shape);

            Fsphere Sph;
            Sph.identity();
            Sph.R = size;
            shape->add_sphere(Sph);
            O->AttachObject(S);
        }
    }
    return O;
}
//----------------------------------------------------

int ESceneSpawnTool::MultiRenameObjects()
{
    int cnt = 0;
    for (ObjectIt o_it = m_Objects.begin(); o_it != m_Objects.end(); o_it++)
    {
        CCustomObject *obj = *o_it;
        if (obj->Selected())
        {
            string256 pref;
            strconcat(sizeof(pref), pref, Scene->LevelPrefix().c_str(), "_", obj->RefName());
            string256 buf;
            Scene->GenObjectName(obj->FClassID, buf, pref);
            if (xr_strcmp(obj->GetName(), buf) == 0)
            {
                obj->SetName(buf);
                cnt++;
            }
        }
    }
    return cnt;
}
/*
void ESceneSpawnTool::GetStaticDesc(int& v_cnt, int& f_cnt, bool b_selected_only)
{
    for (ObjectIt it=m_Objects.begin(); it!=m_Objects.end(); it++){
        CSceneObject* obj = (CSceneObject*)(*it);

        if(b_selected_only && !obj->Selected())
            continue;

        CSpawnPoint* sp = dynamic_cast<CSpawnPoint*>(obj);
        if(!sp){
            Msg("! ghm/ not a spawn object");
            continue;
        }
        if(sp->m_SpawnData.Valid())
        {
            f_cnt	+= obj->GetFaceCount();
            v_cnt	+= obj->GetVertexCount();
        }
    }
}

bool ESceneSpawnTool::ExportStatic(SceneBuilder* B, bool b_selected_only)
{
    return B->ParseStaticObjects(m_Objects, NULL, b_selected_only);
}
*/

#include <map>

int ESceneSpawnTool::GenerateGraphPoints(float spacing, int mode, bool clear_existing)
{
    if (spacing < 1.0f)
        spacing = 1.0f;

    xr_vector<Fvector> existing_pts;
    ObjectList obj_list;
    if (Scene->GetQueryObjects(obj_list, OBJCLASS_SPAWNPOINT, -1, -1, 0))
    {
        for (CCustomObject* obj : obj_list)
        {
            CSpawnPoint* sp = dynamic_cast<CSpawnPoint*>(obj);
            if (sp && sp->RefCompare("graph_point"))
            {
                if (clear_existing)
                {
                    Scene->RemoveObject(sp, false, true);
                    xr_delete(sp);
                }
                else
                {
                    existing_pts.push_back(sp->GetPosition());
                }
            }
        }
    }

    ESceneAIMapTool* ai_tool = dynamic_cast<ESceneAIMapTool*>(Scene->GetOTool(OBJCLASS_AIMAP));
    bool use_ai_nodes = false;
    if (mode == 1) // Forced AI Map
    {
        use_ai_nodes = true;
    }
    else if (mode == 0) // Auto
    {
        if (ai_tool && !ai_tool->Nodes().empty())
            use_ai_nodes = true;
    }

    int generated_count = 0;

    if (use_ai_nodes)
    {
        if (!ai_tool || ai_tool->Nodes().empty())
        {
            ELog.DlgMsg(mtError, "AI Map has no nodes. Generate AI Map first or use 'From Scene Geometry' mode.");
            return 0;
        }

        const AINodeVec& nodes = ai_tool->Nodes();
        float y_step = 4.0f;

        struct GridKey
        {
            int x, y, z;
            bool operator<(const GridKey& o) const
            {
                if (x != o.x) return x < o.x;
                if (y != o.y) return y < o.y;
                return z < o.z;
            }
        };

        struct Candidate
        {
            SAINode* node;
            float dist_sq;
        };

        std::map<GridKey, Candidate> grid_map;

        for (SAINode* n : nodes)
        {
            if (!n) continue;

            int ix = iFloor(n->Pos.x / spacing);
            int iy = iFloor(n->Pos.y / y_step);
            int iz = iFloor(n->Pos.z / spacing);

            GridKey key = { ix, iy, iz };

            Fvector target_center;
            target_center.x = (float(ix) + 0.5f) * spacing;
            target_center.y = float(iy) * y_step + (y_step * 0.5f);
            target_center.z = (float(iz) + 0.5f) * spacing;

            float d2 = n->Pos.distance_to_sqr(target_center);

            auto it = grid_map.find(key);
            if (it == grid_map.end() || d2 < it->second.dist_sq)
            {
                grid_map[key] = { n, d2 };
            }
        }

        float min_dist_sq = (spacing * 0.7f) * (spacing * 0.7f);

        for (auto& pair : grid_map)
        {
            SAINode* n = pair.second.node;
            if (!n) continue;

            Fvector pos = n->Pos;
            pos.y += 0.1f;

            bool too_close = false;
            for (const Fvector& ep : existing_pts)
            {
                if (pos.distance_to_sqr(ep) < min_dist_sq)
                {
                    too_close = true;
                    break;
                }
            }
            if (too_close) continue;

            string256 namebuf;
            Scene->GenObjectName(OBJCLASS_SPAWNPOINT, namebuf, "graph_point");

            CCustomObject* obj = CreateObject((void*)"graph_point", namebuf);
            if (obj)
            {
                obj->SetPosition(pos);
                Scene->AppendObject(obj, false);
                existing_pts.push_back(pos);
                generated_count++;
            }
        }
    }
    else // From Scene Geometry Grid
    {
        Fbox bb;
        Scene->GetBox(bb, OBJCLASS_SCENEOBJECT);
        {
            Fvector sz; bb.getsize(sz);
            if (!bb.is_valid() || sz.magnitude() < 0.1f)
                Scene->GetBox(bb, OBJCLASS_DUMMY);
        }

        {
            Fvector sz; bb.getsize(sz);
            if (!bb.is_valid() || sz.magnitude() < 0.1f)
            {
                ELog.DlgMsg(mtError, "Scene geometry bounding box is invalid or empty.");
                return 0;
            }
        }

        float min_dist_sq = (spacing * 0.7f) * (spacing * 0.7f);
        float ray_start_y = bb.max.y + 20.0f;
        float ray_dir_y = -1.0f;

        for (float x = bb.min.x + spacing * 0.5f; x <= bb.max.x; x += spacing)
        {
            for (float z = bb.min.z + spacing * 0.5f; z <= bb.max.z; z += spacing)
            {
                Fvector start; start.set(x, ray_start_y, z);
                Fvector dir;   dir.set(0.0f, ray_dir_y, 0.0f);
                Fvector hit_pt;
                Fvector hit_normal;

                if (LUI->PickGround(hit_pt, start, dir, -1, &hit_normal))
                {
                    if (hit_normal.y < 0.5f)
                        continue;

                    hit_pt.y += 0.1f;

                    bool too_close = false;
                    for (const Fvector& ep : existing_pts)
                    {
                        if (hit_pt.distance_to_sqr(ep) < min_dist_sq)
                        {
                            too_close = true;
                            break;
                        }
                    }
                    if (too_close) continue;

                    string256 namebuf;
                    Scene->GenObjectName(OBJCLASS_SPAWNPOINT, namebuf, "graph_point");

                    CCustomObject* obj = CreateObject((void*)"graph_point", namebuf);
                    if (obj)
                    {
                        obj->SetPosition(hit_pt);
                        Scene->AppendObject(obj, false);
                        existing_pts.push_back(hit_pt);
                        generated_count++;
                    }
                }
            }
        }
    }

    if (generated_count > 0 || clear_existing)
    {
        Scene->UndoSave();
        ExecCommand(COMMAND_UPDATE_PROPERTIES);
        UI->RedrawScene();
    }

    return generated_count;
}
