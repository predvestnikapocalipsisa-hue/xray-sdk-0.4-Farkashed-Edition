#pragma once
#include "hud_item_object.h"
#include "HudSound.h"

struct dContact;
struct SGameMtl;

class CMissile : public CHudItemObject
{
    friend class CWeaponScript;
    typedef CHudItemObject inherited;

public:
    CMissile();
    virtual ~CMissile();

    virtual BOOL AlwaysTheCrow() { return TRUE; }
    virtual void OnDrawUI();

    virtual void reinit();
    virtual CMissile* cast_missile() { return this; }

    virtual void Load(LPCSTR section);
    virtual BOOL net_Spawn(CSE_Abstract* DC);
    virtual void net_Destroy();

    virtual void UpdateCL();
    virtual void shedule_Update(u32 dt);

    virtual void OnH_A_Chield();
    virtual void OnH_B_Independent(bool just_before_destroy);

    virtual void OnEvent(NET_Packet& P, u16 type);

    virtual void OnAnimationEnd(u32 state);
    virtual void OnMotionMark(u32 state, const motion_marks& M);

protected:
    virtual void DeviceUpdate() override;

public:
    virtual void Show(bool = false);
    virtual void Hide(bool = false);

    virtual void Throw();
    virtual void Destroy();

    virtual bool Action(s32 cmd, u32 flags);

    virtual void State(u32 state, u32 oldState);
    virtual void OnStateSwitch(u32 S, u32 oldState);
    virtual void PlayAnimIdle();
    virtual void PlayAnimThrowStart();
    virtual void PlayAnimThrow();
    virtual void PlayAnimThrowEnd();
    virtual void PlayAnimDeviceSwitch() override;
    virtual void GetBriefInfo(xr_string& str_name, xr_string& icon_sect_name, xr_string& str_count);

protected:
    virtual void UpdateXForm();
    void UpdatePosition(const Fmatrix& trans);
    void spawn_fake_missile();

    //инициализация если вещь в активном слоте или спрятана на OnH_B_Chield
    virtual void OnActiveItem();
    virtual void OnHiddenItem();

    //для сети
    virtual void net_Relcase(CObject* O);

protected:
    bool m_throw;

    bool HeadLampSwitch{}, NightVisionSwitch{};

    //время уничтожения
    u32 m_dwDestroyTime;
    u32 m_dwDestroyTimeMax;

    Fvector m_throw_direction;
    Fmatrix m_throw_matrix;

    // указатель на владельца RocketLauncher - который кидал гранату
    CGameObject* m_pOwner{};

    CMissile* m_fake_missile;

    //параметры броска

    float m_fMinForce, m_fConstForce, m_fMaxForce, m_fForceGrowSpeed;
    
    bool m_constpower;
    float m_fThrowForce;

    bool m_kick_on_explode{};
    bool m_explode_by_timer_on_safe_dist{true};

    float m_safe_dist_to_explode{};

    bool has_already_contact{};

    //относительная точка и направление вылета гранаты
    Fvector m_vThrowPoint;

    bool m_throwMotionMarksAvailable;

    //-- Sounds
protected:
    //звук анимации "играния"
    HUD_SOUND sndPlaying;
    HUD_SOUND sndItemOn;
    HUD_SOUND sndShow;
    HUD_SOUND sndHide;
    HUD_SOUND sndThrowStart;
    HUD_SOUND sndThrow;
    HUD_SOUND sndThrowEnd;

    u32 dwUpdateSounds_Frame{};
    virtual void StopHUDSounds() override;
    virtual bool UpdateHUDSounds();

private:
    void setup_throw_params();
   
public:
    bool m_bThrowPointUpdated{};
    shared_str m_sThrowPointBoneName{};
    virtual bool g_ThrowPointParams(Fvector& FirePos, Fvector& FireDir);

public:
    virtual void activate_physic_shell();
    virtual void setup_physic_shell();
    virtual void create_physic_shell();
    IC void set_destroy_time(u32 delta_destroy_time) { m_dwDestroyTime = delta_destroy_time + Device.dwTimeGlobal; }

protected:
    u32 m_ef_weapon_type;

public:
    virtual u32 ef_weapon_type() const;
    IC u32 destroy_time() const { return m_dwDestroyTime; };
    static void ExitContactCallback(bool& do_colide, bool bo1, dContact& c, SGameMtl* /*material_1*/, SGameMtl* /*material_2*/);
};
