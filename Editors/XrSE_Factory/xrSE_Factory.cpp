////////////////////////////////////////////////////////////////////////////
//	Module 		: xrSE_Factory.cpp
//	Created 	: 18.06.2004
//  Modified 	: 18.06.2004
//	Author		: Dmitriy Iassenev
//	Description : Precompiled header creatore
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "xrSE_Factory.h"
#include "ai_space.h"
#include "object_factory.h"
#include "xrSE_Factory_import_export.h"
#include "script_properties_list_helper.h"

#include "character_info.h"
#include "specific_character.h"
#include "CompiledLevelConfig.h"
#include "xrServer_Objects_ALife_All.h"
#include "xrServer_Objects_Alife_Smartcovers.h"
#include "spawn_packet_format.h"
//#include "character_community.h"
//#include "monster_community.h"
//#include "character_rank.h"
//#include "character_reputation.h"

//#include "..\External\LuaJIT\src"
//#include <lua/library_linkage.h>
//#include <luabind/library_linkage.h>

#ifndef XR_SDK_RUNTIME_BUILD
#pragma comment(lib, "SDKRuntime.lib")
#endif
#pragma comment(lib, "luajit.lib")
#pragma comment(lib, "luabind.lib")

extern CSE_Abstract *F_entity_Create(LPCSTR section);

extern CScriptPropertiesListHelper *g_property_list_helper;

#ifdef NDEBUG

#endif // #ifdef NDEBUG

void setup_luabind_allocator();
namespace
{
struct CompiledPacketScope
{
	bool previous = compiledCopSpawnPacket;
	CompiledPacketScope() { compiledCopSpawnPacket = true; }
	~CompiledPacketScope() { compiledCopSpawnPacket = previous; }
};

template <class Entity>
CSE_Abstract* MakeCompiledEntity(LPCSTR section, CLASS_ID id)
{
	auto* entity = xr_new<Entity>(section);
	entity->m_tClassID = id;
	return entity->init();
}

using CompiledEntityConstructor = CSE_Abstract* (*)(LPCSTR, CLASS_ID);
struct CompiledEntityType
{
	CLASS_ID id;
	CompiledEntityConstructor construct;
	bool hasScriptState;
};

const CompiledEntityType* CompiledType(CLASS_ID id)
{
	// Monolith aliases for native CoP types, including the script classes
	// whose editable state precedes their private gameplay state.
	static const CompiledEntityType types[] = {
		{TEXT2CLSID("SMRTTRRN"), MakeCompiledEntity<CSE_ALifeSmartZone>, true},
		{TEXT2CLSID("RE_SPAWN"), MakeCompiledEntity<CSE_ALifeSmartZone>, true},
		{TEXT2CLSID("SPC_RS_S"), MakeCompiledEntity<CSE_ALifeSpaceRestrictor>, true},
		{TEXT2CLSID("O_PHYS_S"), MakeCompiledEntity<CSE_ALifeObjectPhysic>, true},
		{TEXT2CLSID("O_DSTR_S"), MakeCompiledEntity<CSE_ALifeObjectPhysic>, true},
		{TEXT2CLSID("SO_HLAMP"), MakeCompiledEntity<CSE_ALifeObjectHangingLamp>, true},
		{TEXT2CLSID("SMRT_C_S"), MakeCompiledEntity<CSE_SmartCover>, true},
		{TEXT2CLSID("LVL_CHNG"), MakeCompiledEntity<CSE_ALifeLevelChanger>, true},
		{TEXT2CLSID("S_ACTOR"), MakeCompiledEntity<CSE_ALifeCreatureActor>, true},
		{TEXT2CLSID("AI_STL_S"), MakeCompiledEntity<CSE_ALifeHumanStalker>, true},
		{TEXT2CLSID("AI_TRD_S"), MakeCompiledEntity<CSE_ALifeTrader>, true},
		{TEXT2CLSID("C_HLCP_S"), MakeCompiledEntity<CSE_ALifeHelicopter>, true},
		{TEXT2CLSID("SCRPTCAR"), MakeCompiledEntity<CSE_ALifeCar>, true},
		{TEXT2CLSID("ON_OFF_S"), MakeCompiledEntity<CSE_ALifeOnlineOfflineGroup>, true},
		{TEXT2CLSID("ZS_BFUZZ"), MakeCompiledEntity<CSE_ALifeZoneVisual>, true},
		{TEXT2CLSID("ZS_AMEBA"), MakeCompiledEntity<CSE_ALifeZoneVisual>, true},
		{TEXT2CLSID("ZS_MBALD"), MakeCompiledEntity<CSE_ALifeAnomalousZone>, true},
		{TEXT2CLSID("ZS_GALAN"), MakeCompiledEntity<CSE_ALifeAnomalousZone>, true},
		{TEXT2CLSID("ZS_MINCE"), MakeCompiledEntity<CSE_ALifeAnomalousZone>, true},
		{TEXT2CLSID("ZS_RADIO"), MakeCompiledEntity<CSE_ALifeAnomalousZone>, true},
		{TEXT2CLSID("ZS_NGRAV"), MakeCompiledEntity<CSE_ALifeAnomalousZone>, true},
		{TEXT2CLSID("ZS_TORRD"), MakeCompiledEntity<CSE_ALifeTorridZone>, true},
		{TEXT2CLSID("Z_CFIRE"), MakeCompiledEntity<CSE_ALifeAnomalousZone>, false},
		{TEXT2CLSID("S_EXPLO"), MakeCompiledEntity<CSE_ALifeItemExplosive>, true},
		{TEXT2CLSID("SCRPTART"), MakeCompiledEntity<CSE_ALifeItemArtefact>, true},
		{TEXT2CLSID("II_ATTCH"), MakeCompiledEntity<CSE_ALifeItem>, true},
		{TEXT2CLSID("E_HLMET"), MakeCompiledEntity<CSE_ALifeItemHelmet>, true},
		{TEXT2CLSID("E_STLK"), MakeCompiledEntity<CSE_ALifeItemCustomOutfit>, true},
		{TEXT2CLSID("EQU_STLK"), MakeCompiledEntity<CSE_ALifeItemCustomOutfit>, true},
		{TEXT2CLSID("TORCH_S"), MakeCompiledEntity<CSE_ALifeItemTorch>, true},
		{TEXT2CLSID("S_PDA"), MakeCompiledEntity<CSE_ALifeItemPDA>, true},
		{TEXT2CLSID("S_FOOD"), MakeCompiledEntity<CSE_ALifeItem>, true},
		{TEXT2CLSID("WP_AK74"), MakeCompiledEntity<CSE_ALifeItemWeaponMagazinedWGL>, true},
		{TEXT2CLSID("WP_GROZA"), MakeCompiledEntity<CSE_ALifeItemWeaponMagazinedWGL>, true},
		{TEXT2CLSID("WP_LR300"), MakeCompiledEntity<CSE_ALifeItemWeaponMagazined>, true},
		{TEXT2CLSID("WP_PM"), MakeCompiledEntity<CSE_ALifeItemWeaponMagazined>, true},
		{TEXT2CLSID("WP_HPSA"), MakeCompiledEntity<CSE_ALifeItemWeaponMagazined>, true},
		{TEXT2CLSID("WP_RPG7"), MakeCompiledEntity<CSE_ALifeItemWeaponMagazined>, true},
		{TEXT2CLSID("WP_SVD"), MakeCompiledEntity<CSE_ALifeItemWeaponMagazined>, true},
		{TEXT2CLSID("WP_SVU"), MakeCompiledEntity<CSE_ALifeItemWeaponMagazined>, true},
		{TEXT2CLSID("WP_VAL"), MakeCompiledEntity<CSE_ALifeItemWeaponMagazined>, true},
		{TEXT2CLSID("WP_USP45"), MakeCompiledEntity<CSE_ALifeItemWeaponMagazined>, true},
		{TEXT2CLSID("WP_BM16"), MakeCompiledEntity<CSE_ALifeItemWeaponShotGun>, true},
		{TEXT2CLSID("WP_RG6"), MakeCompiledEntity<CSE_ALifeItemWeaponShotGun>, true},
		{TEXT2CLSID("WP_BINOC"), MakeCompiledEntity<CSE_ALifeItemWeaponMagazined>, true},
		{TEXT2CLSID("WP_KNIFE"), MakeCompiledEntity<CSE_ALifeItemWeapon>, true},
		{TEXT2CLSID("WP_VINT"), MakeCompiledEntity<CSE_ALifeItemWeaponMagazined>, true},
		{TEXT2CLSID("WP_WALTH"), MakeCompiledEntity<CSE_ALifeItemWeaponMagazined>, true},
		{TEXT2CLSID("WP_SHOTG"), MakeCompiledEntity<CSE_ALifeItemWeaponShotGun>, true},
		{TEXT2CLSID("WP_ASHTG"), MakeCompiledEntity<CSE_ALifeItemWeaponAutoShotGun>, true},
		{TEXT2CLSID("WP_SCOPE"), MakeCompiledEntity<CSE_ALifeItem>, true},
		{TEXT2CLSID("WP_SILEN"), MakeCompiledEntity<CSE_ALifeItem>, true},
		{TEXT2CLSID("WP_GLAUN"), MakeCompiledEntity<CSE_ALifeItem>, true},
		{TEXT2CLSID("AMMO_S"), MakeCompiledEntity<CSE_ALifeItemAmmo>, true},
		{TEXT2CLSID("S_VOG25"), MakeCompiledEntity<CSE_ALifeItemAmmo>, true},
		{TEXT2CLSID("S_OG7B"), MakeCompiledEntity<CSE_ALifeItemAmmo>, true},
		{TEXT2CLSID("S_M209"), MakeCompiledEntity<CSE_ALifeItemAmmo>, true},
		{TEXT2CLSID("G_F1_S"), MakeCompiledEntity<CSE_ALifeItemGrenade>, true},
		{TEXT2CLSID("G_RGD5_S"), MakeCompiledEntity<CSE_ALifeItemGrenade>, true},
		{TEXT2CLSID("DET_SIMP"), MakeCompiledEntity<CSE_ALifeItemDetector>, true},
		{TEXT2CLSID("DET_ADVA"), MakeCompiledEntity<CSE_ALifeItemDetector>, true},
		{TEXT2CLSID("DET_ELIT"), MakeCompiledEntity<CSE_ALifeItemDetector>, true},
		{TEXT2CLSID("DET_SCIE"), MakeCompiledEntity<CSE_ALifeItemDetector>, true},
		{TEXT2CLSID("S_INVBOX"), MakeCompiledEntity<CSE_ALifeInventoryBox>, true},
	};
	const auto* type = std::find_if(std::begin(types), std::end(types),
		[id](const auto& item) { return item.id == id; });
	if (type != std::end(types)) return type;

	static const char* const monsterClasses[] = {
		"SM_BLOOD", "SM_BOARW", "SM_DOG_S", "SM_FLESH", "SM_P_DOG", "SM_BURER",
		"SM_CAT_S", "SM_CHIMS", "SM_CONTR", "SM_IZLOM", "SM_POLTR", "SM_GIANT",
		"SM_ZOMBI", "SM_SNORK", "SM_TUSHK", "SM_RAT", "SM_DOG_P", "SM_DOG_F"
	};
	const auto monster = std::find_if(std::begin(monsterClasses), std::end(monsterClasses),
		[id](const char* name) { return TEXT2CLSID(name) == id; });
	static const CompiledEntityType monsterType{0, MakeCompiledEntity<CSE_ALifeMonsterBase>, true};
	return monster == std::end(monsterClasses) ? nullptr : &monsterType;
}
}
namespace XrSE_Factory
{
	FACTORY_API ISE_Abstract *create_entity(LPCSTR section)
	{
		return (F_entity_Create(section));
	}
	FACTORY_API ISE_Abstract* create_compiled_entity(LPCSTR section, CLASS_ID clsid)
	{
		if (!pSettings->line_exist(section, "class")) return nullptr;
		if (const auto* type = CompiledType(clsid)) return type->construct(section, clsid);
		auto* entity = object_factory().native_server_object(clsid, section);
		if (entity) entity->m_tClassID = clsid;
		return entity;
	}
	FACTORY_API BOOL read_compiled_entity(ISE_Abstract* entity, NET_Packet& packet)
	{
		CompiledPacketScope format;
		if (!entity->Spawn_Read(packet)) return FALSE;
		if (!packet.inistream && packet.r_tell() < packet.B.count)
		{
			const auto* type = CompiledType(static_cast<const CSE_Abstract*>(entity)->m_tClassID);
			if (type && type->hasScriptState) packet.r_advance(packet.B.count - packet.r_tell());
		}
		return TRUE;
	}
	FACTORY_API void write_compiled_entity(ISE_Abstract* entity, NET_Packet& packet)
	{
		CompiledPacketScope format;
		entity->Spawn_Write(packet, TRUE);
	}

	FACTORY_API void destroy_entity(ISE_Abstract *&abstract)
	{
		if (!abstract)
			return;
		CSE_Abstract *object = smart_cast<CSE_Abstract *>(abstract);
		F_entity_Destroy(object);
		abstract = 0;
	}
	FACTORY_API void initialize()
	{
		string_path SYSTEM_LTX;
		FS.update_path(SYSTEM_LTX, "$game_config$", "system.ltx");
		pSettings = xr_new<CInifile>(SYSTEM_LTX);
		InitializeCompiledLevelConfig();

		setup_luabind_allocator();

		if (!Core.DebugMode)
        {
            CCharacterInfo::InitInternal();
            CSpecificCharacter::InitInternal();
        }
        else
            Msg("! [SDK DEBUG] Skipping character profile initialization.");
	}
	FACTORY_API void destroy()
	{
		DestroyCompiledLevelConfig();
		CCharacterInfo::DeleteSharedData();
		CCharacterInfo::DeleteIdToIndexData();
		CSpecificCharacter::DeleteSharedData();
		CSpecificCharacter::DeleteIdToIndexData();

		xr_delete(g_object_factory);
		CInifile **s = (CInifile **)(&pSettings);
		xr_delete(*s);
		xr_delete(g_property_list_helper);
		xr_delete(g_ai_space);
		xr_delete(g_object_factory);
	}
}
// typedef void DUMMY_STUFF (const void*,const u32&,void*);
// XRCORE_API DUMMY_STUFF	*g_temporary_stuff;

//#define TRIVIAL_ENCRYPTOR_DECODER
//#include UP(xrEngine/trivial_encryptor.h)

void _destroy_item_data_vector_cont(T_VECTOR *vec)
{
	T_VECTOR::iterator it = vec->begin();
	T_VECTOR::iterator it_e = vec->end();

	xr_vector<CUIXml *> _tmp;
	for (; it != it_e; ++it)
	{
		xr_vector<CUIXml *>::iterator it_f = std::find(_tmp.begin(), _tmp.end(), (*it)._xml);
		if (it_f == _tmp.end())
			_tmp.push_back((*it)._xml);
	}
	delete_data(_tmp);
}
