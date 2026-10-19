#define USE_TDETAIL

#include "common.h"

v2p_bumped main(v_static I)
{
    float4 w_pos = I.P;
    float2 tc = unpack_tc_base(I.tc, I.T.w, I.B.w); // copy tc
    float hemi = I.Nh.w;

    // Eye-space pos/normal
    v2p_bumped O;
    float3 Pe = mul(m_WV, w_pos);
    O.hpos = mul(m_WVP, w_pos);
    O.hpos_curr = O.hpos;
    O.hpos_old = mul(m_WVP_old, w_pos);
    O.tcdh = float4(tc.xyyy);
    O.position = float4(Pe, hemi);
    O.hpos.xy = get_taa_jitter(O.hpos);

    // Calculate the 3x3 transform from tangent space to eye-space
    I.Nh = unpack_D3DCOLOR(I.Nh);
    I.T = unpack_D3DCOLOR(I.T);
    I.B = unpack_D3DCOLOR(I.B);
    float3 N = unpack_bx4(I.Nh); // just scale (assume normal in the -.5f, .5f)
    float3 T = unpack_bx4(I.T); //
    float3 B = unpack_bx4(I.B); //
    float3x3 xform = mul((float3x3)m_WV, float3x3(T.x, B.x, N.x, T.y, B.y, N.y, T.z, B.z, N.z));

    // Feed this transform to pixel shader
    O.M1 = xform[0];
    O.M2 = xform[1];
    O.M3 = xform[2];

#ifdef USE_TDETAIL
    O.tcdbump = O.tcdh * dt_params; // dt tc
#endif

#ifdef USE_LM_HEMI
    O.lmh = unpack_tc_lmap(I.lmh);
#endif
    return O;
}
