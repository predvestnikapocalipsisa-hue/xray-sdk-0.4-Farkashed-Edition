// FVisual.cpp: implementation of the FVisual class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "../../xr_3da/fmesh.h"
#include "fvisual.h"

#include "../xrRenderDX10/dx10BufferUtils.h"

#include <Utilities\FlexibleVertexFormat.h>

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

Fvisual::Fvisual() : dxRender_Visual() { m_fast = nullptr; }

Fvisual::~Fvisual()
{
    HW.stats_manager.decrement_stats_vb(p_rm_Vertices);
    HW.stats_manager.decrement_stats_ib(p_rm_Indices);

    xr_delete(m_fast);
}

void Fvisual::Release() { dxRender_Visual::Release(); }

void Fvisual::Load(const char* N, IReader* data, u32 dwFlags)
{
    dxRender_Visual::Load(N, data, dwFlags);

    auto dcl = std::vector<D3DVERTEXELEMENT9>(MAXD3DDECLLENGTH + 1);
    D3DVERTEXELEMENT9* vFormat = nullptr;
    dwPrimitives = 0;
    BOOL loaded_v = false;

    if (data->find_chunk(OGF_GCONTAINER))
    {
        // verts
        u32 ID = data->r_u32();
        vBase = data->r_u32();
        vCount = data->r_u32();

        vFormat = RImplementation.getVB_Format(ID);

        VERIFY(NULL == p_rm_Vertices);

        p_rm_Vertices = RImplementation.getVB(ID);
        p_rm_Vertices->AddRef();

        // indices
        ID = data->r_u32();
        iBase = data->r_u32();
        iCount = data->r_u32();

        dwPrimitives = iCount / 3;

        VERIFY(NULL == p_rm_Indices);
        p_rm_Indices = RImplementation.getIB(ID);
        p_rm_Indices->AddRef();

        loaded_v = true;

        // check for fast-vertices
        if (data->find_chunk(OGF_FASTPATH))
        {
            destructor<IReader> geomdef(data->open_chunk(OGF_FASTPATH));
            destructor<IReader> def(geomdef().open_chunk(OGF_GCONTAINER));

            // we have fast-mesh
            m_fast = xr_new<IRender_Mesh>();

            // verts
            ID = def().r_u32();
            m_fast->vBase = def().r_u32();
            m_fast->vCount = def().r_u32();

            VERIFY(NULL == m_fast->p_rm_Vertices);
            m_fast->p_rm_Vertices = RImplementation.getVB(ID, true);
            m_fast->p_rm_Vertices->AddRef();
            D3DVERTEXELEMENT9* fmt = RImplementation.getVB_Format(ID, true);

            // indices
            ID = def().r_u32();
            m_fast->iBase = def().r_u32();
            m_fast->iCount = def().r_u32();
            m_fast->dwPrimitives = m_fast->iCount / 3;

            VERIFY(NULL == m_fast->p_rm_Indices);
            m_fast->p_rm_Indices = RImplementation.getIB(ID, true);
            m_fast->p_rm_Indices->AddRef();

            // geom
            m_fast->rm_geom.create(fmt, m_fast->p_rm_Vertices, m_fast->p_rm_Indices);
        }
    }

    if (!loaded_v)
    {
        // read vertices
        if ((dwFlags & VLOAD_NOVERTICES) == 0)
        {
            if (data->find_chunk(OGF_VCONTAINER))
            {
                R_ASSERT(0, "pls notify andy about this.");
            }
            else
            {
                R_ASSERT(data->find_chunk(OGF_VERTICES));
                vBase = 0;
                const u32 fvf = data->r_u32();
                R_ASSERT(FVF::CreateDeclFromFVF(fvf, dcl));
                vFormat = dcl.data();
                vCount = data->r_u32();
                const u32 vStride = FVF::ComputeVertexSize(fvf);

                VERIFY(NULL == p_rm_Vertices);
                if (N && strncmp(N, "sdk_preview_mesh_", 17) == 0 && fvf == 0x112)
                {
                    // The SDK exports XYZ/NORMAL/UV; modern deferred model
                    // shaders also require a tangent basis. Build it in memory.
                    struct Source { Fvector p, n; Fvector2 uv; };
                    struct Vertex { Fvector p, n, t, b; Fvector2 uv; };
                    static_assert(sizeof(Source) == 32 && sizeof(Vertex) == 56);
                    R_ASSERT(vCount <= data->elapsed() / sizeof(Source));
                    const auto* source = reinterpret_cast<const Source*>(data->pointer());
                    xr_vector<Vertex> vertices(vCount);
                    for (u32 i = 0; i < vCount; ++i)
                    {
                        vertices[i] = {source[i].p, source[i].n, Fvector{}, Fvector{}, source[i].uv};
                    }
                    IReader* indices = data->open_chunk(OGF_INDICES);
                    R_ASSERT(indices);
                    const u32 count = indices->r_u32();
                    R_ASSERT(count % 3 == 0 && count <= indices->elapsed() / sizeof(u16));
                    for (u32 i = 0; i < count; i += 3)
                    {
                        const u16 a = indices->r_u16(), b = indices->r_u16(), c = indices->r_u16();
                        R_ASSERT(a < vCount && b < vCount && c < vCount);
                        Fvector e1, e2, tangent, bitangent;
                        e1.sub(source[b].p, source[a].p); e2.sub(source[c].p, source[a].p);
                        const float u1 = source[b].uv.x - source[a].uv.x, v1 = source[b].uv.y - source[a].uv.y;
                        const float u2 = source[c].uv.x - source[a].uv.x, v2 = source[c].uv.y - source[a].uv.y;
                        const float determinant = u1 * v2 - v1 * u2;
                        if (_abs(determinant) < EPS_S) continue;
                        tangent.mul(e1, v2).mad(e2, -v1).div(determinant);
                        bitangent.mul(e2, u1).mad(e1, -u2).div(determinant);
                        for (const u16 index : {a,b,c}) { vertices[index].t.add(tangent); vertices[index].b.add(bitangent); }
                    }
                    indices->close();
                    for (auto& v : vertices)
                    {
                        v.n.normalize_safe(Fvector{0,1,0});
                        v.t.mad(v.n, -v.t.dotproduct(v.n));
                        if (v.t.square_magnitude() < EPS_S)
                            v.t.crossproduct(_abs(v.n.y) < .9f ? Fvector{0,1,0} : Fvector{1,0,0}, v.n);
                        v.t.normalize_safe();
                        Fvector bitangent; bitangent.crossproduct(v.n, v.t);
                        if (bitangent.dotproduct(v.b) < 0.f) bitangent.invert();
                        v.b = bitangent;
                    }
                    dcl = {{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},
                        {0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},
                        {0,24,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_TANGENT,0},
                        {0,36,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_BINORMAL,0},
                        {0,48,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0}, D3DDECL_END()};
                    vFormat = dcl.data();
                    R_CHK(dx10BufferUtils::CreateVertexBuffer(&p_rm_Vertices, vertices.data(), vCount * sizeof(Vertex)));
                }
                else
                    R_CHK(dx10BufferUtils::CreateVertexBuffer(&p_rm_Vertices, data->pointer(), vCount * vStride));
                DXUT_SetDebugName(p_rm_Vertices, "Fvisual VB");
                HW.stats_manager.increment_stats_vb(p_rm_Vertices);
            }
        }

        // read indices
        if (data->find_chunk(OGF_ICONTAINER))
        {
            R_ASSERT(0, "pls notify andy about this.");
        }
        else
        {
            R_ASSERT(data->find_chunk(OGF_INDICES));
            iBase = 0;
            iCount = data->r_u32();
            dwPrimitives = iCount / 3;

            VERIFY(NULL == p_rm_Indices);
            R_CHK(dx10BufferUtils::CreateIndexBuffer(&p_rm_Indices, data->pointer(), iCount * 2));
            DXUT_SetDebugName(p_rm_Indices, "Fvisual IB");
            HW.stats_manager.increment_stats_ib(p_rm_Indices);
        }
    }

    if (dwFlags & VLOAD_NOVERTICES)
        return;

    rm_geom.create(vFormat, p_rm_Vertices, p_rm_Indices);
}


void Fvisual::Render(CBackend& cmd_list, float, bool use_fast_geo)
{
    ZoneScoped;

    if (m_fast && use_fast_geo && !cmd_list.is_TessEnabled())
    {
        cmd_list.set_Geometry(m_fast->rm_geom);
        cmd_list.Render(D3DPT_TRIANGLELIST, m_fast->vBase, 0, m_fast->vCount, m_fast->iBase, m_fast->dwPrimitives);
        cmd_list.stat.r.s_static.add(m_fast->vCount);
    }
    else
    {
        cmd_list.set_Geometry(rm_geom);
        cmd_list.Render(D3DPT_TRIANGLELIST, vBase, 0, vCount, iBase, dwPrimitives);
        cmd_list.stat.r.s_static.add(vCount);
    }
}

#define PCOPY(a) a = pFrom->a

void Fvisual::Copy(dxRender_Visual* pSrc)
{
    dxRender_Visual::Copy(pSrc);

    const Fvisual* pFrom = smart_cast<Fvisual*>(pSrc);

    PCOPY(rm_geom);

    PCOPY(p_rm_Vertices);
    if (p_rm_Vertices)
        p_rm_Vertices->AddRef();
    PCOPY(vBase);
    PCOPY(vCount);

    PCOPY(p_rm_Indices);
    if (p_rm_Indices)
        p_rm_Indices->AddRef();
    PCOPY(iBase);
    PCOPY(iCount);
    PCOPY(dwPrimitives);

    PCOPY(m_fast);
}
