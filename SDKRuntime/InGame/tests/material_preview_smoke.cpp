#include "../SharedScene.h"
#include "../PreviewGeometryFormat.h"
#include <cstdio>
#include <string>
#include <vector>
#include <cassert>
#include <filesystem>

struct Bytes
{
    std::vector<uint8_t> data;
    template<class T> void add(const T& v) { const auto* b=reinterpret_cast<const uint8_t*>(&v); data.insert(data.end(),b,b+sizeof(v)); }
    void str(const char* s) { data.insert(data.end(),s,s+strlen(s)+1); }
    void chunk(uint32_t id,const Bytes& payload) { add(id); add(uint32_t(payload.data.size())); data.insert(data.end(),payload.data.begin(),payload.data.end()); }
};
struct Header { uint8_t version=4,type=0; uint16_t shader=0; float min[3]={-10,0,-2},max[3]={10,3,12},center[3]={0,1.5f,5},radius=14; };
struct V { float x,y,z,nx,ny,nz,u,v; };
static_assert(sizeof(Header)==44 && sizeof(V)==32);
std::vector<uint8_t> mesh(const char* shader,const char* texture,const std::vector<V>& vertices)
{
    Bytes all,header,tex,verts,idx,light;
    header.add(Header{}); all.chunk(1,header);
    tex.str(texture);tex.str(shader);all.chunk(2,tex);
    verts.add(uint32_t(0x112));verts.add(uint32_t(vertices.size()));
    for(const auto& v:vertices) verts.add(v);
    all.chunk(3,verts);
    idx.add(uint32_t(vertices.size()));for(uint16_t i=0;i<vertices.size();++i)idx.add(i);
    all.chunk(4,idx);
    light.add(SDKInGame::previewLightingVersion);light.add(uint32_t(1));light.add(uint32_t(vertices.size()));
    for(size_t i=0;i<vertices.size();++i)light.add(SDKInGame::PreviewLighting{.4f,1});
    all.chunk(SDKInGame::previewLightingChunk,light);
    return all.data;
}
int wmain(int argc,wchar_t** argv)
{
    if(argc!=3) return 1;
    SDKInGame::SceneBuilder builder;
    const auto dataRoot=(std::filesystem::path(argv[2]).parent_path()/L"gamedata").u8string()+"\\";
    strcpy_s(builder.info.sdkDataRoot,dataRoot.c_str());
    strcpy_s(builder.info.textureRoot,(dataRoot+"textures\\").c_str());
    builder.info.camera={0,2,-1}; builder.info.angles={0,0,0};
    builder.vertices={{-10,0,-2},{10,0,-2},{10,0,12},{-10,0,12}};
    builder.triangles={{{0,2,1},0},{{0,3,2},0}};
    const std::vector<V> left={{-8,0,0,0,1,0,0,0},{-8,0,10,0,1,0,0,1},{0,0,0,0,1,0,1,0},
        {0,0,0,0,1,0,1,0},{-8,0,10,0,1,0,0,1},{0,0,10,0,1,0,1,1}};
    std::vector<V> right=left;for(auto& v:right)v.x+=8;
    const std::vector<V> bush={{-1,0,3,0,0,-1,0,1},{1,2.5f,3,0,0,-1,1,0},{1,0,3,0,0,-1,1,1},
        {-1,0,3,0,0,-1,0,1},{-1,2.5f,3,0,0,-1,0,0},{1,2.5f,3,0,0,-1,1,0}};
    const auto append=[&](const char* shader,const char* texture,const std::vector<V>& vertices){
        const auto ogf=mesh(shader,texture,vertices);
        SDKInGame::Instance inst{};inst.transform[0]=inst.transform[5]=inst.transform[10]=inst.transform[15]=1;
        inst.mesh=builder.AddMesh(ogf.data(),ogf.size());builder.instances.push_back(inst);
    };
    append("def_shaders\\def_vertex","crete\\crete_beton_0",left);
    append("levels\\bar_asfalt","terrain\\terrain_bar",right);
    append("flora\\leaf_wave","trees\\trees_a_bush",bush);
    auto bytes=builder.Finish();SDKInGame::SceneView check;const char* error=nullptr;
    assert(check.Open(bytes.data(),bytes.size(),error));
    // The new flags are bounded to material/suppression bits and remain strict.
    auto old=bytes;
    auto* oldHeader=reinterpret_cast<SDKInGame::Header*>(old.data());oldHeader->version=4;
    assert(!check.Open(old.data(),old.size(),error));
    old=bytes;oldHeader=reinterpret_cast<SDKInGame::Header*>(old.data());
    memset(oldHeader->sdkDataRoot,'x',sizeof(oldHeader->sdkDataRoot));
    assert(!check.Open(old.data(),old.size(),error));
    builder.triangles[0].material=0x10000;
    auto invalid=builder.Finish();assert(!check.Open(invalid.data(),invalid.size(),error));
    builder.triangles[0].material=0xc005;invalid=builder.Finish();assert(check.Open(invalid.data(),invalid.size(),error));
    const std::wstring name=L"Local\\SDKMaterialSmoke."+std::to_wstring(GetCurrentProcessId());
    SDKInGame::SharedScene mapping;assert(mapping.Create(name,bytes));
    std::wstring command=L"\""+std::wstring(argv[1])+L"\" -use-work-dir -fsltx fsgame.runtime.ltx -ltx sdk-preview.ltx -sdk-preview "+name+L" -sdk-preview-test -nointro -nosplash";
    STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(argv[1],command.data(),nullptr,nullptr,FALSE,0,nullptr,argv[2],&startup,&process))return 2;
    CloseHandle(process.hThread);
    const auto wait=WaitForSingleObject(process.hProcess,180000);
    if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,124);CloseHandle(process.hProcess);return 124;}
    DWORD result;GetExitCodeProcess(process.hProcess,&result);CloseHandle(process.hProcess);
    printf("Material preview engine smoke test exit: %lu\n",result);return int(result);
}
