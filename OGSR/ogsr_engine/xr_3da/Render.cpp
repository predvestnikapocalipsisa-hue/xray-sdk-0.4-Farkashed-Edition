#include "stdafx.h"
#include "render.h"

IRender_interface::~IRender_interface(){};

// ENGINE_API	IRender_interface*	Render		= NULL;

// resources
IRender_Light::~IRender_Light() { ::Render->light_destroy(this); }

// shader options
static thread_local bool m_hud_loading{false};
static thread_local s32 m_skinning{-1};

void IRender_interface::shader_option_skinning(s32 mode) { m_skinning = mode; }
s32 IRender_interface::shader_option_skinning() { return m_skinning; }

void IRender_interface::shader_option_hud_loading(bool t) { m_hud_loading = t; }
bool IRender_interface::shader_option_hud_loading() { return m_hud_loading; }

ENGINE_API ShExports shader_exports{};

ENGINE_API GRASS_SHADER_DATA grass_shader_data{};
ENGINE_API GRASS_SHADER_DATA_OLD grass_shader_data_old{};
