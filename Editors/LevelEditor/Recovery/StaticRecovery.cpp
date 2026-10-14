#include "LevelRecovery.h"
#include "NativeRecoveryIO.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace LevelRecovery
{
namespace
{
namespace fs=std::filesystem;
void Check(bool v,const char* error) { if (!v) throw std::runtime_error(error); }
bool Static(const Mesh& m) { return m.type==0 || m.type==2; }
uint32_t Bits(float v) { uint32_t b; if (v==0) v=0; std::memcpy(&b,&v,4); return b; }
// Quantization is used ONLY to shortlist potential matches. Reuse requires exact
// source vertex positions after float translation, exact attributes and a vertex
// bijection. A hash collision or close geometry cannot authorize a replacement.
using Key=std::array<int64_t,25>;
struct Canonical { Key key; StaticFaceRef ref; };
Key FaceKey(const Level& l,const StaticFaceRef& ref,const std::array<float,3>& origin)
{
    Key key{};
    const auto& mesh=l.meshes[ref.mesh];
    key[0]=mesh.shader;
    for (int c=0; c<3; ++c)
    {
        const auto& v=mesh.vertices[mesh.triangles[ref.face].vertices[(c+ref.rotation)%3]];
        for (int j=0; j<3; ++j)
        {
            const double x=(double(v.position[j])-origin[j])*10000.0;
            Check(std::isfinite(x) && std::abs(x)<9e18,"Static bounds exceed fingerprint range");
            key[1+c*8+j]=int64_t(std::llround(x));
            key[1+c*8+3+j]=Bits(v.normal[j]);
        }
        key[1+c*8+6]=Bits(v.uv[0]); key[1+c*8+7]=Bits(v.uv[1]);
    }
    return key;
}
uint64_t Fingerprint(const std::vector<Canonical>& faces)
{
    uint64_t h=14695981039346656037ull;
    for (const auto& f : faces)
        for (int64_t x : f.key)
            for (unsigned shift=0; shift<64; shift+=8) { h^=uint8_t(uint64_t(x)>>shift); h*=1099511628211ull; }
    return h;
}
bool Matches(const Level& l,const StaticModel& model,const std::vector<Canonical>& faces,const std::array<float,3>& origin)
{
    if (model.canonicalFaces.size()!=faces.size()) return false;
    std::unordered_map<uint64_t,uint64_t> forward,reverse;
    for (size_t i=0; i<faces.size(); ++i)
    {
        const auto& ref=faces[i].ref;
        const auto& source=l.meshes[ref.mesh];
        const auto address=model.canonicalFaces[i];
        const auto& part=model.parts[address[0]];
        if (part.shader!=source.shader) return false;
        for (int c=0; c<3; ++c)
        {
            const uint32_t si=source.triangles[ref.face].vertices[(c+ref.rotation)%3];
            const uint32_t pi=part.triangles[address[1]].vertices[c];
            const auto& a=source.vertices[si]; const auto& b=part.vertices[pi];
            for (int j=0; j<3; ++j)
                if (float(b.position[j]+origin[j])!=a.position[j] || Bits(b.normal[j])!=Bits(a.normal[j])) return false;
            if (Bits(a.uv[0])!=Bits(b.uv[0]) || Bits(a.uv[1])!=Bits(b.uv[1])) return false;
            const uint64_t s=(uint64_t(ref.mesh)<<32)|si, p=(uint64_t(address[0])<<32)|pi;
            const auto f=forward.emplace(s,p),r=reverse.emplace(p,s);
            if ((!f.second && f.first->second!=p) || (!r.second && r.first->second!=s)) return false;
        }
    }
    return true;
}
StaticModel MakeModel(const Level& l,const std::vector<Canonical>& faces,const std::array<float,3>& origin)
{
    StaticModel model;
    std::map<uint32_t,uint32_t> parts;
    std::vector<std::unordered_map<uint64_t,uint32_t>> vertices;
    for (const auto& f : faces)
    {
        const auto& mesh=l.meshes[f.ref.mesh];
        const auto found=parts.emplace(mesh.shader,uint32_t(parts.size()));
        if (found.second) { TreePrototype part; part.shader=mesh.shader; model.parts.push_back(std::move(part)); vertices.emplace_back(); }
        const auto partId=found.first->second;
        auto& part=model.parts[partId];
        Triangle triangle;
        for (int c=0; c<3; ++c)
        {
            const auto source=mesh.triangles[f.ref.face].vertices[(c+f.ref.rotation)%3];
            const uint64_t identity=(uint64_t(f.ref.mesh)<<32)|source;
            const auto v=vertices[partId].emplace(identity,uint32_t(part.vertices.size()));
            if (v.second)
            {
                auto value=mesh.vertices[source];
                for (int j=0; j<3; ++j) value.position[j]-=origin[j];
                part.vertices.push_back(value);
            }
            triangle.vertices[c]=v.first->second;
        }
        model.canonicalFaces.push_back({{partId,uint32_t(part.triangles.size())}});
        part.triangles.push_back(triangle);
    }
    return model;
}
std::string ModelName(size_t i)
{
    std::ostringstream s; s << "static_" << std::setw(5) << std::setfill('0') << i; return s.str();
}
std::string TreeName(size_t i)
{
    std::ostringstream s; s << "tree_" << std::setw(5) << std::setfill('0') << i; return s.str();
}
std::ofstream Text(const fs::path& p)
{
    std::ofstream f; f.exceptions(std::ios::badbit|std::ios::failbit); f.open(p);
    f.imbue(std::locale::classic()); f << std::setprecision(9); return f;
}
std::string Csv(const std::string& s)
{
    std::string r="\""; for (char c : s) { if (c=='"') r+=c; r+=c; } return r+'"';
}
bool Inside(fs::path child,const fs::path& root)
{
    while (!child.empty())
    {
        if (fs::exists(child) && fs::equivalent(child,root)) return true;
        auto parent=child.parent_path(); if (parent==child) break; child=parent;
    }
    return false;
}
void WriteModels(const Level& l,const StaticLibrary& plan,const fs::path& root)
{
    for (size_t i=0; i<plan.models.size(); ++i)
    {
        NativeIO::SaveModel(l,plan.models[i].parts,(root/(ModelName(i)+".object")).u8string());
        NativeIO::SaveNormals(plan.models[i].parts,(root/(ModelName(i)+".normals.bin")).u8string());
    }
}
}

StaticLibrary BuildStaticLibrary(const Level& l)
{
    Check(l.componentCount>0,"Run connected surface analysis before static recovery");
    std::map<uint32_t,std::vector<StaticFaceRef>> components;
    for (uint32_t m=0; m<l.meshes.size(); ++m)
        if (Static(l.meshes[m]))
            for (uint32_t f=0; f<l.meshes[m].triangles.size(); ++f)
                components[l.meshes[m].triangles[f].component].push_back({m,f,0});
    StaticLibrary plan;
    std::unordered_map<uint64_t,std::vector<uint32_t>> candidates;
    for (const auto& component : components)
    {
        std::array<float,3> origin=l.meshes[component.second.front().mesh].vertices[
            l.meshes[component.second.front().mesh].triangles[component.second.front().face].vertices[0]].position;
        for (const auto& ref : component.second)
            for (auto vi : l.meshes[ref.mesh].triangles[ref.face].vertices)
                for (int j=0; j<3; ++j) origin[j]=(std::min)(origin[j],l.meshes[ref.mesh].vertices[vi].position[j]);
        // If centring would lose even one source float, retain world coordinates
        // on that axis. Large terrain never suffers cancellation or remeshing.
        for (const auto& ref : component.second)
            for (auto vi : l.meshes[ref.mesh].triangles[ref.face].vertices)
                for (int j=0; j<3; ++j)
                {
                    const float p=l.meshes[ref.mesh].vertices[vi].position[j];
                    if (float(float(p-origin[j])+origin[j])!=p) origin[j]=0;
                }
        std::vector<Canonical> faces;
        faces.reserve(component.second.size());
        for (auto ref : component.second)
        {
            Canonical best{FaceKey(l,ref,origin),ref};
            for (uint32_t rotation=1; rotation<3; ++rotation)
            {
                ref.rotation=rotation;
                const auto key=FaceKey(l,ref,origin);
                if (key<best.key) best={key,ref};
            }
            faces.push_back(best);
        }
        std::stable_sort(faces.begin(),faces.end(),[](const Canonical& a,const Canonical& b) { return a.key<b.key; });
        auto& possible=candidates[Fingerprint(faces)];
        uint32_t model=UINT32_MAX;
        for (auto i : possible) if (Matches(l,plan.models[i],faces,origin)) { model=i; break; }
        if (model==UINT32_MAX)
        {
            model=uint32_t(plan.models.size());
            plan.models.push_back(MakeModel(l,faces,origin)); possible.push_back(model);
            Check(Matches(l,plan.models.back(),faces,origin),"Static model failed exact float round-trip verification");
        }
        StaticInstance instance; instance.component=component.first; instance.model=model; instance.origin=origin;
        for (const auto& f : faces) instance.faces.push_back(f.ref);
        plan.instances.push_back(std::move(instance));
    }
    Check(!plan.instances.empty(),"No static geometry in this level");
    return plan;
}

std::string StaticLibraryName(const Level& l)
{
    const auto pos=l.libraryName.find_last_of('_');
    Check(pos!=std::string::npos,"Missing source fingerprint");
    const auto hash=l.libraryName.substr(pos+1);
    Check(hash.size()==16 && hash.find_first_not_of("0123456789abcdef")==std::string::npos,"Invalid source fingerprint");
    return "recovered_statics_v03_"+hash;
}
void InstallStaticLibrary(const Level& l,const StaticLibrary& plan,const std::string& objectsDirectory)
{
    Check(!plan.models.empty(),"Empty static library");
    const auto root=fs::canonical(fs::u8path(objectsDirectory));
    Check(!Inside(root,fs::canonical(fs::u8path(l.source))),"Cannot install inside source level");
    const auto dest=root/StaticLibraryName(l);
    if (fs::exists(dest))
    {
        Check(fs::is_directory(dest) && !fs::is_symlink(dest),"Static namespace is not a regular directory");
        for (size_t i=0; i<plan.models.size(); ++i)
            Check(NativeIO::EqualModel(l,plan.models[i].parts,(dest/(ModelName(i)+".object")).u8string()) &&
                NativeIO::EqualNormals(plan.models[i].parts,(dest/(ModelName(i)+".normals.bin")).u8string()),
                "Static library contains modified files; nothing overwritten");
        return;
    }
    auto stage=dest; stage+=".partial";
    Check(fs::create_directory(stage),"Static install staging directory exists");
    WriteModels(l,plan,stage);
    Check(!fs::exists(dest),"Static destination appeared during installation"); fs::rename(stage,dest);
}

void ExportSceneLibrary(const Level& l,const StaticLibrary& plan,const std::string& newDirectory)
{
    Check(!plan.models.empty() && !newDirectory.empty(),"Empty static export");
    const auto library=StaticLibraryName(l);
    const auto trees=BuildTreeLibrary(l);
    std::vector<NativeIO::Instance> selection;
    for (const auto& instance : plan.instances)
    {
        Check(instance.model<plan.models.size(),"Invalid static model reference");
        Placement p; p.position=instance.origin; p.scale={{1,1,1}};
        selection.push_back({"surface_candidate_"+std::to_string(instance.component),library+"\\"+ModelName(instance.model),p});
    }
    for (const auto& instance : trees.instances)
    {
        Placement p;
        for (auto i : instance.meshes) Check(TreePlacement(l,l.meshes[i],p),"Unsupported vegetation transform; use the separate tree export for diagnostics");
        selection.push_back({"recovered_tree_visual_"+std::to_string(instance.visual),l.libraryName+"\\"+TreeName(instance.model),p});
    }
    const auto out=fs::weakly_canonical(fs::absolute(fs::u8path(newDirectory)));
    Check(!Inside(out,fs::canonical(fs::u8path(l.source))),"Scene export must be outside source level");
    Check(!fs::exists(out) && fs::is_directory(out.parent_path()),"Scene export requires a new folder and existing parent");
    auto stage=out; stage+=".partial";
    Check(fs::create_directory(stage),"Scene staging directory exists");
    fs::create_directories(stage/"objects"/library);
    WriteModels(l,plan,stage/"objects"/library);
    if (!trees.models.empty()) ExportTreeLibrary(l,(stage/"vegetation").u8string());
    NativeIO::SaveSelection(selection,(stage/"geometry.selection").u8string());
    auto models=Text(stage/"static_models.csv");
    auto parts=Text(stage/"static_parts.csv");
    models << "model,object_reference,parts,vertices,triangles\n";
    parts << "model,mesh_index,shader_id,source_shader_and_textures\n";
    uint64_t sharedTriangles=0;
    for (size_t i=0; i<plan.models.size(); ++i)
    {
        uint64_t nv=0,nf=0; const auto& m=plan.models[i];
        for (size_t j=0; j<m.parts.size(); ++j)
        {
            const auto& p=m.parts[j]; nv+=p.vertices.size(); nf+=p.triangles.size();
            parts << i << ',' << j << ',' << p.shader << ',' << Csv(l.shaders.at(p.shader)) << '\n';
        }
        sharedTriangles+=nf;
        models << i << ',' << library << '\\' << ModelName(i) << ',' << m.parts.size() << ',' << nv << ',' << nf << '\n';
    }
    models.close(); parts.close();
    auto instances=Text(stage/"static_instances.csv");
    auto faces=Text(stage/"static_faces.csv");
    instances << "component,model,x,y,z,triangles\n";
    faces << "component,model,mesh_index,model_face,source_visual,source_face,source_rotation,shader_id\n";
    uint64_t expandedTriangles=0;
    for (const auto& instance : plan.instances)
    {
        instances << instance.component << ',' << instance.model;
        for (auto x : instance.origin) instances << ',' << x;
        instances << ',' << instance.faces.size() << '\n';
        expandedTriangles+=instance.faces.size();
        const auto& model=plan.models[instance.model];
        for (size_t i=0; i<instance.faces.size(); ++i)
        {
            const auto& ref=instance.faces[i]; const auto& source=l.meshes[ref.mesh]; const auto address=model.canonicalFaces[i];
            faces << instance.component << ',' << instance.model << ',' << address[0] << ',' << address[1] << ','
                << source.visual << ',' << ref.face << ',' << ref.rotation << ',' << source.shader << '\n';
        }
    }
    instances.close(); faces.close();
    { std::ofstream f(stage/"original.cform",std::ios::binary); f.exceptions(std::ios::badbit|std::ios::failbit);
      f.write(reinterpret_cast<const char*>(l.collisionBytes.data()),l.collisionBytes.size()); f.close(); }
    auto report=Text(stage/"scene-recovery.txt");
    report << "X-Ray native render recovery 0.3\nSource: " << l.source << "\nStatic library: " << library
        << "\nStatic surface candidates: " << plan.instances.size() << "\nUnique static models: " << plan.models.size()
        << "\nStatic triangles preserved: " << expandedTriangles << "\nStatic library triangles: " << sharedTriangles
        << "\nCoincident static triangle copies preserved: " << l.coincidentFaceCopies << "\nNonmanifold static edges left unresolved: " << l.ambiguousEdges
        << "\nWhole vegetation models: " << trees.models.size() << "\nVegetation instances: " << trees.instances.size()
        << "\nGeometry selection objects: " << selection.size()
        << "\n\nAll static instance translations reproduce source float positions exactly; UVs, normals, winding and vertex correspondence are checked before reuse."
        << "\nCoincident faces participate once in adjacency analysis only. Every source triangle, including reverse sides, is exported. No remeshing, welding, simplification or approximate placement."
        << "\nSurface candidates are NOT proven author objects. A welded table and bar cannot be identified as separate objects from connectivity alone. Native models retain separate material meshes for review."
        << "\nInstall objects/<static namespace> and vegetation/objects/<tree namespace> into SDK objects, then open geometry.selection. The recovery window can install and import into an empty scene."
        << "\nPreview is editor\\wire; render/compiler/physical materials remain explicitly unmapped and SDK Build/Make Game are blocked."
        << "\nOriginal local shading normals are archived in *.normals.bin; SDK preview recomputes normals. Tree lighting, wind, lightmaps, sectors, portals, details and spawn are not restored into this selection."
        << "\noriginal.cform is an exact source snapshot. It is NOT imported as render geometry and no collider is generated from recovered meshes. Original gamemtl.xr is still required for physical material mapping.\n";
    report.close();
    Check(!fs::exists(out),"Scene destination appeared during export"); fs::rename(stage,out);
}
}
