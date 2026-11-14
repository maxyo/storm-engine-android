// Far LODs of a .gm model (README.md): every object split into spatial clusters, every cluster simplified into a chain
// of levels by meshoptimizer. Only triangle indices are made: every level draws the object's own vertices, so vertex
// buffers, baked colours (.col), lightmap uvs, bone data and collision are untouched.
//
// No engine dependencies: tests/far_lod builds it alone.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace storm::geometry::far_lod
{

// Bump when Build changes its result or Encode its format (the cache keeps them).
inline constexpr uint32_t kVersion = 2;

// Level 0 is the object's own triangles; levels 1.. are simplified within kLevelError metres (model units) each.
inline constexpr int kLevels = 6;
inline constexpr std::array<float, kLevels> kLevelError = {0.f, 0.05f, 0.2f, 0.8f, 3.2f, 12.8f};
// Triangles per cluster (about): a cluster is the unit of level choice and of depth-range culling.
inline constexpr uint32_t kClusterTriangles = 1536;

// One object of the model as the file lays it out: `triangle_count` triangles from `first_triangle` of the model's
// triangle array, their 16-bit indices relative to `first_vertex` of vertex buffer `vertex_buffer`.
struct MeshObject
{
    uint32_t vertex_buffer = 0;
    uint32_t first_vertex = 0, vertex_count = 0;
    uint32_t first_triangle = 0, triangle_count = 0;
};

struct MeshBuffer
{
    const uint8_t *vertices = nullptr; // position (3 floats) at 0, normal (3 floats) at 12, uv0 (2 floats) at 28
    uint32_t stride = 0;
    uint32_t count = 0;
};

struct Cluster
{
    std::array<float, 3> center{}; // bounding sphere of the cluster's triangles, model space
    float radius = 0;
    // Level l: `count[l]` triangles from triangle `first[l]` of Lods::indices (3 indices each, relative to the
    // object's first vertex); `error[l]` metres. A level that would keep almost all of the previous one's triangles is
    // the previous one (same range), so the chain may repeat.
    std::array<uint32_t, kLevels> first{}, count{};
    std::array<float, kLevels> error{};
};

struct ObjectLods
{
    uint32_t first_cluster = 0, cluster_count = 0;
};

struct Lods
{
    std::vector<ObjectLods> objects; // one per MeshObject (objects without triangles: no clusters)
    std::vector<Cluster> clusters;
    std::vector<uint16_t> indices; // every level of every cluster
};

// Whether a model gets far LODs: islands and ships, and locations of at least kLocationRadius metres (outdoor
// places: towns, forts, shores, jungles; interiors are close everywhere); never a model with animated (skinned)
// vertices. `model`: its path under RESOURCE/MODELS ('\\' or '/', any case).
inline constexpr float kLocationRadius = 50.f;
bool Wanted(std::string_view model, float radius, bool animated);

// Why the layout cannot be given LODs, or nothing when it can: every object's triangle and vertex ranges inside the
// model, every index inside the object's vertex range, no two objects sharing triangles or (in one buffer) vertices.
std::optional<std::string> CheckMeshLayout(std::span<const MeshObject> objects,
                                           std::span<const uint32_t> buffer_vertex_counts,
                                           std::span<const uint16_t> triangles);

// Pairs (earlier, later) of the object's triangles, as numbered inside the object, that can tie in the depth test: in
// one plane (facing either way, within kTieNormal / kTieDistance) and overlapping, not just touching. The game draws
// with a less-or-equal depth test, so of two such triangles the later one wins where their depths are equal
// (duplicated faces, decals a few millimetres off their surface).
inline constexpr double kTieNormal = 2e-3;   // per component of the unit normals
inline constexpr double kTieDistance = 0.01; // model units (metres)
std::vector<std::pair<uint32_t, uint32_t>> DepthTiePairs(const MeshObject &object, const MeshBuffer &buffer,
                                                         std::span<const uint16_t> triangles);

// DepthTiePairs over the whole model: also a decal in an object of its own over another object's surface. Pairs of
// (object, triangle as numbered inside the object), the earlier first.
struct TriangleRef
{
    uint32_t object = 0, triangle = 0;
};
std::vector<std::pair<TriangleRef, TriangleRef>> ModelDepthTies(std::span<const MeshObject> objects,
                                                                std::span<const MeshBuffer> buffers,
                                                                std::span<const uint16_t> triangles);

// The clusters and levels of every object. The layout must pass CheckMeshLayout.
// - Clusters: the object's triangles in meshoptimizer's spatial order, cut every kClusterTriangles; triangles that tie
//   in depth inside the object go to one cluster.
// - Level l > 0: meshopt_simplifyWithAttributes (normal, uv0) of the cluster within kLevelError[l], with Permissive
//   then the parts smaller than the level's error that touch no seam dropped. Vertices whose position is also used by another cluster or another object of the model are locked, so
//   neighbours at different levels meet without cracks. Depth-tie triangles (ModelDepthTies) are never simplified; those
//   tying inside their object stay last in every level, in the file's order (objects keep their draw order). Each level is ordered for the post-transform vertex cache.
// `parallel(count, work)` runs work(first, last) over [0, count) in pieces, on any threads (the engine passes the
// background bakes' ParallelRanges: their threads, thermal pause and cancellation); empty: on this thread.
using Parallel = std::function<void(size_t, const std::function<void(size_t, size_t)> &)>;
Lods Build(std::span<const MeshObject> objects, std::span<const MeshBuffer> buffers,
           std::span<const uint16_t> triangles, const Parallel &parallel = {});

// The cache's form (meshopt_encodeIndexBuffer for the indices: it may rotate a triangle's corners, the winding and the
// triangle order stay) and back. Decode returns nothing for data that is not
// the LODs of `objects` (a damaged or foreign entry): every range and index is checked.
std::vector<uint8_t> Encode(const Lods &lods);
std::optional<Lods> Decode(std::span<const uint8_t> data, std::span<const MeshObject> objects);

} // namespace storm::geometry::far_lod
