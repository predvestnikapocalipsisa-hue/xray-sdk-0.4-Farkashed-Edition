#include "LevelRecovery.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

namespace LevelRecovery
{
namespace
{
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
using Vec3 = std::array<float, 3>;
void Require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}
struct Reader
{
    const uint8_t* data = nullptr;
    size_t size = 0, offset = 0;
    Reader() = default;
    Reader(const uint8_t* p, size_t n) : data(p), size(n) {}
    explicit Reader(const Bytes& b) : Reader(b.data(), b.size()) {}
    size_t Remaining() const { return size - offset; }
    const uint8_t* Take(size_t n)
    {
        Require(n <= Remaining(), "Truncated binary record");
        const auto p = data + offset;
        offset += n;
        return p;
    }
    template<class T> T Get()
    {
        T v;
        std::memcpy(&v, Take(sizeof(T)), sizeof(T));
        return v;
    }
    uint32_t U32() { return Get<uint32_t>(); }
    float Float()
    {
        const auto v = Get<float>();
        Require(std::isfinite(v), "Non-finite coordinate or transform");
        return v;
    }
    std::string String()
    {
        const size_t start = offset;
        while (offset < size && data[offset]) ++offset;
        Require(offset < size, "Unterminated string");
        const std::string s(reinterpret_cast<const char*>(data + start), offset - start);
        ++offset;
        Require(s.size() < 4096, "Oversized shader/texture name");
        return s;
    }
    void End() const { Require(offset == size, "Unexpected trailing bytes in known record"); }
};
using Chunks = std::map<uint32_t, Reader>;
Chunks Split(Reader r)
{
    Chunks result;
    while (r.Remaining())
    {
        const auto id = r.U32(), n = r.U32();
        Require(!(id & 0x80000000u), "Compressed chunks require a separate format adapter");
        const auto p = r.Take(n);
        Require(result.emplace(id, Reader(p, n)).second, "Duplicate chunk ID");
    }
    return result;
}
Reader Chunk(const Chunks& c, uint32_t id)
{
    const auto i = c.find(id);
    Require(i != c.end(), "Missing chunk " + std::to_string(id));
    return i->second;
}
Bytes Load(const fs::path& path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    Require(bool(f), "Cannot open " + path.u8string());
    const auto n = f.tellg();
    Require(n >= 0 && uint64_t(n) <= 1024ull * 1024 * 1024, "Input file exceeds 1 GiB limit");
    Bytes b(static_cast<size_t>(n));
    f.seekg(0);
    if (!b.empty()) f.read(reinterpret_cast<char*>(b.data()), b.size());
    Require(bool(f), "Cannot read " + path.u8string());
    return b;
}
struct Element { uint16_t stream, offset; uint8_t type, method, usage, index; };
static_assert(sizeof(Element) == 8, "D3D9 declaration layout");
struct Buffer
{
    std::vector<Element> elements;
    uint32_t count = 0, stride = 0;
    Reader bytes;
    Element Find(uint8_t usage, uint8_t index = 0) const
    {
        for (const auto& e : elements) if (e.usage == usage && e.index == index) return e;
        throw std::runtime_error("Missing vertex semantic " + std::to_string(usage));
    }
};
std::vector<Buffer> VertexBuffers(Reader r)
{
    const auto count = r.U32();
    Require(count <= r.Remaining() / 12, "Invalid VB count");
    std::vector<Buffer> buffers;
    const uint32_t sizes[] = {4,8,12,16,4,4,4,8,4,4,8,4,8,4,4,4,8};
    for (uint32_t i = 0; i < count; ++i)
    {
        Buffer b;
        for (;;)
        {
            const auto e = r.Get<Element>();
            if (e.stream == 255)
            {
                Require(e.type == 17, "Invalid declaration terminator");
                break;
            }
            Require(b.elements.size() < 64 && e.stream == 0 && e.method == 0 && e.type < 17,
                "Unsupported vertex declaration");
            b.stride = (std::max)(b.stride, uint32_t(e.offset) + sizes[e.type]);
            b.elements.push_back(e);
        }
        b.count = r.U32();
        Require(b.stride > 0 && b.count <= r.Remaining() / b.stride, "VB outside chunk");
        const size_t n = size_t(b.count) * b.stride;
        b.bytes = Reader(r.Take(n), n);
        buffers.push_back(b);
    }
    r.End();
    return buffers;
}
std::vector<Reader> IndexBuffers(Reader r)
{
    const auto count = r.U32();
    Require(count <= r.Remaining() / 4, "Invalid IB count");
    std::vector<Reader> result;
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto n = r.U32();
        Require(n <= r.Remaining() / 2, "IB outside chunk");
        result.emplace_back(r.Take(size_t(n) * 2), size_t(n) * 2);
    }
    r.End();
    return result;
}
struct Window { uint32_t offset; uint16_t triangles, vertices; };
std::vector<Window> Windows(Reader r)
{
    r.Take(16);
    const auto count = r.U32();
    Require(count && count <= r.Remaining() / 8, "Invalid progressive window count");
    std::vector<Window> windows;
    for (uint32_t i = 0; i < count; ++i) windows.push_back(r.Get<Window>());
    r.End();
    return windows;
}
Vec3 Transform(const Vec3& p, const std::array<float, 16>& m, bool direction)
{
    Vec3 v{};
    for (int j = 0; j < 3; ++j)
    {
        v[j] = p[0] * m[j] + p[1] * m[4+j] + p[2] * m[8+j] + (direction ? 0.f : m[12+j]);
        Require(std::isfinite(v[j]), "Transform overflow");
    }
    return v;
}
void Normalize(Vec3& p)
{
    const double length = std::sqrt(double(p[0])*p[0] + double(p[1])*p[1] + double(p[2])*p[2]);
    Require(length > 1e-20 && std::isfinite(length), "Invalid vertex normal");
    for (auto& v : p) v = float(v / length);
}
Vertex Decode(const Buffer& b, uint32_t index, bool tree)
{
    const auto p = b.Find(0), n = b.Find(3), uv = b.Find(5);
    Require(p.type == 2 && (n.type == 4 || n.type == 2) &&
        (uv.type == 1 || uv.type == 6 || uv.type == 7), "Unsupported position/normal/UV encoding");
    const auto start = b.bytes.data + size_t(index) * b.stride;
    Vertex v;
    Reader pr(start + p.offset, 12);
    for (auto& x : v.position) x = pr.Float();
    if (n.type == 2)
    {
        Reader nr(start + n.offset, 12);
        for (auto& x : v.normal) x = nr.Float();
    }
    else
        for (int j = 0; j < 3; ++j) v.normal[j] = start[n.offset + 2-j] * (2.f/255.f) - 1.f;
    Reader tr(start + uv.offset, uv.type == 1 ? 8 : 4);
    if (uv.type == 1)
        for (auto& x : v.uv) x = tr.Float();
    else
    {
        // Trees use their own 16-tile scale. Static base UVs also carry a
        // fractional correction in the alpha bytes of tangent and binormal.
        const auto t = b.Find(6), bin = b.Find(7);
        Require(t.type == 4 && bin.type == 4, "Unsupported packed UV correction");
        const float scale = tree ? 1.f / 2048.f : 1.f / 1024.f;
        v.uv[0] = (float(tr.Get<int16_t>()) + (tree ? 0.f : start[t.offset+3] / 255.f)) * scale;
        v.uv[1] = (float(tr.Get<int16_t>()) + (tree ? 0.f : start[bin.offset+3] / 255.f)) * scale;
    }
    Normalize(v.normal);
    return v;
}
uint64_t FaceCount(const Level& l)
{
    uint64_t n = 0;
    for (const auto& m : l.meshes) n += m.triangles.size();
    return n;
}
}

Level Read(const std::string& directory)
{
    Level l;
    const auto dir = fs::canonical(fs::u8path(directory));
    l.source = dir.u8string();
    const auto levelBytes = Load(dir / "level");
    const auto chunks = Split(Reader(levelBytes));
    auto header = Chunk(chunks, 1);
    l.version = header.Get<uint16_t>();
    header.Get<uint16_t>(); header.End();
    Require(l.version == 13 || l.version == 14, "Unsupported level version " + std::to_string(l.version));
    auto shaders = Chunk(chunks, 2);
    const auto shaderCount = shaders.U32();
    Require(shaderCount <= shaders.Remaining(), "Invalid shader count");
    for (uint32_t i = 0; i < shaderCount; ++i) l.shaders.push_back(shaders.String());
    shaders.End();

    const auto geomBytes = Load(dir / "level.geom");
    uint64_t fingerprint = 14695981039346656037ull;
    for (const auto* bytes : {&levelBytes, &geomBytes})
        for (auto b : *bytes) { fingerprint ^= b; fingerprint *= 1099511628211ull; }
    std::ostringstream library;
    library << "recovered_trees_v02_" << std::hex << std::setw(16) << std::setfill('0') << fingerprint;
    l.libraryName = library.str();
    const auto geom = Split(Reader(geomBytes));
    auto geomHeader = Chunk(geom, 1);
    Require(geomHeader.Get<uint16_t>() == l.version, "level/level.geom version mismatch");
    geomHeader.Get<uint16_t>(); geomHeader.End();
    const auto vb = VertexBuffers(Chunk(geom, 9));
    const auto ib = IndexBuffers(Chunk(geom, 10));
    std::vector<std::vector<Window>> swis;
    if (geom.count(11))
    {
        auto r = Chunk(geom, 11);
        const auto n = r.U32();
        Require(n <= r.Remaining() / 20, "Invalid SWI container count");
        for (uint32_t i = 0; i < n; ++i)
        {
            r.Take(16);
            const auto count = r.U32();
            Require(count && count <= r.Remaining() / 8, "Invalid SWI window count");
            std::vector<Window> windows;
            for (uint32_t j = 0; j < count; ++j) windows.push_back(r.Get<Window>());
            swis.push_back(std::move(windows));
        }
        r.End();
    }
    const auto visuals = Split(Chunk(chunks, 3));
    using PrototypeKey = std::tuple<std::array<uint32_t,6>,uint32_t,uint32_t,uint32_t,uint32_t>;
    std::map<PrototypeKey, uint32_t> prototypeIds;
    l.visualCount = uint32_t(visuals.size());
    for (const auto& visual : visuals)
    {
        try
        {
            const auto c = Split(visual.second);
            auto h = Chunk(c, 1);
            Require(h.size == 44, "Unsupported OGF header size");
            Require(h.Get<uint8_t>() == 4, "Unsupported OGF version");
            Mesh mesh;
            mesh.visual = visual.first;
            mesh.type = h.Get<uint8_t>(); mesh.shader = h.Get<uint16_t>();
            if (mesh.type == 1) { ++l.hierarchyCount; continue; }
            // LOD proxies duplicate the underlying full-detail geometry.
            if (mesh.type == 6)
            {
                ++l.lodCount;
                if (c.count(10))
                {
                    auto children=Chunk(c,10);
                    const auto n=children.U32();
                    Require(n<=children.Remaining()/4,"Invalid LOD child count");
                    TreeGroup group; group.visual=mesh.visual;
                    for (uint32_t j=0; j<n; ++j) group.children.push_back(children.U32());
                    children.End(); l.treeGroups.push_back(std::move(group));
                }
                continue;
            }
            Require(mesh.type == 0 || mesh.type == 2 || mesh.type == 7 || mesh.type == 11,
                "Unsupported visual type " + std::to_string(mesh.type));
            Require(mesh.shader < l.shaders.size(), "Shader index out of bounds");
            auto g = Chunk(c, 21);
            const auto vId = g.U32(), vBase = g.U32(), vCount = g.U32();
            const auto iId = g.U32(), iBase = g.U32(), iCount = g.U32();
            mesh.geometry = {{vId,vBase,vCount,iId,iBase,iCount}};
            g.End();
            Require(vId < vb.size() && iId < ib.size(), "Invalid geometry container ID");
            Require(vBase <= vb[vId].count && vCount <= vb[vId].count-vBase, "Visual VB range out of bounds");
            const size_t available = ib[iId].size / 2;
            Require(iBase <= available && iCount <= available-iBase, "Visual IB range out of bounds");
            uint32_t first = 0, count = iCount, vertexCount = vCount;
            if (mesh.type == 2 || mesh.type == 11)
            {
                std::vector<Window> windows;
                if (mesh.type == 2) windows = Windows(Chunk(c, 6));
                else
                {
                    auto s = Chunk(c, 20);
                    const auto id = s.U32(); s.End();
                    Require(id < swis.size(), "Invalid tree SWI index");
                    windows = swis[id];
                }
                // The renderer uses window zero for LOD=1; never concatenate LODs.
                first = windows[0].offset; count = uint32_t(windows[0].triangles) * 3;
                vertexCount = windows[0].vertices;
                Require(first <= iCount && count <= iCount-first && vertexCount <= vCount,
                    "Progressive window outside visual");
            }
            Require(count % 3 == 0 && vertexCount > 0, "Invalid triangle list");
            mesh.selectedIndexOffset = first;
            const bool tree = mesh.type == 7 || mesh.type == 11;
            std::array<float, 16> matrix{};
            if (tree)
            {
                auto t = Chunk(c, 12);
                Require(t.size == 104, "Unsupported tree transform record");
                for (auto& x : matrix) x = t.Float();
                mesh.transform = matrix;
                Require(matrix[3] == 0 && matrix[7] == 0 && matrix[11] == 0 && matrix[15] == 1,
                    "Non-affine tree transform");
                for (auto& x : mesh.treeLighting) x = t.Float();
                t.End();
            }
            const PrototypeKey key(mesh.geometry, first, vertexCount, count, mesh.shader);
            const auto existing = prototypeIds.find(key);
            const bool newPrototype = tree && existing == prototypeIds.end();
            TreePrototype prototype;
            prototype.shader = mesh.shader;
            if (newPrototype) prototype.vertices.reserve(vertexCount);
            mesh.vertices.reserve(vertexCount);
            for (uint32_t j = 0; j < vertexCount; ++j)
            {
                auto v = Decode(vb[vId], vBase+j, tree);
                if (newPrototype) prototype.vertices.push_back(v);
                if (tree)
                {
                    v.position = Transform(v.position, matrix, false);
                    // Match the engine's tree vertex shader normal transform.
                    v.normal = Transform(v.normal, matrix, true);
                    Normalize(v.normal);
                }
                mesh.vertices.push_back(v);
            }
            Reader indices(ib[iId].data + size_t(iBase+first)*2, size_t(count)*2);
            mesh.triangles.reserve(count / 3);
            for (uint32_t j = 0; j < count / 3; ++j)
            {
                Triangle t;
                for (auto& k : t.vertices)
                {
                    k = indices.Get<uint16_t>();
                    Require(k < vertexCount, "Triangle vertex outside visual");
                }
                mesh.triangles.push_back(t);
            }
            if (tree)
            {
                if (newPrototype)
                {
                    mesh.treePrototype = uint32_t(l.treePrototypes.size());
                    prototype.triangles = mesh.triangles;
                    l.treePrototypes.push_back(std::move(prototype));
                    prototypeIds.emplace(key, mesh.treePrototype);
                }
                else mesh.treePrototype = existing->second;
            }
            l.meshes.push_back(std::move(mesh));
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error("Visual " + std::to_string(visual.first) + ": " + e.what());
        }
    }
    for (const auto& group : l.treeGroups)
        for (auto child : group.children)
            Require(visuals.count(child)!=0,"LOD visual references a missing child");
    l.collisionBytes = Load(dir / "level.cform");
    Reader cr(l.collisionBytes);
    Require(cr.U32() == 4, "Unsupported CFORM version (expected 4)");
    const auto vertices = cr.U32(), faces = cr.U32();
    for (int j = 0; j < 6; ++j) cr.Float();
    Require(uint64_t(vertices)*12 + uint64_t(faces)*16 == cr.Remaining(), "CFORM size mismatch");
    l.collisionVertices.reserve(vertices);
    for (uint32_t j = 0; j < vertices; ++j)
    {
        Vec3 v;
        for (auto& x : v) x = cr.Float();
        l.collisionVertices.push_back(v);
    }
    l.collisionFaces.reserve(faces);
    for (uint32_t j = 0; j < faces; ++j)
    {
        CollisionFace f;
        for (auto& k : f.vertices)
        {
            k = cr.U32(); Require(k < vertices, "CFORM vertex index out of bounds");
        }
        f.attributes = cr.U32(); l.collisionFaces.push_back(f);
    }
    cr.End();
    l.warnings.push_back("Connected surfaces are geometric candidates, not recovered author objects.");
    l.warnings.push_back("OBJ has no X-Ray physics, wind, sectors, portals, details, lightmaps or spawn semantics. Do not recompile it as a finished level.");
    l.warnings.push_back("CFORM material numbers require the source gamemtl.xr; they are not mapped to the active SDK material library.");
    l.warnings.push_back("Custom shader UV/normal decoding must be verified against its shader code. No remeshing or automatic collider generation is performed.");
    return l;
}

void GroupConnectedSurfaces(Level& l)
{
    struct Point
    {
        std::array<uint32_t, 3> bits;
        bool operator==(const Point& b) const { return bits == b.bits; }
    };
    struct Hash
    {
        size_t operator()(const Point& p) const
        {
            size_t h = 0;
            for (auto x : p.bits) h ^= size_t(x) + 0x9e3779b9u + (h<<6) + (h>>2);
            return h;
        }
    };
    struct Edge
    {
        uint32_t domain, a, b, face;
        bool operator<(const Edge& e) const { return std::tie(domain,a,b,face) < std::tie(e.domain,e.a,e.b,e.face); }
        bool Same(const Edge& e) const { return domain == e.domain && a == e.a && b == e.b; }
    };
    const auto faceCount = FaceCount(l);
    Require(faceCount < UINT32_MAX, "Too many faces for grouping");
    std::vector<uint32_t> parent(size_t(faceCount), 0), rank(size_t(faceCount), 0);
    std::iota(parent.begin(), parent.end(), 0u);
    auto root = [&](uint32_t a)
    {
        while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
        return a;
    };
    std::unordered_map<Point, uint32_t, Hash> points;
    std::unordered_map<Point, uint32_t, Hash> uniqueFaces;
    std::vector<Edge> edges;
    edges.reserve(size_t(faceCount)*3);
    uint32_t faceId = 0;
    l.boundaryEdges = l.ambiguousEdges = l.degenerateFaces = l.coincidentFaceCopies = 0;
    for (const auto& mesh : l.meshes)
    {
        const uint32_t firstFace = faceId;
        // A source tree visual is already an instance, independent of card
        // connectivity. Do not let coincident foliage affect static edge analysis.
        if (mesh.type == 7 || mesh.type == 11)
        {
            for (size_t i=0; i<mesh.triangles.size(); ++i) parent[faceId++]=firstFace;
            continue;
        }
        std::vector<uint32_t> ids;
        ids.reserve(mesh.vertices.size());
        for (const auto& v : mesh.vertices)
        {
            Point p;
            for (int j = 0; j < 3; ++j)
            {
                const float x = v.position[j] == 0 ? 0.f : v.position[j]; // normalize -0
                std::memcpy(&p.bits[j], &x, 4);
            }
            Require(points.size() < UINT32_MAX, "Too many position keys");
            const auto i = points.emplace(p, uint32_t(points.size()));
            ids.push_back(i.first->second);
        }
        // Instances must never join each other or the terrain on coincident edges.
        const uint32_t domain = mesh.type == 7 || mesh.type == 11 ? mesh.visual+1 : 0;
        for (const auto& t : mesh.triangles)
        {
            const auto a = ids[t.vertices[0]], b = ids[t.vertices[1]], c = ids[t.vertices[2]];
            const auto& p = mesh.vertices[t.vertices[0]].position;
            const auto& q = mesh.vertices[t.vertices[1]].position;
            const auto& r = mesh.vertices[t.vertices[2]].position;
            double u[3], v[3];
            for (int j=0; j<3; ++j) { u[j]=double(q[j])-p[j]; v[j]=double(r[j])-p[j]; }
            const double area2 = std::pow(u[1]*v[2]-u[2]*v[1],2) + std::pow(u[2]*v[0]-u[0]*v[2],2) + std::pow(u[0]*v[1]-u[1]*v[0],2);
            if (a == b || b == c || c == a || area2 == 0) ++l.degenerateFaces;
            else
            {
                // Collapse coincident geometric triangles ONLY in the adjacency
                // graph. Keep every source face, its winding, UVs and vertices.
                Point key{{a,b,c}};
                std::sort(key.bits.begin(),key.bits.end());
                const auto original=uniqueFaces.emplace(key,faceId);
                if (!original.second)
                {
                    parent[faceId++]=original.first->second;
                    ++l.coincidentFaceCopies;
                    continue;
                }
                for (int j = 0; j < 3; ++j)
                {
                    const auto x = ids[t.vertices[j]], y = ids[t.vertices[(j+1)%3]];
                    edges.push_back({domain, (std::min)(x,y), (std::max)(x,y), faceId});
                }
            }
            ++faceId;
        }
        // A tree visual carries an explicit instance transform. Its disconnected
        // leaf cards belong to this instance and must not become separate objects.
        if (domain && faceId > firstFace)
            for (uint32_t i = firstFace+1; i < faceId; ++i) parent[i] = firstFace;
    }
    std::sort(edges.begin(), edges.end());
    for (size_t i = 0; i < edges.size();)
    {
        size_t end = i+1;
        while (end < edges.size() && edges[i].Same(edges[end])) ++end;
        if (end-i == 1) ++l.boundaryEdges;
        else if (end-i == 2)
        {
            auto a = root(edges[i].face), b = root(edges[i+1].face);
            if (a != b)
            {
                if (rank[a] < rank[b]) std::swap(a,b);
                parent[b] = a;
                if (rank[a] == rank[b]) ++rank[a];
            }
        }
        else ++l.ambiguousEdges; // nonmanifold seam: require human review
        i = end;
    }
    std::unordered_map<uint32_t, uint32_t> names;
    faceId = 0;
    for (auto& mesh : l.meshes)
        for (auto& t : mesh.triangles)
        {
            const auto i = names.emplace(root(faceId++), uint32_t(names.size()));
            t.component = i.first->second;
        }
    l.componentCount = uint32_t(names.size());
}

std::string Describe(const Level& l)
{
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << "X-Ray Level Recovery 0.3 (experimental geometry recovery)\n"
      << "Source: " << l.source << "\nLevel version: " << l.version
      << "\nVisual records: " << l.visualCount << "\nFull-detail meshes: " << l.meshes.size()
      << "\nHierarchy records: " << l.hierarchyCount << "\nLOD proxies excluded: " << l.lodCount
      << "\nRender triangles: " << FaceCount(l) << "\nConnected surface candidates: " << l.componentCount
      << "\nBoundary edges: " << l.boundaryEdges << "\nNonmanifold edges (not joined): " << l.ambiguousEdges
      << "\nCoincident static face copies (preserved): " << l.coincidentFaceCopies
      << "\nDegenerate static faces (preserved): " << l.degenerateFaces
      << "\nCollision vertices: " << l.collisionVertices.size()
      << "\nCollision triangles: " << l.collisionFaces.size()
      << "\nRecovered tree prototypes (geometry + material): " << l.treePrototypes.size() << "\n";
    if (!l.treePrototypes.empty())
    {
        const auto plan=BuildTreeLibrary(l);
        s << "Native tree models: " << plan.models.size() << "\nTree object instances: " << plan.instances.size() << "\n";
    }
    for (const auto& w : l.warnings) s << "\nNOTE: " << w;
    return s.str() + "\n";
}

namespace
{
std::ofstream Output(const fs::path& p, bool binary = false)
{
    std::ofstream f;
    f.exceptions(std::ios::failbit | std::ios::badbit);
    f.open(p, binary ? std::ios::out | std::ios::binary : std::ios::out);
    f.imbue(std::locale::classic());
    f << std::setprecision(std::numeric_limits<float>::max_digits10);
    return f;
}
bool Inside(fs::path child, const fs::path& parent)
{
    for (; !child.empty(); child = child.parent_path())
    {
        if (fs::exists(child) && fs::equivalent(child, parent)) return true;
        if (child == child.parent_path()) break;
    }
    return false;
}
std::string Csv(const std::string& s)
{
    std::string out = "\"";
    for (char c : s) { if (c == '"') out += '"'; out += c; }
    return out + '"';
}
void ExportFiles(const Level& l, const fs::path& out)
{
    { auto report = Output(out / "report.txt"); report << Describe(l); report.close(); }
    { auto f = Output(out / "original.cform", true); f.write(reinterpret_cast<const char*>(l.collisionBytes.data()), l.collisionBytes.size()); f.close(); }
    {
        auto f = Output(out / "materials.csv");
        f << "shader_id,original_shader_and_texture_list\n";
        for (size_t i=0; i<l.shaders.size(); ++i) f << i << ',' << Csv(l.shaders[i]) << '\n';
        f.close();
    }
    {
        auto f = Output(out / "visuals.csv");
        f << "visual,type,shader_id,vb_id,vb_base,vb_count,ib_id,ib_base,ib_count,lod_index_offset,recovered_vertices,recovered_triangles";
        for (int i=0; i<16; ++i) f << ",matrix_" << i;
        f << '\n';
        for (const auto& m : l.meshes)
        {
            f << m.visual << ',' << m.type << ',' << m.shader;
            for (auto x : m.geometry) f << ',' << x;
            f << ',' << m.selectedIndexOffset << ',' << m.vertices.size() << ',' << m.triangles.size();
            for (auto x : m.transform) f << ',' << x;
            f << '\n';
        }
        f.close();
    }
    {
        auto f = Output(out / "render.mtl");
        for (size_t i=0; i<l.shaders.size(); ++i)
        {
            f << "newmtl shader_" << i << "\nKd 0.8 0.8 0.8\n";
            // Preserve the source list in CSV; do not invent texture paths or alpha flags.
        }
        f.close();
    }
    {
        auto f = Output(out / "render.obj");
        auto provenance = Output(out / "render_faces.csv");
        provenance << "obj_face,component,visual,visual_type,source_face,shader_id\n";
        f << "# X-Ray axes and winding retained; UV V is flipped for OBJ. Units: metres.\nmtllib render.mtl\n";
        std::vector<uint64_t> bases;
        uint64_t base = 1;
        for (const auto& m : l.meshes)
        {
            bases.push_back(base); base += m.vertices.size();
            for (const auto& v : m.vertices) f << "v " << v.position[0] << ' ' << v.position[1] << ' ' << v.position[2] << '\n';
            for (const auto& v : m.vertices)
                f << "vt " << v.uv[0] << ' ' << std::setprecision(17) << 1.0-double(v.uv[1])
                  << std::setprecision(std::numeric_limits<float>::max_digits10) << '\n';
            for (const auto& v : m.vertices) f << "vn " << v.normal[0] << ' ' << v.normal[1] << ' ' << v.normal[2] << '\n';
        }
        struct Ref { uint32_t component, mesh, face; };
        std::vector<Ref> refs;
        refs.reserve(size_t(FaceCount(l)));
        for (uint32_t i=0; i<l.meshes.size(); ++i)
            for (uint32_t j=0; j<l.meshes[i].triangles.size(); ++j)
                refs.push_back({l.meshes[i].triangles[j].component,i,j});
        std::stable_sort(refs.begin(), refs.end(), [](const Ref& a,const Ref& b) { return a.component < b.component; });
        uint32_t group = UINT32_MAX, shader = UINT32_MAX;
        uint64_t face = 0;
        for (const auto& r : refs)
        {
            const auto& m = l.meshes[r.mesh];
            if (group != r.component) { group = r.component; f << "o surface_candidate_" << group << '\n'; }
            if (shader != m.shader) { shader = m.shader; f << "usemtl shader_" << shader << '\n'; }
            f << 'f';
            for (const auto index : m.triangles[r.face].vertices)
            {
                const auto n = bases[r.mesh]+index;
                f << ' ' << n << '/' << n << '/' << n;
            }
            f << '\n';
            provenance << ++face << ',' << r.component << ',' << m.visual << ',' << m.type << ',' << r.face << ',' << m.shader << '\n';
        }
        f.close(); provenance.close();
    }
    {
        auto f = Output(out / "collision.obj");
        auto attributes = Output(out / "collision_faces.csv");
        attributes << "obj_face,raw_attributes,material_index,suppress_shadows,suppress_wallmarks,sector\n";
        f << "# Inspection only: exact physical attributes are in original.cform and collision_faces.csv\n";
        for (const auto& v : l.collisionVertices) f << "v " << v[0] << ' ' << v[1] << ' ' << v[2] << '\n';
        uint64_t face = 0;
        for (const auto& t : l.collisionFaces)
        {
            f << "f " << uint64_t(t.vertices[0])+1 << ' ' << uint64_t(t.vertices[1])+1 << ' ' << uint64_t(t.vertices[2])+1 << '\n';
            const auto a = t.attributes;
            attributes << ++face << ',' << a << ',' << (a&0x3fff) << ',' << ((a>>14)&1) << ',' << ((a>>15)&1) << ',' << (a>>16) << '\n';
        }
        f.close(); attributes.close();
    }
}
}
void Export(const Level& l, const std::string& newDirectory)
{
    Require(l.componentCount > 0, "Run connected-surface analysis before export");
    Require(!newDirectory.empty(), "Empty export path");
    const auto out = fs::weakly_canonical(fs::absolute(fs::u8path(newDirectory)));
    const auto source = fs::canonical(fs::u8path(l.source));
    Require(!Inside(out, source), "Export directory must be outside the source level");
    Require(!fs::exists(out), "Export directory already exists; choose a new name");
    Require(fs::is_directory(out.parent_path()), "Export parent directory does not exist");
    // Own only a directory this invocation successfully created.
    auto staging = out;
    staging += ".partial";
    Require(fs::create_directory(staging), "Export staging directory already exists");
    try
    {
        ExportFiles(l, staging);
        Require(!fs::exists(out), "Export destination appeared during export");
        fs::rename(staging, out);
    }
    catch (...)
    {
        // No recursive deletion: retain partial output for diagnosis.
        throw;
    }
}
}
