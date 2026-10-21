////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_surge_manager.cpp
//	Created 	: 25.12.2002
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife Simulator surge manager
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "alife_surge_manager.h"
#include "alife_object_registry.h"
#include "alife_spawn_registry.h"
#include "alife_time_manager.h"
#include "alife_graph_registry.h"
#include "alife_schedule_registry.h"
#include "alife_simulator_header.h"
#include "ai_space.h"
#include "ef_storage.h"
#include "ef_pattern.h"
#include "graph_engine.h"
#include "xrserver.h"
#include "alife_human_brain.h"
#include "../xr_3da/SDKPreview.h"
#include "xrServer_Objects_ALife_Monsters.h"

using namespace ALife;

CALifeSurgeManager::~CALifeSurgeManager() {}

void CALifeSurgeManager::spawn_new_spawns()
{
    xr_vector<ALife::_SPAWN_ID>::const_iterator I = m_temp_spawns.begin();
    xr_vector<ALife::_SPAWN_ID>::const_iterator E = m_temp_spawns.end();
    for (; I != E; ++I)
    {
        CSE_ALifeDynamicObject *object, *spawn = smart_cast<CSE_ALifeDynamicObject*>(&spawns().spawns().vertex(*I)->data()->object());
        VERIFY3(spawn, spawns().spawns().vertex(*I)->data()->object().name(), spawns().spawns().vertex(*I)->data()->object().name_replace());
        if (SDKPlayRequested())
        {
            auto* actor = smart_cast<CSE_ALifeCreatureActor*>(spawn);
            if (!actor) continue;
            SDKConfigureActorSpawn(*actor);
            Fvector angles;
            SDKPlaySpawn(actor->o_Position, angles);
            actor->o_torso.yaw = -angles.x;
            actor->o_torso.pitch = angles.y;
            actor->m_flags.set(CSE_ALifeObject::flUsedAI_Locations, FALSE);
        }

#ifdef DEBUG
        CTimer timer;
        timer.Start();
#endif
        create(object, spawn, *I);
        if (SDKPlayRequested())
        {
            // Populate the actor before it switches online. Its ordinary
            // add_online path sends both the actor and owned inventory items.
            const char* equipment[] = {"wpn_knife", "wpn_pm", "ammo_9x18_fmj", "ammo_9x18_fmj",
                "wpn_ak74", "ammo_5.45x39_fmj", "ammo_5.45x39_fmj", "novice_outfit", "device_torch", "device_pda", "medkit", "bandage", "bread"};
            for (const char* section : equipment)
            {
                R_ASSERT3(pSettings->section_exist(section), "Missing SDK actor equipment section", section);
                spawn_item(section, object->o_Position, object->m_tNodeID, object->m_tGraphID, object->ID);
            }
            Msg("SDK actor: native ALife start equipment created before online switch");
            SDKImportSceneSpawns();
        }
#ifdef DEBUG
        if (psAI_Flags.test(aiALife))
            Msg("LSS : SURGE : SPAWN : [%s],[%s], level %s, time %f ms", *spawn->s_name, spawn->name_replace(),
                *ai().game_graph().header().level(ai().game_graph().vertex(spawn->m_tGraphID)->level_id()).name(), timer.GetElapsed_sec() * 1000.f);
#endif
    }
}

void CALifeSurgeManager::spawn_new_objects()
{
    spawns().fill_new_spawns(m_temp_spawns, time_manager().game_time());
    spawn_new_spawns();
    VERIFY(graph().actor());
}
