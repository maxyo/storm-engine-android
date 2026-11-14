// The far LOD checks' common part (tests/far_lod: synthetic models; tools/game_checks/far_lod: the game's models):
// Build one model on 4 threads and check every property of its levels, or throw.
//
// For every object and cluster: level 0 is exactly the cluster's triangles; every level uses only vertices of its
// object and of its cluster; triangles that tie in depth are in every level, last and in the file's order; every edge
// a cluster shares with another cluster or object at level 0 is still drawn at every level (no cracks); Encode and
// Decode give the LODs back (the codec may rotate a triangle's corners) and Decode refuses damaged data.
#pragma once

#include "far_lod.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>


namespace far_lod_check
{
namespace far_lod = storm::geometry::far_lod;
using far_lod::MeshBuffer;
using far_lod::MeshObject;

struct Model
{
    std::vector<MeshObject> objects;
    std::vector<std::vector<uint8_t>> buffers;
    std::vector<uint32_t> strides;
    std::vector<uint16_t> triangles;

    std::vector<uint32_t> counts() const
    {
        std::vector<uint32_t> result;
        for (size_t b = 0; b < buffers.size(); ++b)
            result.push_back(static_cast<uint32_t>(buffers[b].size() / strides[b]));
        return result;
    }
    std::vector<MeshBuffer> views() const
    {
        std::vector<MeshBuffer> result;
        for (size_t b = 0; b < buffers.size(); ++b)
            result.push_back({buffers[b].data(), strides[b], static_cast<uint32_t>(buffers[b].size() / strides[b])});
        return result;
    }
};

[[noreturn]] inline void fail(const std::string &what)
{
    throw std::runtime_error(what);
}

using Position = std::array<float, 3>;
inline Position position(const Model &model, const MeshObject &object, uint32_t index)
{
    Position p;
    std::memcpy(p.data(), model.buffers[object.vertex_buffer].data() +
                              size_t(model.strides[object.vertex_buffer]) * (object.first_vertex + index),
                sizeof(p));
    return p;
}
using Edge = std::pair<Position, Position>;
inline Edge edge(Position a, Position b)
{
    return a < b ? Edge{a, b} : Edge{b, a};
}
struct EdgeHash
{
    size_t operator()(const Edge &e) const
    {
        uint64_t h = 1469598103934665603ull;
        for (const Position *p : {&e.first, &e.second})
            for (const float f : *p)
            {
                uint32_t bits;
                std::memcpy(&bits, &f, sizeof(bits));
                h = (h ^ bits) * 1099511628211ull;
            }
        return h;
    }
};
inline std::array<uint32_t, 3> canonical(const uint16_t *i)
{
    // The smallest of the three rotations (a degenerate triangle has two corners alike).
    const std::array<uint32_t, 3> a{i[0], i[1], i[2]}, b{i[1], i[2], i[0]}, c{i[2], i[0], i[1]};
    return std::min({a, b, c});
}

struct Totals
{
    size_t models = 0, objects = 0, clusters = 0, triangles = 0, encoded = 0;
    std::array<double, far_lod::kLevels> kept{};
    double ms = 0;
    void add(const Totals &o)
    {
        models += o.models, objects += o.objects, clusters += o.clusters, triangles += o.triangles, encoded += o.encoded;
        for (int l = 0; l < far_lod::kLevels; ++l)
            kept[l] += o.kept[l];
        ms += o.ms;
    }
};

inline void threads(size_t count, const std::function<void(size_t, size_t)> &work)
{
    std::vector<std::thread> pool;
    const size_t piece = (count + 3) / 4;
    for (size_t first = 0; first < count; first += piece)
        pool.emplace_back(work, first, std::min(count, first + piece));
    for (auto &thread : pool)
        thread.join();
}

// Builds, checks and measures one model; throws on a broken property.
inline Totals check(const Model &model)
{
    Totals totals;
    if (const auto problem = far_lod::CheckMeshLayout(model.objects, model.counts(), model.triangles))
        fail("layout: " + *problem);
    const auto views = model.views();
    const auto started = std::chrono::steady_clock::now();
    // Built on 4 threads, as the engine builds it (the result must not depend on them).
    const far_lod::Lods lods = far_lod::Build(model.objects, views, model.triangles, threads);
    totals.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (lods.objects.size() != model.objects.size())
        fail("object count");

    // Which (object, cluster) has each edge (by its corners' positions) at level 0.
    std::unordered_map<Edge, std::set<std::pair<size_t, uint32_t>>, EdgeHash> users;
    for (size_t o = 0; o < model.objects.size(); ++o)
        for (uint32_t c = 0; c < lods.objects[o].cluster_count; ++c)
        {
            const far_lod::Cluster &cluster = lods.clusters[lods.objects[o].first_cluster + c];
            for (uint32_t t = 0; t < cluster.count[0]; ++t)
            {
                const uint16_t *i = &lods.indices[3 * (cluster.first[0] + t)];
                for (int k = 0; k < 3; ++k)
                    users[edge(position(model, model.objects[o], i[k]), position(model, model.objects[o], i[(k + 1) % 3]))]
                        .insert({o, c});
            }
        }

    for (size_t o = 0; o < model.objects.size(); ++o)
    {
        const MeshObject &object = model.objects[o];
        const far_lod::ObjectLods &object_lods = lods.objects[o];
        if (object.triangle_count == 0)
        {
            if (object_lods.cluster_count != 0)
                fail("an empty object has clusters");
            continue;
        }
        ++totals.objects;
        totals.triangles += object.triangle_count;
        totals.clusters += object_lods.cluster_count;
        const auto ties = far_lod::DepthTiePairs(object, views[object.vertex_buffer], model.triangles);
        std::set<uint32_t> tied;
        for (const auto &[a, b] : ties)
            tied.insert(a), tied.insert(b);

        // Level 0 of all clusters together: exactly the object's triangles.
        std::multiset<std::array<uint32_t, 3>> file, level0;
        for (uint32_t t = 0; t < object.triangle_count; ++t)
            file.insert(canonical(&model.triangles[3 * size_t(object.first_triangle + t)]));
        for (uint32_t c = 0; c < object_lods.cluster_count; ++c)
        {
            const far_lod::Cluster &cluster = lods.clusters[object_lods.first_cluster + c];
            std::set<uint32_t> cluster_vertices;
            std::multiset<std::array<uint32_t, 3>> cluster0;
            std::unordered_map<Edge, int, EdgeHash> seam0; // the cluster's L0 open edges that another cluster or object also has
            for (uint32_t t = 0; t < cluster.count[0]; ++t)
            {
                const uint16_t *i = &lods.indices[3 * (cluster.first[0] + t)];
                level0.insert(canonical(i));
                cluster0.insert(canonical(i));
                for (int k = 0; k < 3; ++k)
                    cluster_vertices.insert(i[k]);
                for (int k = 0; k < 3; ++k)
                    ++seam0[edge(position(model, object, i[k]), position(model, object, i[(k + 1) % 3]))];
            }
            std::erase_if(seam0, [&](const auto &entry) { return entry.second != 1 || users[entry.first].size() < 2; });
            // The tie triangles of this cluster, in the file's order: the tail of every level.
            std::vector<std::array<uint32_t, 3>> tie_tail;
            for (const uint32_t t : tied)
            {
                const uint16_t *i = &model.triangles[3 * size_t(object.first_triangle + t)];
                if (cluster0.contains(canonical(i)))
                    tie_tail.push_back({i[0], i[1], i[2]});
            }
            for (int l = 0; l < far_lod::kLevels; ++l)
            {
                if (l > 0 && cluster.error[l] < cluster.error[l - 1])
                    fail("level errors go down");
                if (l > 0 && cluster.count[l] > cluster.count[l - 1])
                    fail("a level has more triangles than the one before");
                totals.kept[l] += cluster.count[l];
                std::unordered_map<Edge, int, EdgeHash> open;
                for (uint32_t t = 0; t < cluster.count[l]; ++t)
                {
                    const uint16_t *i = &lods.indices[3 * (cluster.first[l] + t)];
                    for (int k = 0; k < 3; ++k)
                        if (i[k] >= object.vertex_count || !cluster_vertices.contains(i[k]))
                            fail("object " + std::to_string(o) + " level " + std::to_string(l) +
                                 " uses a vertex outside its cluster");
                    for (int k = 0; k < 3; ++k)
                        ++open[edge(position(model, object, i[k]), position(model, object, i[(k + 1) % 3]))];
                }
                // Ties: the last triangles, unchanged, in the file's order.
                if (tie_tail.size() > cluster.count[l])
                    fail("a level lost tie triangles");
                for (size_t k = 0; k < tie_tail.size(); ++k)
                {
                    const uint16_t *i = &lods.indices[3 * (cluster.first[l] + cluster.count[l] - tie_tail.size() + k)];
                    if (std::array<uint32_t, 3>{i[0], i[1], i[2]} != tie_tail[k])
                        fail("object " + std::to_string(o) + " level " + std::to_string(l) + " moved a tie triangle");
                }
                // Seams: every L0 seam edge is still drawn (no crack opens between neighbours; a fold may draw it
                // more than once).
                for (const auto &[e, n] : seam0)
                    if (open[e] < n)
                        fail("object " + std::to_string(o) + " cluster " + std::to_string(c) + " level " +
                             std::to_string(l) + " changed a seam edge (" + std::to_string(n) + " -> " + std::to_string(open[e]) + ", " +
                             std::to_string(cluster.count[l]) + " of " + std::to_string(cluster.count[0]) + " triangles)");
            }
        }
        if (file != level0)
            fail("object " + std::to_string(o) + ": level 0 is not the object's triangles");
    }

    const std::vector<uint8_t> encoded = far_lod::Encode(lods);
    totals.encoded = encoded.size();
    const auto decoded = far_lod::Decode(encoded, model.objects);
    // The index codec may rotate a triangle's corners (the winding stays).
    const auto same_triangles = [](const std::vector<uint16_t> &a, const std::vector<uint16_t> &b) {
        if (a.size() != b.size())
            return false;
        for (size_t t = 0; t < a.size(); t += 3)
            if (canonical(&a[t]) != canonical(&b[t]))
                return false;
        return true;
    };
    if (!decoded || !same_triangles(decoded->indices, lods.indices) || decoded->clusters.size() != lods.clusters.size() ||
        std::memcmp(decoded->clusters.data(), lods.clusters.data(), sizeof(far_lod::Cluster) * lods.clusters.size()) != 0)
        fail("Encode/Decode round trip");
    std::vector<uint8_t> damaged(encoded.begin(), encoded.end() - 1);
    if (far_lod::Decode(damaged, model.objects))
        fail("Decode took truncated data");
    totals.models = 1;
    return totals;
}

} // namespace far_lod_check
