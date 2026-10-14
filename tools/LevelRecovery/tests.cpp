#include "../../Editors/LevelEditor/Recovery/LevelRecovery.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace
{
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
void Check(bool c, const char* message) { if (!c) throw std::runtime_error(message); }
template<class T> void Put(Bytes& b, T x)
{
    const auto p = reinterpret_cast<const uint8_t*>(&x);
    b.insert(b.end(), p, p+sizeof(T));
}
void U32(Bytes& b, uint32_t x) { Put(b,x); }
void U16(Bytes& b, uint16_t x) { Put(b,x); }
void F32(Bytes& b, float x) { Put(b,x); }
void Chunk(Bytes& b, uint32_t id, const Bytes& body)
{
    U32(b,id); U32(b,uint32_t(body.size())); b.insert(b.end(),body.begin(),body.end());
}
Bytes Integers(std::initializer_list<uint32_t> values)
{
    Bytes b; for (auto x : values) U32(b,x); return b;
}
void Save(const fs::path& p, const Bytes& b)
{
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()), b.size());
    Check(bool(f), "fixture write failed");
}
void Reject(const std::function<void()>& fn)
{
    bool rejected = false;
    try { fn(); } catch (const std::exception&) { rejected = true; }
    Check(rejected, "malformed/unsafe input was accepted");
}
Bytes Fixture(const fs::path& dir, bool invalidIndex = false, bool tree = false, bool repeat = false, bool materialVariant = false, bool group = false)
{
    fs::create_directories(dir);
    Bytes header; U16(header,14); U16(header,2);
    Bytes vb; U32(vb,1);
    auto element = [&](uint16_t stream,uint16_t offset,uint8_t type,uint8_t usage)
    {
        U16(vb,stream); U16(vb,offset); Put(vb,type); Put(vb,uint8_t(0)); Put(vb,usage); Put(vb,uint8_t(0));
    };
    element(0,0,2,0); element(0,12,4,3); element(0,16,4,6); element(0,20,4,7); element(0,24,6,5); element(255,0,17,0);
    U32(vb,3);
    const float positions[3][3] = {{0,0,0},{1,0,0},{0,0,1}};
    for (const auto& p : positions)
    {
        for (auto x : p) F32(vb,x);
        U32(vb,0x007fff7f); U32(vb,0x80000000); U32(vb,0x40000000);
        Put(vb,int16_t(1024)); Put(vb,int16_t(-512));
    }
    Bytes ib; U32(ib,1); U32(ib,6);
    for (const auto i : {0,1,2,0,2,1}) U16(ib,uint16_t(invalidIndex ? 9 : i));
    Bytes geom; Chunk(geom,1,header); Chunk(geom,9,vb); Chunk(geom,10,ib); Save(dir/"level.geom",geom);
    Bytes ogfHeader; Put(ogfHeader,uint8_t(4)); Put(ogfHeader,uint8_t(tree ? 7 : 2)); U16(ogfHeader,0);
    for (int i=0; i<10; ++i) F32(ogfHeader,0);
    Bytes window(16,0); U32(window,1); U32(window,3); U16(window,1); U16(window,3);
    Bytes visual; Chunk(visual,1,ogfHeader); Chunk(visual,21,Integers({0,0,3,0,0,6})); Chunk(visual,6,window);
    if (tree)
    {
        Bytes transform;
        for (const auto x : {0.f,0.f,-1.f,0.f, 0.f,1.f,0.f,0.f, 1.f,0.f,0.f,0.f, 10.f,20.f,30.f,1.f}) F32(transform,x);
        for (int i=0; i<10; ++i) F32(transform,0);
        Chunk(visual,12,transform);
    }
    Bytes visuals; Chunk(visuals,0,visual);
    if (repeat)
    {
        Bytes secondHeader=ogfHeader;
        if (materialVariant) secondHeader[2]=1;
        Bytes second;
        Chunk(second,1,secondHeader); Chunk(second,21,Integers({0,0,3,0,0,6}));
        Bytes transform;
        const std::array<float,16> other = group ? std::array<float,16>{{0,0,-1,0, 0,1,0,0, 1,0,0,0, 10,20,30,1}} :
            std::array<float,16>{{1,0,0,0, 0,1,0,0, 0,0,1,0, -100,2,50,1}};
        for (const auto x : other) F32(transform,x);
        for (int i=0; i<10; ++i) F32(transform,float(i));
        Chunk(second,12,transform); Chunk(visuals,1,second);
    }
    if (group)
    {
        auto lodHeader=ogfHeader; lodHeader[1]=6;
        Bytes lod; Chunk(lod,1,lodHeader); Chunk(lod,10,Integers({2,0,1})); Chunk(visuals,2,lod);
    }
    Bytes shaders; U32(shaders,materialVariant ? 2 : 1); shaders.push_back(0);
    if (materialVariant)
    {
        const std::string shader="trees\\variant/trees\\leaf";
        shaders.insert(shaders.end(),shader.begin(),shader.end()); shaders.push_back(0);
    }
    Bytes level; Chunk(level,1,header); Chunk(level,2,shaders); Chunk(level,3,visuals); Save(dir/"level",level);
    Bytes cform = Integers({4,3,1});
    for (int i=0; i<6; ++i) F32(cform,0);
    for (const auto& p : positions) for (auto x : p) F32(cform,x);
    for (auto i : {0u,1u,2u,0x002ac123u}) U32(cform,i);
    Save(dir/"level.cform",cform);
    return cform;
}
LevelRecovery::Mesh Mesh(std::initializer_list<std::array<float,3>> positions)
{
    LevelRecovery::Mesh m;
    for (const auto& p : positions) { LevelRecovery::Vertex v; v.position=p; m.vertices.push_back(v); }
    for (uint32_t i=0; i<m.vertices.size(); i+=3) { LevelRecovery::Triangle t; t.vertices={i,i+1,i+2}; m.triangles.push_back(t); }
    return m;
}
void Topology()
{
    LevelRecovery::Level l;
    l.meshes.push_back(Mesh({{0,0,0},{1,0,0},{0,0,1}}));
    l.meshes.push_back(Mesh({{1,0,0},{0,0,0},{0,1,0}}));
    l.meshes[1].shader=1; // batching/material seam must not split the connected surface
    l.meshes.push_back(Mesh({{0,0,1},{2,0,1},{2,0,2}})); // shared vertex alone must not merge
    const auto before = l.meshes[0].vertices[1].position;
    LevelRecovery::GroupConnectedSurfaces(l);
    Check(l.componentCount==2, "edge connectivity/material seam/vertex contact");
    Check(l.meshes[0].vertices[1].position==before, "grouping modified vertex payload");
    // A close edge must not be welded.
    l.meshes[1].vertices[0].position[0]+=0.00001f;
    LevelRecovery::GroupConnectedSurfaces(l);
    Check(l.componentCount==3, "nearby positions were welded");
    // Existing instance transform is stronger evidence than leaf-card connectivity.
    l.meshes[0]=Mesh({{0,0,0},{1,0,0},{0,0,1},{3,0,0},{4,0,0},{3,0,1}});
    l.meshes[0].type=7; l.meshes[0].visual=9;
    LevelRecovery::GroupConnectedSurfaces(l);
    Check(l.meshes[0].triangles[0].component==l.meshes[0].triangles[1].component, "tree instance was split into cards");
    // Three unrelated triangles on one edge: do not choose a pairing silently.
    l.meshes.clear();
    l.meshes.push_back(Mesh({{0,0,0},{1,0,0},{0,0,1},{0,0,0},{1,0,0},{0,1,0},{0,0,0},{1,0,0},{0,-1,0}}));
    LevelRecovery::GroupConnectedSurfaces(l);
    Check(l.componentCount==3 && l.ambiguousEdges==1, "nonmanifold edge was guessed");
}
Bytes File(const fs::path& path)
{
    std::ifstream f(path,std::ios::binary | std::ios::ate);
    Check(bool(f),"missing native export file");
    Bytes b(size_t(f.tellg())); f.seekg(0); f.read(reinterpret_cast<char*>(b.data()),b.size());
    Check(bool(f),"native export read"); return b;
}
template<class T> T At(const Bytes& b,size_t offset)
{
    Check(offset+sizeof(T)<=b.size(),"native record truncated");
    T x; std::memcpy(&x,b.data()+offset,sizeof(T)); return x;
}
Bytes Data(const Bytes& b,uint32_t id)
{
    for (size_t o=0; o<b.size();)
    {
        const auto type=At<uint32_t>(b,o), size=At<uint32_t>(b,o+4);
        Check(size<=b.size()-o-8,"native chunk truncated");
        if (type==id) return Bytes(b.begin()+o+8,b.begin()+o+8+size);
        o+=size+8;
    }
    throw std::runtime_error("native chunk missing");
}
void Reworld(const LevelRecovery::Level& l,LevelRecovery::Mesh& mesh)
{
    const auto& p=l.treePrototypes.at(mesh.treePrototype);
    for (size_t i=0; i<p.vertices.size(); ++i)
        for (int j=0; j<3; ++j)
        {
            const auto& v=p.vertices[i].position;
            mesh.vertices[i].position[j]=v[0]*mesh.transform[j]+v[1]*mesh.transform[4+j]+v[2]*mesh.transform[8+j]+mesh.transform[12+j];
        }
}
void NativeTrees(const fs::path& dir)
{
    Fixture(dir/"repeated",false,true,true);
    auto l=LevelRecovery::Read((dir/"repeated").u8string());
    Check(l.meshes.size()==2 && l.treePrototypes.size()==1,"repeated trees did not share a prototype");
    Check(l.treePrototypes[0].vertices[1].position==std::array<float,3>{1,0,0},"local coordinates were reconstructed with a lossy inverse");
    Check(l.meshes[1].treeLighting[9]==9,"per-instance lighting dropped");
    LevelRecovery::Placement placement;
    for (const auto& m : l.meshes) Check(LevelRecovery::TreePlacement(l,m,placement),"rigid instance placement rejected");
    auto transformed=l.meshes[0];
    for (int i=0; i<3; ++i) for (int j=0; j<3; ++j) transformed.transform[4*i+j]*=float(i+2);
    Reworld(l,transformed);
    Check(LevelRecovery::TreePlacement(l,transformed,placement),"nonuniform scale rejected");
    for (int j=0; j<3; ++j) transformed.transform[8+j]*=-1;
    Reworld(l,transformed);
    Check(LevelRecovery::TreePlacement(l,transformed,placement) && placement.scale[2]<0,"reflected instance lost");
    transformed.transform[4]+=0.25f; Reworld(l,transformed);
    Check(!LevelRecovery::TreePlacement(l,transformed,placement),"sheared transform approximated silently");
    transformed.transform={{1,0,0,0, 0,0,-1,0, 0,1,0,0, 0,0,0,1}};
    Reworld(l,transformed);
    Check(LevelRecovery::TreePlacement(l,transformed,placement),"gimbal-lock placement failed");
    for (int j=0; j<3; ++j) transformed.transform[j]=0;
    Reworld(l,transformed);
    Check(!LevelRecovery::TreePlacement(l,transformed,placement),"singular instance accepted");
    LevelRecovery::ExportTreeLibrary(l,(dir/"native").u8string());
    const auto root=dir/"native"/"objects"/l.libraryName;
    Check(fs::exists(root/"tree_00000.object") && !fs::exists(root/"tree_00001.object"),"duplicate native object written");
    const auto body=Data(File(root/"tree_00000.object"),0x7777);
    Check(At<uint16_t>(Data(body,0x0900),0)==0x10,"native object version");
    Check(At<uint32_t>(Data(body,0x0903),0)==16,"native object does not preserve multiple usage");
    const auto mesh=Data(Data(body,0x0910),0);
    const auto vertices=Data(mesh,0x1005), faces=Data(mesh,0x1006), vmrefs=Data(mesh,0x1008);
    Check(At<uint32_t>(vertices,0)==3 && vertices.size()==40,"native vertices layout");
    Check(At<uint32_t>(faces,0)==2 && faces.size()==52,"native triangle layout");
    Check(At<uint32_t>(faces,12)==1 && At<uint32_t>(faces,16)==1,"native position/UV-reference indices");
    Check(At<uint32_t>(vmrefs,0)==3 && vmrefs.size()==31,"native vertex-map reference layout");
    const auto uv=Data(mesh,0x1012);
    Check(At<float>(uv,19)==0.5f && At<float>(uv,23)==-0.25f,"native UVs flipped or corrupted");
    const auto normals=File(root/"tree_00000.normals.bin");
    Check(normals.size()==48 && At<uint32_t>(normals,8)==3,"original normal sidecar");
    const auto selection=Data(File(dir/"native"/"trees.selection"),0x7708);
    const auto object0=Data(Data(selection,0),0x7777), object1=Data(Data(selection,1),0x7777);
    Check(Data(object0,0x0902)==Data(object1,0x0902),"selection duplicated references");
    Check(At<float>(Data(object1,0xF903),0)==-100.f,"selection instance translation");
    Reject([&] { LevelRecovery::ExportTreeLibrary(l,(dir/"native").u8string()); });
    Reject([&] { LevelRecovery::ExportTreeLibrary(l,(dir/"repeated"/"export").u8string()); });
    fs::create_directory(dir/"sdk-objects");
    LevelRecovery::InstallTreeLibrary(l,(dir/"sdk-objects").u8string());
    LevelRecovery::InstallTreeLibrary(l,(dir/"sdk-objects").u8string()); // idempotent reuse
    const auto installed=dir/"sdk-objects"/l.libraryName/"tree_00000.object";
    Save(installed,Bytes{1,2,3});
    Reject([&] { LevelRecovery::InstallTreeLibrary(l,(dir/"sdk-objects").u8string()); });
    Check(File(installed)==Bytes({1,2,3}),"modified library file was overwritten");
    Fixture(dir/"variant",false,true,true,true);
    Check(LevelRecovery::Read((dir/"variant").u8string()).treePrototypes.size()==2,"different source materials merged");
    Fixture(dir/"whole-tree",false,true,true,true,true);
    auto whole=LevelRecovery::Read((dir/"whole-tree").u8string());
    Check(whole.treeGroups.size()==1 && whole.lodCount==1,"source LOD child references lost");
    const auto plan=LevelRecovery::BuildTreeLibrary(whole);
    Check(plan.models.size()==1 && plan.instances.size()==1 && plan.models[0].parts.size()==2,"trunk and foliage were not recovered as one object");
    LevelRecovery::ExportTreeLibrary(whole,(dir/"whole-native").u8string());
    const auto combined=Data(File(dir/"whole-native"/"objects"/whole.libraryName/"tree_00000.object"),0x7777);
    Check(At<uint32_t>(Data(combined,0x0907),0)==2,"whole tree material surfaces merged");
    Check(!Data(Data(combined,0x0910),1).empty(),"whole tree second mesh missing");
    whole.treeGroups.clear();
    Check(LevelRecovery::BuildTreeLibrary(whole).instances.size()==2,"coincident transforms used to guess object grouping");
    whole.treeGroups={{2,{0,1}},{3,{0}}};
    Check(LevelRecovery::BuildTreeLibrary(whole).instances.size()==2,"ambiguous source parents duplicated an instance");
    auto partial=l;
    partial.meshes[1].transform[4]+=0.25f;
    Reworld(partial,partial.meshes[1]);
    LevelRecovery::ExportTreeLibrary(partial,(dir/"partial-native").u8string());
    const auto partialSelection=Data(File(dir/"partial-native"/"trees.selection"),0x7708);
    Check(!Data(partialSelection,0).empty(),"representable instance excluded");
    Reject([&] { Data(partialSelection,1); });
    const auto report=File(dir/"partial-native"/"tree-recovery.txt");
    Check(std::string(report.begin(),report.end()).find("Instances excluded from selection (unsupported transform): 1")!=std::string::npos,"unsupported placement was excluded without a report");
    const auto source=File(dir/"whole-tree"/"level");
    const auto originalVisuals=Data(source,3);
    Bytes brokenVisuals,brokenLod,brokenLevel;
    Chunk(brokenVisuals,0,Data(originalVisuals,0)); Chunk(brokenVisuals,1,Data(originalVisuals,1));
    Chunk(brokenLod,1,Data(Data(originalVisuals,2),1)); Chunk(brokenLod,10,Integers({1,9999}));
    Chunk(brokenVisuals,2,brokenLod);
    Chunk(brokenLevel,1,Data(source,1)); Chunk(brokenLevel,2,Data(source,2)); Chunk(brokenLevel,3,brokenVisuals);
    Save(dir/"whole-tree"/"level",brokenLevel);
    Reject([&] { LevelRecovery::Read((dir/"whole-tree").u8string()); });
    auto invalid=l; invalid.libraryName="..\\escape";
    Reject([&] { LevelRecovery::InstallTreeLibrary(invalid,(dir/"sdk-objects").u8string()); });
}
void StaticRecovery(const fs::path& dir)
{
    Fixture(dir/"static-source");
    auto l=LevelRecovery::Read((dir/"static-source").u8string());
    auto second=l.meshes[0]; second.visual=1;
    for (auto& v : second.vertices) { v.position[0]+=16; v.position[2]+=8; }
    l.meshes.push_back(second);
    LevelRecovery::GroupConnectedSurfaces(l);
    auto plan=LevelRecovery::BuildStaticLibrary(l);
    Check(plan.instances.size()==2 && plan.models.size()==1,"exact translated static instance not shared");
    Check(plan.instances[1].origin==std::array<float,3>{16,0,8},"static placement origin");
    const auto before=l.collisionBytes;
    LevelRecovery::ExportSceneLibrary(l,plan,(dir/"static-native").u8string());
    Check(File(dir/"static-native"/"original.cform")==before,"native scene changed physics");
    const auto body=Data(File(dir/"static-native"/"objects"/LevelRecovery::StaticLibraryName(l)/"static_00000.object"),0x7777);
    Check(At<uint32_t>(Data(body,0x0903),0)==0,"static surface incorrectly marked as tree/multiple usage");
    const auto selection=Data(File(dir/"static-native"/"geometry.selection"),0x7708);
    Check(Data(Data(selection,0),0x7777).size()>0 && Data(Data(selection,1),0x7777).size()>0,"static selection missing instance");
    Reject([&] { LevelRecovery::ExportSceneLibrary(l,plan,(dir/"static-native").u8string()); });
    Reject([&] { LevelRecovery::ExportSceneLibrary(l,plan,(dir/"static-source"/"unsafe").u8string()); });
    fs::create_directory(dir/"static-sdk");
    LevelRecovery::InstallStaticLibrary(l,plan,(dir/"static-sdk").u8string());
    LevelRecovery::InstallStaticLibrary(l,plan,(dir/"static-sdk").u8string());
    const auto installed=dir/"static-sdk"/LevelRecovery::StaticLibraryName(l)/"static_00000.object";
    Save(installed,Bytes{9,8,7});
    Reject([&] { LevelRecovery::InstallStaticLibrary(l,plan,(dir/"static-sdk").u8string()); });
    Check(File(installed)==Bytes({9,8,7}),"static installer overwrote edited object");
    l.meshes[1].vertices[1].position[0]+=0.000015f; // same fingerprint bucket, different actual geometry
    Check(LevelRecovery::BuildStaticLibrary(l).models.size()==2,"quantization authorized approximate mesh replacement");
    l.meshes[1]=second; l.meshes[1].vertices[0].uv[0]+=0.25f;
    LevelRecovery::GroupConnectedSurfaces(l);
    Check(LevelRecovery::BuildStaticLibrary(l).models.size()==2,"different static UVs were merged");
    l.meshes[1]=second; l.meshes[1].shader=1; l.shaders.push_back("other/texture");
    LevelRecovery::GroupConnectedSurfaces(l);
    Check(LevelRecovery::BuildStaticLibrary(l).models.size()==2,"different static materials were merged");
    // Two-sided quad: collapse geometry for adjacency, retain every source face.
    l.meshes.clear();
    auto quad=Mesh({{0,0,0},{1,0,0},{0,0,1},{1,0,0},{1,0,1},{0,0,1}});
    auto back0=quad.triangles[0],back1=quad.triangles[1];
    std::swap(back0.vertices[0],back0.vertices[2]); std::swap(back1.vertices[0],back1.vertices[2]);
    quad.triangles.push_back(back0); quad.triangles.push_back(back1); l.meshes.push_back(quad);
    LevelRecovery::GroupConnectedSurfaces(l);
    Check(l.componentCount==1 && l.coincidentFaceCopies==2,"coincident backsides split a connected quad");
    const auto quadPlan=LevelRecovery::BuildStaticLibrary(l);
    Check(quadPlan.instances[0].faces.size()==4 && quadPlan.models[0].parts[0].triangles.size()==4,"duplicate-aware grouping deleted triangles");
    // Sharing indices is part of the exact match, even with identical corner attributes.
    l.meshes[0]=Mesh({{0,0,0},{1,0,0},{0,0,1},{1,0,0},{1,0,1},{0,0,1}});
    second=l.meshes[0]; second.visual=1;
    second.triangles[1].vertices={{1,4,2}};
    for (auto& v : second.vertices) v.position[0]+=16;
    l.meshes.push_back(second); LevelRecovery::GroupConnectedSurfaces(l);
    Check(LevelRecovery::BuildStaticLibrary(l).models.size()==2,"vertex-sharing topology changed during model reuse");
    // Ground crossing zero: centring must not round away source float positions.
    l.meshes.clear();
    auto terrain=Mesh({{-729.63458f,0,-422.9516f},{0.0000123f,0,0.0000321f},{270.36536f,0,577.0484f}});
    l.meshes.push_back(terrain); LevelRecovery::GroupConnectedSurfaces(l);
    const auto ground=LevelRecovery::BuildStaticLibrary(l);
    Check(ground.instances[0].origin[0]==0 && ground.instances[0].origin[2]==0,"terrain centring loses source coordinate precision");
}
}
int main()
{
    try
    {
        Topology();
        const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto dir = fs::temp_directory_path()/fs::u8path("xray-recovery-tests-"+unique);
        Check(fs::create_directory(dir), "test directory exists");
        const auto original = Fixture(dir/"source");
        auto l = LevelRecovery::Read((dir/"source").u8string());
        Check(l.meshes.size()==1 && l.meshes[0].triangles.size()==1, "progressive LODs were concatenated");
        Check(l.meshes[0].triangles[0].vertices[1]==2, "progressive offset ignored");
        Check(std::abs(l.meshes[0].vertices[0].uv[0]-(1024.f+128.f/255.f)/1024.f)<1e-7f, "packed UV correction lost");
        Check(l.collisionFaces[0].attributes==0x002ac123u && l.collisionBytes==original, "collision attributes changed");
        LevelRecovery::GroupConnectedSurfaces(l);
        Reject([&] { LevelRecovery::Export(l,(dir/"source"/"export").u8string()); });
        LevelRecovery::Export(l,(dir/"export").u8string());
        Check(fs::file_size(dir/"export"/"original.cform")==original.size(), "CFORM export size");
        Reject([&] { LevelRecovery::Export(l,(dir/"export").u8string()); });
        Fixture(dir/"bad-index",true);
        Reject([&] { LevelRecovery::Read((dir/"bad-index").u8string()); });
        Fixture(dir/"truncated");
        fs::resize_file(dir/"truncated"/"level.cform",40);
        Reject([&] { LevelRecovery::Read((dir/"truncated").u8string()); });
        Fixture(dir/"compressed");
        Bytes compressed; Chunk(compressed,0x80000001u,{}); Save(dir/"compressed"/"level",compressed);
        Reject([&] { LevelRecovery::Read((dir/"compressed").u8string()); });
        Fixture(dir/"v13");
        // Same supported DX9 layout with a SoC-era version tag.
        for (const auto name : {"level", "level.geom"})
        {
            std::fstream f(dir/"v13"/name, std::ios::in | std::ios::out | std::ios::binary);
            f.seekp(8); const char version[2] = {13,0}; f.write(version,2);
        }
        Check(LevelRecovery::Read((dir/"v13").u8string()).version==13, "version 13 adapter");
        Fixture(dir/"tree",false,true);
        const auto tree = LevelRecovery::Read((dir/"tree").u8string());
        Check(tree.meshes[0].vertices[1].position==std::array<float,3>{10,20,29}, "tree transform orientation/translation");
        Check(tree.meshes[0].vertices[0].uv[0]==0.5f && tree.meshes[0].vertices[0].uv[1]==-0.25f, "tree UV scale");
        NativeTrees(dir);
        StaticRecovery(dir);
        std::cout << "PASS: static duplicate-aware adjacency, exact translated reuse, topology/UV/material separation, terrain precision, native scene and collision snapshot; tree/SRT/parser/export/install regressions\nFixtures: " << dir.u8string() << '\n';
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
