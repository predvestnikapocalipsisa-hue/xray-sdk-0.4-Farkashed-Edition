////////////////////////////////////////////////////////////////////////////
//	Module 		: debug_renderer.h
//	Created 	: 19.06.2006
//  Modified 	: 19.06.2006
//	Author		: Dmitriy Iassenev
//	Description : debug renderer
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "../Include/xrRender/DebugRender.h"

//-- static debug items for actions like missiles throw, shots, etc., that don't need per-frame update
struct CStaticDebugItem
{
    u32 mColor{D3DCOLOR_XRGB(255, 0, 0)}; // red
    u32 mTextColor{D3DCOLOR_XRGB(255, 255, 255)}; // white
    shared_str mText{};
    float mNameDist{5.f};
    u32 mDrawMask{0xFFFFFFFF};
    //u32 mLifeTimeMs{0}; // 0 - infinite
};

struct CStaticDebugLine : CStaticDebugItem
{
    Fvector mStartPos{};
    Fvector mEndPos{};
    shared_str mTextEnd{};
    u32 mTextEndColor{D3DCOLOR_XRGB(255, 255, 255)}; // white
};

struct CStaticDebugBox : CStaticDebugItem
{
    Fmatrix mXFORM{};
    Fvector mHalfSize{};
};

struct CStaticDebugSphere : CStaticDebugItem
{
    Fmatrix mXFORM{};
    // float radius{1.f};
};

class CDebugRenderer
{
private:
    void add_lines(Fvector const* vertices, u32 const& vertex_count, u16 const* pairs, u32 const& pair_count, u32 const& color, bool hud_mode);

public:
    IC void render();

public:
    IC void draw_line(const Fmatrix& matrix, const Fvector& vertex0, const Fvector& vertex1, const u32& color, bool hud_mode = false);
    IC void draw_aabb(const Fvector& center, const float& half_radius_x, const float& half_radius_y, const float& half_radius_z, const u32& color, bool hud_mode = false);
    void draw_obb(const Fmatrix& matrix, const u32& color, bool hud_mode = false);
    void draw_obb(const Fmatrix& matrix, const Fvector& half_size, const u32& color, bool hud_mode = false);
    void draw_ellipse(const Fmatrix& matrix, const u32& color, bool hud_mode = false);

private:
    xr_vector<CStaticDebugLine> m_StaticLines;
    xr_vector<CStaticDebugBox> m_StaticBoxes;
    xr_vector<CStaticDebugSphere> m_StaticSpheres;

    //-- for drawing larger amount of static debug items with same draw mask
    u32 m_StaticDrawFlag{0xFFFFFFFF}; //-- draw always
public:
    
    void ClearDebugContainer()
    {
        m_StaticLines.clear();
        m_StaticBoxes.clear();
        m_StaticSpheres.clear();
        SetStaticDrawFlagDefault();
    }
    void SetStaticDrawFlag(u32 flag) { m_StaticDrawFlag = flag; }
    void SetStaticDrawFlagDefault() { m_StaticDrawFlag = 0xFFFFFFFF; }

    void add_static_line(CStaticDebugLine& line);
    void add_static_line(const Fvector& pos_start, const Fvector& pos_end, u32 color = D3DCOLOR_XRGB(255, 0, 0));
    void add_static_box(CStaticDebugBox& box);
    void add_static_box(const Fmatrix& xform, const Fvector& half_size, u32 color = D3DCOLOR_XRGB(255, 0, 0), shared_str text = "");
    void add_static_sphere(CStaticDebugSphere& sphere);
    void add_static_sphere(const Fmatrix& xform, u32 color = D3DCOLOR_XRGB(255, 0, 0), shared_str text = "");
    
    //-- render static items
    void OnRenderStaticItems();
};

#include "debug_renderer_inline.h"
