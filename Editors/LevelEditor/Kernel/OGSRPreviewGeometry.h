#pragma once
#include "../../../SDKRuntime/InGame/PreviewGeometryFormat.h"
#include <array>
#include <map>
#include <functional>

namespace SDKPreviewExport
{
    struct SurfacePolicy { bool visible, collision, shadow; };
    struct Options
    {
        bool hideTechnical = true, disableTechnicalCollision = false, bakeLighting = true;
        float cellSize = 1.f, ambientCap = .7f;
        std::map<xr_string, SurfacePolicy> overrides;
    };
    inline Options& Settings() { static Options settings; return settings; }
    inline xr_string Key(const CSurface& s)
    {
        return xr_string(s._ShaderXRLCName()) + " | " + s._GameMtlName() + " | " + s._ShaderName() + " | " + s._Texture();
    }
    inline bool Technical(const CSurface& s)
    {
        const auto* shader = EDevice.ShaderXRLC.Get(s._ShaderXRLCName());
        return shader && !shader->flags.bRendering;
    }
    inline SurfacePolicy Policy(const CSurface& s)
    {
        auto& settings = Settings();
        const auto found = settings.overrides.find(Key(s));
        if (found != settings.overrides.end()) return found->second;
        const auto* shader = EDevice.ShaderXRLC.Get(s._ShaderXRLCName());
        const bool technical = Technical(s);
        return {!(technical && settings.hideTechnical),
            (!shader || shader->flags.bCollision) && !(technical && settings.disableTechnicalCollision),
            !technical && (!shader || shader->flags.bLIGHT_CastShadow)};
    }
    // Fast, spatially cached sky visibility over an immutable BVH. This is a
    // per-vertex preview bake, not a replacement for the offline xrLC lightmaps.
    class Lighting
    {
        CDB::MODEL model;
        CDB::COLLIDER collider;
        bool ready = false;
        std::map<std::array<int, 6>, SDKInGame::PreviewLighting> cache;
    public:
        void Build(xr_vector<Fvector>& vertices, xr_vector<CDB::TRI>& triangles)
        {
            if (!Settings().bakeLighting || triangles.size() < 2) return;
            model.build(vertices.data(), int(vertices.size()), triangles.data(), int(triangles.size()));
            collider.ray_options(CDB::OPT_ONLYFIRST);
            ready = true;
        }
        SDKInGame::PreviewLighting Sample(const Fvector& position, const Fvector& normal)
        {
            if (!Settings().bakeLighting) return {Settings().ambientCap, 1.f};
            const float cell = Settings().cellSize;
            const std::array<int,6> key{{iFloor(position.x/cell), iFloor(position.y/cell), iFloor(position.z/cell),
                iFloor(normal.x*3.f), iFloor(normal.y*3.f), iFloor(normal.z*3.f)}};
            const auto found = cache.find(key);
            if (found != cache.end()) return found->second;
            Fvector origin; origin.mad(position, normal, .04f);
            static const Fvector directions[] = {{0,1,0},{.7071f,.7071f,0},{-.7071f,.7071f,0},
                {0,.7071f,.7071f},{0,.7071f,-.7071f},{.5f,.7071f,.5f},{-.5f,.7071f,-.5f}};
            float open = 0, total = 0;
            for (const auto& direction : directions)
            {
                const float weight = _max(.05f, normal.dotproduct(direction));
                total += weight;
                if (ready) collider.ray_query(&model, origin, direction, 120.f);
                if (!ready || !collider.r_count()) open += weight;
            }
            const Fvector sun = {-.35f,.8660254f,.3570714f};
            if (ready) collider.ray_query(&model, origin, sun, 200.f);
            SDKInGame::PreviewLighting light{.08f + (Settings().ambientCap-.08f)*open/total,
                !ready || !collider.r_count() ? 1.f : 0.f};
            cache.emplace(key, light);
            return light;
        }
        size_t Samples() const { return cache.size(); }
    };
    struct Vertex { Fvector position, normal; Fvector2 uv; };
    static_assert(sizeof(Vertex) == 32, "preview source vertex layout");

    inline Fvector2 UV(CEditableMesh& mesh, CSurface& surface, u32 face, unsigned corner)
    {
        const auto& refs = mesh.GetVMRefs()[mesh.GetFaces()[face].pv[corner].vmref];
        const st_VMap* fallback = nullptr; u32 fallbackIndex = 0;
        for (u32 i = 0; i < refs.count; ++i)
        {
            const auto& ref = refs.pts[i];
            const auto* map = mesh.GetVMaps()[ref.vmap_index];
            if (map->type != vmtUV) continue;
            if (!fallback) { fallback = map; fallbackIndex = ref.index; }
            if (map->name == surface._VMap()) return map->getUV(ref.index);
        }
        if (!fallback) throw std::runtime_error("SDK surface has no UV map");
        return fallback->getUV(fallbackIndex);
    }
    inline bool Mirrored(const Fmatrix& m)
    {
        return m._11*(m._22*m._33-m._23*m._32)-m._12*(m._21*m._33-m._23*m._31)+
            m._13*(m._21*m._32-m._22*m._31)<0;
    }
    inline bool Export(CSceneObject& object, IWriter& writer, Lighting& lighting)
    {
        auto* reference = object.GetReference();
        const Fmatrix& transform = object._Transform();
        Fmatrix inverse; inverse.invert(transform);
        const bool mirrored=Mirrored(transform);
        CMemoryWriter children;
        Fbox bounds; bounds.invalidate();
        u32 child = 0;
        for (auto* mesh : reference->Meshes())
        {
            mesh->GenerateVNormals(nullptr);
            struct NormalsScope { CEditableMesh* mesh; ~NormalsScope() { mesh->UnloadVNormals(); } } normals{mesh};
            for (const auto& entry : mesh->GetSurfFaces())
            {
                CSurface* surface = entry.first;
                if (!Policy(*surface).visible) continue;
                xr_vector<Vertex> vertices;
                xr_vector<SDKInGame::PreviewLighting> lights;
                auto flush = [&]()
                {
                    if (vertices.empty()) return;
                    Fbox box; box.invalidate();
                    for (const auto& vertex : vertices) box.modify(vertex.position);
                    bounds.merge(box);
                    children.open_chunk(child++);
                    ogf_header header{};
                    header.format_version = xrOGF_FormatVersion; header.type = MT_NORMAL;
                    header.bb.min = box.min; header.bb.max = box.max; box.getsphere(header.bs.c, header.bs.r);
                    children.w_chunk(OGF_HEADER, &header, sizeof(header));
                    const char* shader = Technical(*surface) ? "models\\model" : surface->_ShaderName();
                    const bool staticShader = strncmp(shader, "models\\", 7) != 0;
                    children.open_chunk(OGF_TEXTURE);
                    children.w_stringZ(surface->_Texture()); children.w_stringZ(shader); children.close_chunk();
                    children.open_chunk(OGF_VERTICES);
                    children.w_u32(0x112); children.w_u32(u32(vertices.size()));
                    children.w(vertices.data(), u32(vertices.size()*sizeof(Vertex))); children.close_chunk();
                    children.open_chunk(OGF_INDICES); children.w_u32(u32(vertices.size()));
                    for (u32 i = 0; i < vertices.size(); ++i) children.w_u16(u16(i));
                    children.close_chunk();
                    children.open_chunk(SDKInGame::previewLightingChunk);
                    children.w_u32(SDKInGame::previewLightingVersion); children.w_u32(staticShader ? 1 : 0);
                    children.w_u32(u32(lights.size())); children.w(lights.data(), u32(lights.size()*sizeof(lights[0])));
                    children.close_chunk(); children.close_chunk();
                    vertices.clear(); lights.clear();
                };
                for (u32 face : entry.second)
                {
                    const Fvector* points[3]; mesh->GetFacePT(face, points);
                    Fvector world[3];
                    for (unsigned i=0; i<3; ++i) transform.transform_tiny(world[i], *points[i]);
                    Fvector faceNormal; faceNormal.mknormal_non_normalized(world[0],world[1],world[2]);
                    if (faceNormal.square_magnitude() <= EPS_S*EPS_S) continue;
                    const unsigned sides = surface->m_Flags.is(CSurface::sf2Sided) ? 2 : 1;
                    for (unsigned side=0; side<sides; ++side)
                    {
                        if (vertices.size()+3 > 65532) flush();
                        for (unsigned k=0; k<3; ++k)
                        {
                            const unsigned corner = (bool(side)!=mirrored) ? 2-k : k;
                            const auto& n = mesh->GetVNormals()[face*3+corner];
                            Fvector normal{inverse._11*n.x+inverse._12*n.y+inverse._13*n.z,
                                inverse._21*n.x+inverse._22*n.y+inverse._23*n.z,
                                inverse._31*n.x+inverse._32*n.y+inverse._33*n.z};
                            normal.normalize_safe(Fvector{0,1,0});
                            if (side) normal.invert();
                            vertices.push_back({world[corner],normal,UV(*mesh,*surface,face,corner)});
                            lights.push_back(lighting.Sample(world[corner],normal));
                        }
                    }
                }
                flush();
            }
        }
        if (!child) return false;
        ogf_header header{};
        header.format_version = xrOGF_FormatVersion; header.type = MT_HIERRARHY;
        header.bb.min = bounds.min; header.bb.max = bounds.max; bounds.getsphere(header.bs.c,header.bs.r);
        writer.w_chunk(OGF_HEADER, &header, sizeof(header));
        writer.w_chunk(OGF_CHILDREN, children.pointer(), children.size());
        return true;
    }
}
