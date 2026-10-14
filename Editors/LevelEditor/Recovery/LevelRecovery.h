#pragma once

// Independent of the renderer, editor globals and the X-Ray allocator.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace LevelRecovery
{
struct Vertex
{
    std::array<float, 3> position{}, normal{};
    std::array<float, 2> uv{};
};
struct Triangle
{
    std::array<uint32_t, 3> vertices{};
    uint32_t component = 0;
};
struct Mesh
{
    uint32_t visual = 0, type = 0, shader = 0;
    // Original GCONTAINER record and selected window: preserve instancing evidence.
    std::array<uint32_t, 6> geometry{};
    uint32_t selectedIndexOffset = 0;
    uint32_t treePrototype = UINT32_MAX;
    std::array<float, 10> treeLighting{};
    std::array<float, 16> transform{{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
    std::vector<Vertex> vertices;
    std::vector<Triangle> triangles;
};
struct TreePrototype
{
    uint32_t shader = 0;
    std::vector<Vertex> vertices; // original LOCAL positions, never inverse-transformed
    std::vector<Triangle> triangles;
};
struct TreeGroup
{
    uint32_t visual = 0;
    std::vector<uint32_t> children; // explicit OGF_CHILDREN_L of a LOD visual
};
struct TreeModel { std::vector<uint32_t> parts; };
struct TreeObjectInstance
{
    uint32_t visual = 0, model = 0;
    std::vector<uint32_t> meshes;
};
struct TreeLibrary
{
    std::vector<TreeModel> models;
    std::vector<TreeObjectInstance> instances;
};
struct Placement
{
    std::array<float, 3> position{}, rotation{}, scale{};
    double maxPositionError = 0;
};
struct CollisionFace
{
    std::array<uint32_t, 3> vertices{};
    uint32_t attributes = 0; // original material:14, shadow:1, wallmark:1, sector:16
};
struct Level
{
    std::string source;
    std::string libraryName; // content-derived namespace for native object references
    uint16_t version = 0;
    uint32_t visualCount = 0, hierarchyCount = 0, lodCount = 0;
    uint32_t componentCount = 0;
    uint64_t boundaryEdges = 0, ambiguousEdges = 0, degenerateFaces = 0;
    uint64_t coincidentFaceCopies = 0;
    std::vector<std::string> shaders, warnings;
    std::vector<Mesh> meshes;
    std::vector<TreePrototype> treePrototypes;
    std::vector<TreeGroup> treeGroups;
    std::vector<std::array<float, 3>> collisionVertices;
    std::vector<CollisionFace> collisionFaces;
    // Exact original bytes: never regenerate physical geometry from render meshes.
    std::vector<uint8_t> collisionBytes;
};

// UTF-8 paths. Unsupported layouts and malformed input throw std::runtime_error.
// No input files are written. Grouping changes labels only, never mesh topology.
Level Read(const std::string& directory);
void GroupConnectedSurfaces(Level& level);
std::string Describe(const Level& level);
// Creates a NEW directory atomically. Refuses existing destinations and any path
// inside the source level. OBJ is an intermediate inspection format, not .level.
void Export(const Level& level, const std::string& newDirectory);
// Recover repeated tree models as native .object files plus a .selection and
// original instance metadata. No render geometry is converted to collision.
void ExportTreeLibrary(const Level& level, const std::string& newDirectory);
TreeLibrary BuildTreeLibrary(const Level& level);
// Installs only recovered objects under a unique namespace in the configured SDK
// object library. Existing byte-identical files can be reused; different files fail.
void InstallTreeLibrary(const Level& level, const std::string& objectsDirectory);
// Checked X-Ray SRT decomposition. Rejects shear/singular transforms and validates
// the reconstructed positions of EVERY prototype vertex against the decoded mesh.
bool TreePlacement(const Level& level, const Mesh& mesh, Placement& result);

struct StaticFaceRef { uint32_t mesh = 0, face = 0, rotation = 0; };
struct StaticModel
{
    std::vector<TreePrototype> parts; // generic native mesh parts, one per shader
    std::vector<std::array<uint32_t,2>> canonicalFaces; // part, local face
};
struct StaticInstance
{
    uint32_t component = 0, model = 0;
    std::array<float,3> origin{};
    std::vector<StaticFaceRef> faces; // canonical order; every source face once
};
struct StaticLibrary
{
    std::vector<StaticModel> models;
    std::vector<StaticInstance> instances;
};
StaticLibrary BuildStaticLibrary(const Level& level);
// Full render recovery: static candidates + whole vegetation, with separate raw
// collision snapshot and face provenance. No authored object boundaries guessed.
void ExportSceneLibrary(const Level& level, const StaticLibrary& plan, const std::string& newDirectory);
void InstallStaticLibrary(const Level& level, const StaticLibrary& plan, const std::string& objectsDirectory);
std::string StaticLibraryName(const Level& level);
}
