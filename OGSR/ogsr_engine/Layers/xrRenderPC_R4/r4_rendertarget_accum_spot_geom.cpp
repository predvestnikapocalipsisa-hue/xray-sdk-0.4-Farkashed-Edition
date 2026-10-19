#include "stdafx.h"

#include "../xrRender/du_cone.h"
#include "../xrRenderDX10/dx10BufferUtils.h"

void CRenderTarget::accum_spot_geom_create()
{
    //	u32	dwUsage				= D3DUSAGE_WRITEONLY;

    // vertices
    {
        const u32 vCount = DU_CONE_NUMVERTEX;
        const u32 vSize = 3 * 4;

        R_CHK(dx10BufferUtils::CreateVertexBuffer(&g_accum_spot_vb, du_cone_vertices, vCount * vSize));
    }

    // Indices
    {
        const u32 iCount = DU_CONE_NUMFACES * 3;

        R_CHK(dx10BufferUtils::CreateIndexBuffer(&g_accum_spot_ib, du_cone_faces, iCount * 2));
    }
}

void CRenderTarget::accum_spot_geom_destroy()
{
#ifdef DEBUG
    _SHOW_REF("g_accum_spot_ib", g_accum_spot_ib);
#endif // DEBUG
    _RELEASE(g_accum_spot_ib);
#ifdef DEBUG
    _SHOW_REF("g_accum_spot_vb", g_accum_spot_vb);
#endif // DEBUG
    _RELEASE(g_accum_spot_vb);
}