#include "stdafx.h"
#include "bolt.h"
#include "ParticlesObject.h"
#include "PhysicsShell.h"
#include "xr_level_controller.h"

#include "game_object_space.h"
#include "actor.h"
#include "script_callback_ex.h"
#include "script_game_object.h"
#include "../COMMON_AI/ai_sounds.h"

CBolt::CBolt(void)
{
    m_weight = .1f;
    SetSlot(BOLT_SLOT);
    m_flags.set(Fruck, FALSE);
    m_thrower_id = u16(-1);
}

CBolt::~CBolt() {
    HUD_SOUND::DestroySound(sndQuickThrowStart);
    HUD_SOUND::DestroySound(sndQuickThrow);
    HUD_SOUND::DestroySound(sndQuickThrowEnd);
}

void CBolt::Load(LPCSTR section) {

    inherited::Load(section);

    m_bQuickThrow = READ_IF_EXISTS(pSettings, r_bool, section, "quick_throw_use", false);
    if (m_bQuickThrow)
    {
        if (pSettings->line_exist(section, "snd_quick_throw_start"))
            HUD_SOUND::LoadSound(section, "snd_quick_throw_start", sndQuickThrowStart, SOUND_TYPE_WEAPON_EMPTY_CLICKING);
        if (pSettings->line_exist(section, "snd_quick_throw"))
            HUD_SOUND::LoadSound(section, "snd_quick_throw", sndQuickThrow, SOUND_TYPE_WEAPON_SHOOTING);
        if (pSettings->line_exist(section, "snd_quick_throw_end"))
            HUD_SOUND::LoadSound(section, "snd_quick_throw_end", sndQuickThrowEnd, SOUND_TYPE_WEAPON_EMPTY_CLICKING);

        m_vQuickThrowPoint = READ_IF_EXISTS(pSettings, r_fvector3, section, "quick_throw_point", m_vThrowPoint);
        m_sQuickThrowPointBoneName = READ_IF_EXISTS(pSettings, r_string, section, "quick_throw_point_bone", m_sThrowPointBoneName);
    }
}

void CBolt::OnH_A_Chield()
{
    inherited::OnH_A_Chield();
    CObject* o = H_Parent()->H_Parent();
    if (o)
        SetInitiator(o->ID());
}

void CBolt::OnEvent(NET_Packet& P, u16 type) { inherited::OnEvent(P, type); }

bool CBolt::Activate(bool now)
{
    m_bQuickThrowProsess = false;
    m_bThrowPointUpdated = false;

    Show(now);
    return true;
}

void CBolt::Deactivate(bool now) { 
    m_bQuickThrowProsess = false;
    m_bThrowPointUpdated = false;

    Hide(now || (GetState() == eThrowStart || GetState() == eReady || GetState() == eThrow)); 
}

void CBolt::Throw()
{
    CMissile* l_pBolt = smart_cast<CMissile*>(m_fake_missile);
    if (!l_pBolt)
        return;
    l_pBolt->set_destroy_time(u32(m_dwDestroyTimeMax / phTimefactor));
    inherited::Throw();
    spawn_fake_missile();
    if (g_actor)
        g_actor->callback(GameObject::eOnActorBoltThrow)(this->lua_game_object());
}

bool CBolt::Useful() const { return false; }

bool CBolt::Action(s32 cmd, u32 flags)
{
    if (cmd == kWPN_FIRE && (flags & CMD_START) && GetState() == eIdle)
        if (m_bQuickThrow)
            m_bQuickThrowProsess = true;

    if (inherited::Action(cmd, flags))
        return true;
    return false;
}

void CBolt::Destroy() { inherited::Destroy(); }

void CBolt::activate_physic_shell()
{
    inherited::activate_physic_shell();
    m_pPhysicsShell->SetAirResistance(.0001f);
}

void CBolt::SetInitiator(u16 id) { m_thrower_id = id; }

u16 CBolt::Initiator() { return m_thrower_id; }

void CBolt::GetBriefInfo(xr_string& str_name, xr_string& icon_sect_name, xr_string& str_count)
{
    str_name = NameShort();
    str_count = "";
    icon_sect_name = *cNameSect();
}

void CBolt::State(u32 state, u32 oldState) {
    if (state == eHiding || state == eShowing || state == eIdle)
        m_bQuickThrowProsess = false;

    inherited::State(state, oldState);
}

void CBolt::OnAnimationEnd(u32 state) { 
    if (state == eThrowEnd)
        m_bQuickThrowProsess = false;

    inherited::OnAnimationEnd(state);
}

//-- little better then setup_throw_params override
bool CBolt::g_ThrowPointParams(Fvector& FirePos, Fvector& FireDir)
{
    const Fvector tp_saved = m_vThrowPoint;
    const shared_str tp_bone_saved = m_sThrowPointBoneName;

    if (m_bQuickThrowProsess)
    {
        m_vThrowPoint = m_vQuickThrowPoint;
        m_sThrowPointBoneName = m_sQuickThrowPointBoneName;
    }
    bool res = inherited::g_ThrowPointParams(FirePos, FireDir);

    m_vThrowPoint = tp_saved;
    m_sThrowPointBoneName = tp_bone_saved;

    return res;
}

//-- motions
void CBolt::PlayAnimThrowStart()
{
    if (m_bQuickThrowProsess)
    {
        PlaySound(sndQuickThrowStart, Position());
        PlayHUDMotion({"anim_throw_quick_begin", "anm_throw_quick_begin"}, true, GetState());
    }
    else
    {
        inherited::PlayAnimThrowStart();
    }
}

void CBolt::PlayAnimThrow()
{
    if (m_bQuickThrowProsess)
    {
        PlaySound(sndQuickThrow, Position());
        PlayHUDMotion({"anim_throw_quick_act", "anm_throw_quick"}, true, GetState());
    }
    else
    {
        inherited::PlayAnimThrow();
    }
}

void CBolt::PlayAnimThrowEnd()
{
    if (m_bQuickThrowProsess)
    {
        PlaySound(sndQuickThrowEnd, Position());
        PlayHUDMotion({"anim_throw_quick_end", "anm_throw_quick_end"}, true, GetState());
    }
    else
    {
        inherited::PlayAnimThrowEnd();
    }
}

void CBolt::StopHUDSounds() { 

    HUD_SOUND::StopSound(sndQuickThrowStart);
    HUD_SOUND::StopSound(sndQuickThrow);
    HUD_SOUND::StopSound(sndQuickThrowEnd); 
    inherited::StopHUDSounds();
}

bool CBolt::UpdateHUDSounds() { 
     
    if (inherited::UpdateHUDSounds())
    {
        const Fvector& play_pos = Device.vCameraPosition;
        if (sndQuickThrowStart.playing())
            sndQuickThrowStart.set_position(play_pos);
        if (sndQuickThrow.playing())
            sndQuickThrow.set_position(play_pos);
        if (sndQuickThrowEnd.playing())
            sndQuickThrowEnd.set_position(play_pos);

        return true;
    }

    return false;
}
