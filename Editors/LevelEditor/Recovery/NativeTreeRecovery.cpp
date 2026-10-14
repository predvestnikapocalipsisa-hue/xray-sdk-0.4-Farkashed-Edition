#include "LevelRecovery.h"
#include "NativeRecoveryIO.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace LevelRecovery
{
namespace
{
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
struct Writer
{
    Bytes bytes;
    template<class T> void Value(const T& v)
    {
        const auto p = reinterpret_cast<const uint8_t*>(&v);
        bytes.insert(bytes.end(),p,p+sizeof(T));
    }
    void U32(uint32_t v) { Value(v); }
    void U16(uint16_t v) { Value(v); }
    void U8(uint8_t v) { Value(v); }
    void String(const std::string& v)
    {
        Check(v.size() < 1024 && v.find('\0') == std::string::npos, "Invalid native object string");
        bytes.insert(bytes.end(),v.begin(),v.end()); bytes.push_back(0);
    }
    void Chunk(uint32_t id, const Writer& body)
    {
        Check(body.bytes.size() <= UINT32_MAX, "Native chunk too large");
        U32(id); U32(uint32_t(body.bytes.size()));
        bytes.insert(bytes.end(),body.bytes.begin(),body.bytes.end());
    }
    void Chunk(uint32_t id, Writer&& body) { Chunk(id,static_cast<const Writer&>(body)); }
    template<class Fn> void Chunk(uint32_t id, Fn fn)
    {
        Writer w; fn(w); Chunk(id, static_cast<const Writer&>(w));
    }
};
std::string ModelName(size_t i)
{
    std::ostringstream s;
    s << "tree_" << std::setw(5) << std::setfill('0') << i;
    return s.str();
}
bool ValidNamespace(const std::string& name)
{
    const std::string prefix = "recovered_trees_v02_";
    if (name.size() != prefix.size()+16 || name.compare(0,prefix.size(),prefix) != 0) return false;
    for (size_t i=prefix.size(); i<name.size(); ++i)
        if (!((name[i]>='0' && name[i]<='9') || (name[i]>='a' && name[i]<='f'))) return false;
    return true;
}
void ValidateLibrary(const Level& l)
{
    Check(ValidNamespace(l.libraryName), "Invalid recovery library namespace");
    Check(!l.treePrototypes.empty(), "No tree instances were recovered");
}
std::string BaseTexture(const std::string& shader)
{
    const auto split = shader.find('/');
    if (split == std::string::npos || split+1 == shader.size()) return "ed\\ed_dummy";
    const auto end = shader.find(',',split+1);
    return shader.substr(split+1,end == std::string::npos ? end : end-split-1);
}
Writer NativeObject(const Level& l, const TreePrototype& p, uint32_t part=0, bool multipleUsage=true)
{
    Check(p.shader < l.shaders.size() && p.vertices.size() >= 3 && !p.triangles.empty(), "Invalid tree prototype");
    Check(p.vertices.size() < UINT32_MAX && p.triangles.size() < UINT32_MAX, "Native mesh too large");
    const auto nv = uint32_t(p.vertices.size()), nf = uint32_t(p.triangles.size());
    Writer object;
    object.Chunk(0x0900,[](Writer& w) { w.U16(0x10); });
    object.Chunk(0x0903,[&](Writer& w) { w.U32(multipleUsage ? 1u<<4 : 0); });
    object.Chunk(0x0912,[&](Writer& w) { w.String(multipleUsage ? "xray_recovery_materials_unresolved; native tree preview; see recovery manifest" :
        "xray_recovery_materials_unresolved; native static preview; see recovery manifest"); });
    object.Chunk(0x0907,[&](Writer& w)
    {
        w.U32(1); w.String("recovered_surface_"+std::to_string(part));
        // An editor shader is available even when the mod's custom blender isn't.
        // Source render shader is kept verbatim in prototypes.csv for mapping.
        w.String("editor\\wire");
        w.String("__recovery_unmapped__"); w.String("__recovery_unmapped__");
        w.String(BaseTexture(l.shaders[p.shader])); w.String("Texture");
        w.U32(0); w.U32(0x112); w.U32(1);
    });
    Writer mesh;
    mesh.Chunk(0x1000,[](Writer& w) { w.U16(0x11); });
    mesh.Chunk(0x1001,[&](Writer& w) { w.String((multipleUsage ? "tree_geometry_" : "static_geometry_")+std::to_string(part)); });
    mesh.Chunk(0x1002,[](Writer& w) { w.U8(1); }); // visible, Maya edge smoothing
    mesh.Chunk(0x1010,[](Writer& w) { w.U32(0); w.U32(0); });
    mesh.Chunk(0x1004,[&](Writer& w)
    {
        auto low = p.vertices.front().position, high = low;
        for (const auto& v : p.vertices)
            for (int j=0; j<3; ++j) { low[j]=(std::min)(low[j],v.position[j]); high[j]=(std::max)(high[j],v.position[j]); }
        for (auto x : low) w.Value(x);
        for (auto x : high) w.Value(x);
    });
    mesh.Chunk(0x1005,[&](Writer& w)
    {
        w.U32(nv);
        for (const auto& v : p.vertices) for (auto x : v.position) w.Value(x);
    });
    mesh.Chunk(0x1006,[&](Writer& w)
    {
        w.U32(nf);
        for (const auto& t : p.triangles) for (auto i : t.vertices)
        {
            Check(i < nv, "Native face index out of bounds");
            w.U32(i); w.U32(i); // position and one UV reference per compiled vertex
        }
    });
    mesh.Chunk(0x1013,[&](Writer& w) { for (uint32_t i=0; i<nf; ++i) w.U32(0); });
    mesh.Chunk(0x1008,[&](Writer& w)
    {
        w.U32(nv);
        for (uint32_t i=0; i<nv; ++i) { w.U8(1); w.U32(0); w.U32(i); }
    });
    mesh.Chunk(0x1009,[&](Writer& w)
    {
        w.U16(1); w.String("recovered_surface_"+std::to_string(part)); w.U32(nf);
        for (uint32_t i=0; i<nf; ++i) w.U32(i);
    });
    mesh.Chunk(0x1012,[&](Writer& w)
    {
        w.U32(1); w.String("Texture"); w.U8(2); w.U8(0); w.U8(0); w.U32(nv);
        for (const auto& v : p.vertices) for (auto x : v.uv) w.Value(x); // SDK UV, no OBJ V flip
        for (uint32_t i=0; i<nv; ++i) w.U32(i);
    });
    object.Chunk(0x0910,[&](Writer& w) { w.Chunk(0,static_cast<const Writer&>(mesh)); });
    Writer file; file.Chunk(0x7777,static_cast<const Writer&>(object));
    return file;
}
Writer Extract(const Writer& w,uint32_t id)
{
    for (size_t o=0; o+8<=w.bytes.size();)
    {
        uint32_t type,n;
        std::memcpy(&type,w.bytes.data()+o,4); std::memcpy(&n,w.bytes.data()+o+4,4);
        Check(n<=w.bytes.size()-o-8,"Internal native chunk length error");
        if (type==id) { Writer r; r.bytes.assign(w.bytes.begin()+o+8,w.bytes.begin()+o+8+n); return r; }
        o+=8+n;
    }
    throw std::runtime_error("Internal native chunk missing");
}
Writer NativeParts(const Level& l,const std::vector<const TreePrototype*>& parts,bool multipleUsage)
{
    Check(!parts.empty() && parts.size()<=65535,"Invalid native model part count");
    Writer body,meshes,surfaces;
    surfaces.U32(uint32_t(parts.size()));
    for (uint32_t i=0; i<parts.size(); ++i)
    {
        const auto object=NativeObject(l,*parts[i],i,multipleUsage);
        const auto part=Extract(object,0x7777);
        if (i==0)
            for (auto id : {0x0900u,0x0903u,0x0912u}) body.Chunk(id,Extract(part,id));
        const auto surface=Extract(part,0x0907);
        surfaces.bytes.insert(surfaces.bytes.end(),surface.bytes.begin()+4,surface.bytes.end());
        meshes.Chunk(i,Extract(Extract(part,0x0910),0));
    }
    body.Chunk(0x0907,static_cast<const Writer&>(surfaces));
    body.Chunk(0x0910,static_cast<const Writer&>(meshes));
    Writer file; file.Chunk(0x7777,static_cast<const Writer&>(body)); return file;
}
Writer NativeModel(const Level& l,const TreeModel& model)
{
    std::vector<const TreePrototype*> parts;
    for (auto i : model.parts) parts.push_back(&l.treePrototypes.at(i));
    return NativeParts(l,parts,true);
}
Writer PartNormals(const std::vector<const TreePrototype*>& parts)
{
    Writer w;
    for (char c : std::string("XRNORM01")) w.U8(uint8_t(c));
    uint64_t count=0;
    for (auto* part : parts) count+=part->vertices.size();
    Check(count<=UINT32_MAX,"Normal sidecar too large"); w.U32(uint32_t(count));
    for (auto* part : parts)
        for (const auto& v : part->vertices) for (auto x : v.normal) w.Value(x);
    return w;
}
Writer Normals(const Level& l,const TreeModel& model)
{
    std::vector<const TreePrototype*> parts;
    for (auto i : model.parts) parts.push_back(&l.treePrototypes.at(i));
    return PartNormals(parts);
}
void Save(const fs::path& path, const Writer& w)
{
    std::ofstream f;
    f.exceptions(std::ios::failbit | std::ios::badbit);
    f.open(path,std::ios::binary);
    f.write(reinterpret_cast<const char*>(w.bytes.data()),w.bytes.size());
    f.close();
}
bool EqualFile(const fs::path& path, const Writer& w)
{
    if (!fs::is_regular_file(path) || fs::file_size(path) != w.bytes.size()) return false;
    std::ifstream f(path,std::ios::binary);
    Bytes bytes(w.bytes.size());
    f.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    return bool(f) && bytes == w.bytes;
}
std::ofstream TextFile(const fs::path& path)
{
    std::ofstream f;
    f.exceptions(std::ios::failbit | std::ios::badbit);
    f.open(path); f.imbue(std::locale::classic()); f << std::setprecision(9);
    return f;
}
std::string Csv(const std::string& text)
{
    std::string result = "\"";
    for (char c : text) { if (c == '"') result += c; result += c; }
    return result+'"';
}
bool Inside(fs::path child, const fs::path& root)
{
    while (!child.empty())
    {
        if (fs::exists(child) && fs::equivalent(child,root)) return true;
        const auto parent = child.parent_path();
        if (child == parent) break;
        child = parent;
    }
    return false;
}
std::array<float,16> Compose(const Placement& p)
{
    const float h=p.rotation[1], pitch=p.rotation[0], b=p.rotation[2];
    const float ch=std::cos(h), sh=std::sin(h), cp=std::cos(pitch), sp=std::sin(pitch), cb=std::cos(b), sb=std::sin(b);
    return {{(ch*cb-sp*sh*sb)*p.scale[0], -cp*sb*p.scale[0], (sp*ch*sb+sh*cb)*p.scale[0], 0,
        (sp*sh*cb+ch*sb)*p.scale[1], cp*cb*p.scale[1], (sh*sb-sp*ch*cb)*p.scale[1], 0,
        -cp*sh*p.scale[2], sp*p.scale[2], cp*ch*p.scale[2], 0,
        p.position[0],p.position[1],p.position[2],1}};
}
Writer SceneRecord(const std::string& name,const std::string& reference,const Placement& p)
{
    Writer body;
    body.Chunk(0xF906,[](Writer& w) { w.U32(2); }); // visible also in SocSdk flag layout
    body.Chunk(0xF907,[&](Writer& w) { w.String(name); });
    body.Chunk(0xF903,[&](Writer& w)
    {
        for (auto x : p.position) w.Value(x);
        for (auto x : p.rotation) w.Value(x);
        for (auto x : p.scale) w.Value(x);
    });
    body.Chunk(0x0900,[](Writer& w) { w.U16(0x12); });
    body.Chunk(0x0902,[&](Writer& w) { w.String(reference); });
    body.Chunk(0x0905,[](Writer& w) { w.U32(0); });
    Writer object;
    object.Chunk(0x7703,[](Writer& w) { w.U32(2); }); // OBJCLASS_SCENEOBJECT
    object.Chunk(0x7777,static_cast<const Writer&>(body));
    return object;
}
Writer SceneInstance(const Level& l,const TreeObjectInstance& instance,const Placement& p)
{
    return SceneRecord("recovered_tree_visual_"+std::to_string(instance.visual),l.libraryName+"\\"+ModelName(instance.model),p);
}
void WriteModels(const Level& l, const TreeLibrary& plan, const fs::path& root)
{
    for (size_t i=0; i<plan.models.size(); ++i)
    {
        Save(root/(ModelName(i)+".object"),NativeModel(l,plan.models[i]));
        Save(root/(ModelName(i)+".normals.bin"),Normals(l,plan.models[i]));
    }
}
}

namespace NativeIO
{
std::vector<const TreePrototype*> Pointers(const std::vector<TreePrototype>& parts)
{
    std::vector<const TreePrototype*> result;
    for (const auto& p : parts) result.push_back(&p);
    return result;
}
void SaveModel(const Level& l,const std::vector<TreePrototype>& parts,const std::string& file)
{ Save(fs::u8path(file),NativeParts(l,Pointers(parts),false)); }
void SaveNormals(const std::vector<TreePrototype>& parts,const std::string& file)
{ Save(fs::u8path(file),PartNormals(Pointers(parts))); }
bool EqualModel(const Level& l,const std::vector<TreePrototype>& parts,const std::string& file)
{ return EqualFile(fs::u8path(file),NativeParts(l,Pointers(parts),false)); }
bool EqualNormals(const std::vector<TreePrototype>& parts,const std::string& file)
{ return EqualFile(fs::u8path(file),PartNormals(Pointers(parts))); }
void SaveSelection(const std::vector<Instance>& instances,const std::string& file)
{
    Writer list;
    for (uint32_t i=0; i<instances.size(); ++i)
        list.Chunk(i,SceneRecord(instances[i].name,instances[i].reference,instances[i].placement));
    Writer selection;
    selection.Chunk(0x9df3,[](Writer& w) { w.U32(5); });
    selection.Chunk(0x7708,static_cast<const Writer&>(list)); Save(fs::u8path(file),selection);
}
}

TreeLibrary BuildTreeLibrary(const Level& l)
{
    TreeLibrary plan;
    std::map<uint32_t,uint32_t> byVisual,owners;
    for (uint32_t i=0; i<l.meshes.size(); ++i)
        if (l.meshes[i].treePrototype!=UINT32_MAX) byVisual.emplace(l.meshes[i].visual,i);
    for (const auto& group : l.treeGroups) for (auto child : group.children) ++owners[child];
    std::set<uint32_t> used;
    std::map<std::vector<uint32_t>,uint32_t> models;
    auto add=[&](uint32_t visual,std::vector<uint32_t> meshes)
    {
        std::stable_sort(meshes.begin(),meshes.end(),[&](uint32_t a,uint32_t b) { return l.meshes[a].treePrototype<l.meshes[b].treePrototype; });
        std::vector<uint32_t> parts;
        for (auto i : meshes) { parts.push_back(l.meshes[i].treePrototype); used.insert(i); }
        const auto inserted=models.emplace(parts,uint32_t(plan.models.size()));
        if (inserted.second) plan.models.push_back(TreeModel{parts});
        plan.instances.push_back(TreeObjectInstance{visual,inserted.first->second,std::move(meshes)});
    };
    for (const auto& group : l.treeGroups)
    {
        std::vector<uint32_t> meshes;
        bool valid=!group.children.empty();
        for (auto child : group.children)
        {
            const auto found=byVisual.find(child);
            if (found==byVisual.end() || owners[child]!=1 || used.count(found->second)) { valid=false; break; }
            if (!meshes.empty() && l.meshes[found->second].transform!=l.meshes[meshes[0]].transform) { valid=false; break; }
            meshes.push_back(found->second);
        }
        if (valid) add(group.visual,std::move(meshes));
    }
    // No geometric guessing: leaves without an unambiguous source parent remain
    // standalone instances. Their geometry and matrices are still preserved.
    for (const auto& item : byVisual) if (!used.count(item.second)) add(item.first,{item.second});
    return plan;
}

bool TreePlacement(const Level& l, const Mesh& mesh, Placement& p)
{
    p = Placement{};
    if (mesh.treePrototype >= l.treePrototypes.size()) return false;
    const auto& prototype = l.treePrototypes[mesh.treePrototype];
    if (prototype.vertices.size() != mesh.vertices.size()) return false;
    auto r = mesh.transform;
    for (auto x : r) if (!std::isfinite(x)) return false;
    if (r[3]!=0 || r[7]!=0 || r[11]!=0 || r[15]!=1) return false;
    for (int i=0; i<3; ++i)
    {
        const int o=4*i;
        p.scale[i]=std::sqrt(r[o]*r[o]+r[o+1]*r[o+1]+r[o+2]*r[o+2]);
        if (!(p.scale[i]>1e-8f) || !std::isfinite(p.scale[i])) return false;
        for (int j=0; j<3; ++j) r[o+j]/=p.scale[i];
    }
    const float det = r[0]*(r[5]*r[10]-r[6]*r[9])-r[1]*(r[4]*r[10]-r[6]*r[8])+r[2]*(r[4]*r[9]-r[5]*r[8]);
    if (std::abs(std::abs(det)-1)>2e-5f) return false;
    if (det<0) { p.scale[2]=-p.scale[2]; for (int j=0; j<3; ++j) r[8+j]=-r[8+j]; }
    for (int i=0; i<3; ++i) for (int j=i+1; j<3; ++j)
        if (std::abs(r[i*4]*r[j*4]+r[i*4+1]*r[j*4+1]+r[i*4+2]*r[j*4+2])>2e-5f) return false;
    const float cy=std::sqrt(r[5]*r[5]+r[1]*r[1]);
    p.rotation[0]=-std::atan2(-r[9],cy);
    if (cy>16.f*std::numeric_limits<float>::epsilon())
    {
        p.rotation[1]=-std::atan2(r[8],r[10]); p.rotation[2]=-std::atan2(r[1],r[5]);
    }
    else { p.rotation[1]=-std::atan2(-r[2],r[0]); p.rotation[2]=0; }
    for (int j=0; j<3; ++j) p.position[j]=mesh.transform[12+j];
    const auto composed=Compose(p);
    for (size_t i=0; i<prototype.vertices.size(); ++i)
        for (int j=0; j<3; ++j)
        {
            const auto& v=prototype.vertices[i].position;
            const float x=v[0]*composed[j]+v[1]*composed[4+j]+v[2]*composed[8+j]+composed[12+j];
            const double error=std::abs(double(x)-mesh.vertices[i].position[j]);
            if (!std::isfinite(error)) return false;
            p.maxPositionError=(std::max)(p.maxPositionError,error);
        }
    return p.maxPositionError<=1e-4; // measured world-space error, in metres
}

void InstallTreeLibrary(const Level& l, const std::string& objectsDirectory)
{
    ValidateLibrary(l);
    const auto plan=BuildTreeLibrary(l);
    const auto root=fs::canonical(fs::u8path(objectsDirectory));
    Check(fs::is_directory(root), "SDK objects directory does not exist");
    Check(!Inside(root,fs::canonical(fs::u8path(l.source))), "Cannot install objects inside source level");
    const auto dest=root/l.libraryName;
    if (fs::exists(dest))
    {
        Check(!fs::is_symlink(dest) && fs::is_directory(dest), "Recovery namespace is not a regular directory");
        for (size_t i=0; i<plan.models.size(); ++i)
            Check(EqualFile(dest/(ModelName(i)+".object"),NativeModel(l,plan.models[i])) &&
                  EqualFile(dest/(ModelName(i)+".normals.bin"),Normals(l,plan.models[i])),
                "Recovery library already exists with different contents; nothing was overwritten");
        return;
    }
    auto stage=dest; stage += ".partial";
    Check(fs::create_directory(stage), "Recovery install staging directory exists");
    WriteModels(l,plan,stage);
    Check(!fs::exists(dest), "Recovery library destination appeared during installation");
    fs::rename(stage,dest);
}

void ExportTreeLibrary(const Level& l, const std::string& newDirectory)
{
    ValidateLibrary(l);
    const auto plan=BuildTreeLibrary(l);
    Check(!newDirectory.empty(), "Empty tree export path");
    const auto out=fs::weakly_canonical(fs::absolute(fs::u8path(newDirectory)));
    Check(!Inside(out,fs::canonical(fs::u8path(l.source))), "Tree export must be outside the source level");
    Check(!fs::exists(out), "Tree export directory already exists");
    Check(fs::is_directory(out.parent_path()), "Tree export parent directory does not exist");
    auto stage=out; stage += ".partial";
    Check(fs::create_directory(stage), "Tree export staging directory exists");
    fs::create_directories(stage/"objects"/l.libraryName);
    WriteModels(l,plan,stage/"objects"/l.libraryName);
    auto models=TextFile(stage/"prototypes.csv");
    models << "model,object_reference,parts,vertices,triangles\n";
    auto parts=TextFile(stage/"model_parts.csv");
    parts << "model,mesh_index,geometry_prototype,vertices,triangles,source_shader_and_textures\n";
    for (size_t i=0; i<plan.models.size(); ++i)
    {
        uint64_t nv=0,nf=0;
        const auto& model=plan.models[i];
        for (size_t j=0; j<model.parts.size(); ++j)
        {
            const auto& p=l.treePrototypes[model.parts[j]];
            nv+=p.vertices.size(); nf+=p.triangles.size();
            parts << i << ',' << j << ',' << model.parts[j] << ',' << p.vertices.size() << ',' << p.triangles.size() << ',' << Csv(l.shaders[p.shader]) << '\n';
        }
        models << i << ',' << l.libraryName << '\\' << ModelName(i) << ',' << model.parts.size() << ',' << nv << ',' << nf << '\n';
    }
    models.close(); parts.close();
    auto instances=TextFile(stage/"instances.csv");
    instances << "visual,source_type,parent_visual,model,geometry_prototype,selection_included,max_position_error";
    for (int i=0; i<16; ++i) instances << ",matrix_" << i;
    for (int i=0; i<10; ++i) instances << ",tree_lighting_" << i;
    instances << '\n';
    Writer objects;
    uint32_t count=0, excluded=0;
    uint64_t expandedVertices=0, expandedTriangles=0, sharedVertices=0, sharedTriangles=0;
    double maxError=0;
    for (const auto& instance : plan.instances)
    {
        Placement p;
        bool included=true;
        double error=0;
        for (auto i : instance.meshes)
        {
            if (!TreePlacement(l,l.meshes[i],p)) included=false;
            error=(std::max)(error,p.maxPositionError);
        }
        if (included)
        {
            const auto object=SceneInstance(l,instance,p);
            objects.Chunk(count++,static_cast<const Writer&>(object));
            maxError=(std::max)(maxError,error);
        }
        else ++excluded;
        for (auto i : instance.meshes)
        {
            const auto& m=l.meshes[i];
            expandedVertices+=m.vertices.size(); expandedTriangles+=m.triangles.size();
            instances << m.visual << ',' << m.type << ',' << instance.visual << ',' << instance.model << ',' << m.treePrototype << ',' << included << ',' << error;
            for (auto x : m.transform) instances << ',' << x;
            for (auto x : m.treeLighting) instances << ',' << x;
            instances << '\n';
        }
    }
    instances.close();
    for (const auto& model : plan.models) for (auto part : model.parts)
    {
        const auto& p=l.treePrototypes[part]; sharedVertices+=p.vertices.size(); sharedTriangles+=p.triangles.size();
    }
    Writer selection;
    selection.Chunk(0x9df3,[](Writer& w) { w.U32(5); });
    selection.Chunk(0x7708,static_cast<const Writer&>(objects));
    Save(stage/"trees.selection",selection);
    auto report=TextFile(stage/"tree-recovery.txt");
    report << "X-Ray tree instance recovery 0.2\nSource: " << l.source << "\nLibrary: " << l.libraryName
        << "\nGeometry prototypes (geometry + source material): " << l.treePrototypes.size()
        << "\nNative models (whole source LOD groups where available): " << plan.models.size()
        << "\nNative selection instances: " << count << "\nInstances excluded from selection (unsupported transform): " << excluded
        << "\nMaximum checked position error (metres): " << maxError
        << "\nExpanded tree vertices: " << expandedVertices << "\nShared tree vertices: " << sharedVertices
        << "\nExpanded tree triangles: " << expandedTriangles << "\nShared tree triangles: " << sharedTriangles
        << "\n\nCopy objects/<library> into the SDK objects library, then File > Open Selection: trees.selection."
        << "\nThe SDK recovery window can install and import this selection into a scene without existing static objects."
        << "\nPreview uses editor\\wire; source render materials are in model_parts.csv. Physics and compiler shaders are deliberately unmapped."
        << "\nSDK Build and Make Game reject recovered objects until their materials are mapped explicitly. No collision is generated by recovery."
        << "\nNative .object stores positions, triangles and UVs; SDK recomputes its shading normals. Original decoded local normals are in *.normals.bin (XRNORM01, u32 count, float3[count])."
        << "\nSource tree lighting is preserved in instances.csv; native selection does not reproduce baked lighting or wind."
        << "\nThis is the recovered vegetation layer, not a complete editable level. Static object reconstruction, collision mapping and other scene layers remain separate.\n";
    report.close();
    Check(!fs::exists(out), "Tree export destination appeared during export");
    fs::rename(stage,out);
}
}
