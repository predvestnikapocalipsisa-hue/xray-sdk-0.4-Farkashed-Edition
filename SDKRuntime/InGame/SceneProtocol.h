#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

// Versioned wire types only. Neither engine's Fvector, CDB::TRI, allocator,
// class pointers nor STL containers are shared across the process boundary.
namespace SDKInGame
{
    constexpr uint32_t sceneVersion = 4;
    constexpr uint64_t maxSceneBytes = 2ull * 1024 * 1024 * 1024;
    constexpr uint64_t maxSpawnPacketBytes = 16 * 1024 - 1;
#pragma pack(push, 4)
    struct Vertex { float x, y, z; };
    struct Triangle { uint32_t vertex[3]; uint32_t material; };
    struct Mesh { uint64_t offset, bytes; };
    struct Instance { float transform[16]; uint32_t mesh, reserved; };
    struct Spawn { uint64_t offset, bytes; uint32_t format, reserved; Vertex position, angles; char section[128], name[256]; };
    struct NavigationNode { Vertex position, normal; uint32_t links[4]; };
    struct Header
    {
        char magic[8];
        uint32_t version, headerBytes;
        uint64_t bytes;
        uint64_t vertices, vertexCount, triangles, triangleCount;
        uint64_t meshes, meshCount, instances, instanceCount;
        uint64_t details, detailBytes;
        uint64_t spawns, spawnCount, navigation, navigationCount;
        uint64_t patrols, patrolBytes;
        float navigationCell;
        Vertex camera, angles;
        char textureRoot[1024]; // UTF-8, optional SDK resource fallback
    };
#pragma pack(pop)
    static_assert(sizeof(Vertex) == 12 && sizeof(Triangle) == 16, "wire geometry layout");
    static_assert(sizeof(Mesh) == 16 && sizeof(Instance) == 72, "wire visual layout");

    inline bool Finite(Vertex v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

    class SceneView
    {
        const uint8_t* data = nullptr;
        Header header{};
    public:
        const Header& Info() const { return header; }
        Vertex GetVertex(size_t i) const { Vertex v; std::memcpy(&v, data + header.vertices + i * sizeof(v), sizeof(v)); return v; }
        Triangle GetTriangle(size_t i) const { Triangle t; std::memcpy(&t, data + header.triangles + i * sizeof(t), sizeof(t)); return t; }
        Mesh GetMesh(size_t i) const { Mesh m; std::memcpy(&m, data + header.meshes + i * sizeof(m), sizeof(m)); return m; }
        Instance GetInstance(size_t i) const { Instance v; std::memcpy(&v, data + header.instances + i * sizeof(v), sizeof(v)); return v; }
        const uint8_t* MeshData(size_t i) const { return data + GetMesh(i).offset; }
        const uint8_t* DetailData() const { return header.detailBytes ? data + header.details : nullptr; }
        const uint8_t* PatrolData() const { return header.patrolBytes ? data + header.patrols : nullptr; }
        Spawn GetSpawn(size_t i) const { Spawn s; std::memcpy(&s, data + header.spawns + i * sizeof(s), sizeof(s)); return s; }
        const uint8_t* SpawnData(size_t i) const { return data + GetSpawn(i).offset; }
        NavigationNode GetNavigationNode(size_t i) const { NavigationNode n; std::memcpy(&n, data + header.navigation + i * sizeof(n), sizeof(n)); return n; }

        bool Open(const void* bytes, size_t size, const char*& error)
        {
            data = nullptr;
            error = nullptr;
            if (!bytes || size < sizeof(Header)) { error = "Truncated scene header"; return false; }
            std::memcpy(&header, bytes, sizeof(header));
            if (std::memcmp(header.magic, "XRSDKP01", 8) || header.version != sceneVersion || header.headerBytes != sizeof(Header))
                { error = "Unsupported scene protocol"; return false; }
            if (header.bytes != size || size > maxSceneBytes) { error = "Invalid scene size"; return false; }
            uint64_t cursor = sizeof(Header);
            const auto section = [&](uint64_t offset, uint64_t count, uint64_t stride)
            {
                if (offset != cursor || cursor > size || count > (size - cursor) / stride) return false;
                cursor += count * stride;
                return true;
            };
            if (!section(header.vertices, header.vertexCount, sizeof(Vertex)) ||
                !section(header.triangles, header.triangleCount, sizeof(Triangle)) ||
                !section(header.meshes, header.meshCount, sizeof(Mesh)) ||
                !section(header.instances, header.instanceCount, sizeof(Instance)) ||
                !section(header.spawns, header.spawnCount, sizeof(Spawn)) ||
                !section(header.navigation, header.navigationCount, sizeof(NavigationNode)))
                { error = "Invalid scene sections"; return false; }
            if (header.vertexCount < 4 || header.triangleCount < 2 || header.spawnCount > 60000 || header.navigationCount >= 0x00ffffff ||
                !Finite(header.camera) || !Finite(header.angles) || !std::memchr(header.textureRoot, 0, sizeof(header.textureRoot)))
                { error = "Invalid scene metadata"; return false; }
            data = static_cast<const uint8_t*>(bytes);
            for (size_t i = 0; i < header.vertexCount; ++i)
                if (!Finite(GetVertex(i))) { error = "Non-finite vertex"; data = nullptr; return false; }
            for (size_t i = 0; i < header.triangleCount; ++i)
            {
                const Triangle t = GetTriangle(i);
                if (t.vertex[0] >= header.vertexCount || t.vertex[1] >= header.vertexCount || t.vertex[2] >= header.vertexCount)
                    { error = "Triangle index outside vertex buffer"; data = nullptr; return false; }
                const Vertex a = GetVertex(t.vertex[0]), b = GetVertex(t.vertex[1]), c = GetVertex(t.vertex[2]);
                const double ux = double(b.x) - a.x, uy = double(b.y) - a.y, uz = double(b.z) - a.z;
                const double vx = double(c.x) - a.x, vy = double(c.y) - a.y, vz = double(c.z) - a.z;
                const double nx = uy*vz-uz*vy, ny = uz*vx-ux*vz, nz = ux*vy-uy*vx;
                if (nx*nx + ny*ny + nz*nz < 1.e-20)
                    { error = "Degenerate collision triangle"; data = nullptr; return false; }
            }
            for (size_t i = 0; i < header.meshCount; ++i)
            {
                const Mesh m = GetMesh(i);
                if (m.offset != cursor || !m.bytes || m.bytes > size - cursor)
                    { error = "Invalid mesh payload"; data = nullptr; return false; }
                cursor += m.bytes;
            }
            for (size_t i = 0; i < header.spawnCount; ++i)
            {
                const auto s = GetSpawn(i);
                if (s.offset != cursor || !s.bytes || s.bytes > maxSpawnPacketBytes || s.bytes > size - cursor || s.format > 1 || s.reserved ||
                    !Finite(s.position) || !Finite(s.angles) || !s.section[0] || !std::memchr(s.section, 0, sizeof(s.section)) ||
                    !std::memchr(s.name, 0, sizeof(s.name)))
                    { error = "Invalid spawn payload"; data = nullptr; return false; }
                cursor += s.bytes;
            }
            if (header.spawnCount > 60000 || (header.navigationCount && (!std::isfinite(header.navigationCell) || header.navigationCell < .1f || header.navigationCell > 10.f)))
                { error = "Invalid navigation metadata"; data = nullptr; return false; }
            for (size_t i = 0; i < header.navigationCount; ++i)
            {
                const auto n = GetNavigationNode(i);
                const double length=double(n.normal.x)*n.normal.x+double(n.normal.y)*n.normal.y+double(n.normal.z)*n.normal.z;
                if (!Finite(n.position) || !Finite(n.normal) || n.normal.y < .1f || length < .5 || length > 2.)
                    { error = "Invalid navigation node"; data = nullptr; return false; }
                for (auto link : n.links) if (link != UINT32_MAX && link >= header.navigationCount)
                    { error = "Invalid navigation link"; data = nullptr; return false; }
            }
            if (!section(header.details, header.detailBytes, 1) || header.patrolBytes > 64ull*1024*1024 ||
                !section(header.patrols, header.patrolBytes, 1) || cursor != size)
                { error = "Invalid detail payload"; data = nullptr; return false; }
            for (size_t i = 0; i < header.instanceCount; ++i)
            {
                const Instance instance = GetInstance(i);
                if (instance.mesh >= header.meshCount || instance.reserved)
                    { error = "Invalid scene instance"; data = nullptr; return false; }
                for (float value : instance.transform)
                    if (!std::isfinite(value)) { error = "Non-finite transform"; data = nullptr; return false; }
            }
            return true;
        }
    };

    class SceneBuilder
    {
        std::vector<std::vector<uint8_t>> visualMeshes;
        std::vector<std::vector<uint8_t>> spawnPackets;
    public:
        Header info{};
        std::vector<Vertex> vertices;
        std::vector<Triangle> triangles;
        std::vector<Instance> instances;
        std::vector<uint8_t> details;
        std::vector<uint8_t> patrols;
        std::vector<Spawn> spawns;
        std::vector<NavigationNode> navigation;
        void AddSpawn(Spawn spawn, const void* bytes, size_t size)
        {
            if (!bytes || !size || size > maxSpawnPacketBytes) throw std::invalid_argument("Invalid InGame spawn packet size");
            const auto* first = static_cast<const uint8_t*>(bytes);
            spawnPackets.emplace_back(first, first + size);
            spawn.bytes = size; spawns.push_back(spawn);
        }
        uint32_t AddMesh(const void* bytes, size_t size)
        {
            if (!bytes || !size) throw std::invalid_argument("Empty InGame visual mesh");
            const uint8_t* first = static_cast<const uint8_t*>(bytes);
            visualMeshes.emplace_back(first, first + size);
            return uint32_t(visualMeshes.size() - 1);
        }
        std::vector<uint8_t> Finish()
        {
            if (spawns.size()!=spawnPackets.size()) throw std::invalid_argument("InGame spawn records/payloads do not match");
            Header h = info;
            std::memcpy(h.magic, "XRSDKP01", 8);
            h.version = sceneVersion; h.headerBytes = sizeof(Header);
            h.vertices = sizeof(Header); h.vertexCount = vertices.size();
            h.triangles = h.vertices + vertices.size() * sizeof(Vertex); h.triangleCount = triangles.size();
            h.meshes = h.triangles + triangles.size() * sizeof(Triangle); h.meshCount = visualMeshes.size();
            h.instances = h.meshes + h.meshCount * sizeof(Mesh); h.instanceCount = instances.size();
            h.spawns = h.instances + instances.size() * sizeof(Instance); h.spawnCount = spawns.size();
            h.navigation = h.spawns + spawns.size() * sizeof(Spawn); h.navigationCount = navigation.size();
            uint64_t cursor = h.navigation + navigation.size() * sizeof(NavigationNode);
            std::vector<Mesh> meshes;
            for (const auto& mesh : visualMeshes) { meshes.push_back({cursor, mesh.size()}); cursor += mesh.size(); }
            auto records = spawns;
            for (auto& spawn : records) { spawn.offset = cursor; cursor += spawn.bytes; }
            h.details = cursor; h.detailBytes = details.size(); cursor += details.size();
            h.patrols = cursor; h.patrolBytes = patrols.size(); cursor += patrols.size();
            h.bytes = cursor;
            if (cursor > maxSceneBytes) throw std::length_error("InGame scene exceeds 2 GiB");
            std::vector<uint8_t> result(size_t(cursor), 0);
            std::memcpy(result.data(), &h, sizeof(h));
            const auto copy = [&](uint64_t offset, const void* first, size_t bytes) { if (bytes) std::memcpy(result.data() + offset, first, bytes); };
            copy(h.vertices, vertices.data(), vertices.size() * sizeof(Vertex));
            copy(h.triangles, triangles.data(), triangles.size() * sizeof(Triangle));
            copy(h.meshes, meshes.data(), meshes.size() * sizeof(Mesh));
            copy(h.instances, instances.data(), instances.size() * sizeof(Instance));
            copy(h.spawns, records.data(), records.size() * sizeof(Spawn));
            copy(h.navigation, navigation.data(), navigation.size() * sizeof(NavigationNode));
            for (size_t i = 0; i < meshes.size(); ++i) copy(meshes[i].offset, visualMeshes[i].data(), visualMeshes[i].size());
            for (size_t i = 0; i < records.size(); ++i) copy(records[i].offset, spawnPackets[i].data(), spawnPackets[i].size());
            copy(h.details, details.data(), details.size());
            copy(h.patrols, patrols.data(), patrols.size());
            return result;
        }
    };
}
