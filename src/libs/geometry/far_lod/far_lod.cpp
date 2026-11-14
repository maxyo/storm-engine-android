#include "far_lod.hpp"

#include <meshoptimizer.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace storm::geometry::far_lod
{
namespace
{
// A level that keeps more than this share of the previous level's triangles is not worth its indices: the previous
// level stands in for it.
constexpr double kMinimumGain = 0.9;
// Clusters smaller than this are drawn whole at every level.
constexpr uint32_t kSmallCluster = 16;
// Times a level is made again with the corners of the seams it lost locked; a level still losing one is not used (the
// previous level stands in).
constexpr int kSealAttempts = 8;
// Attribute weights of the simplification: normal (3), uv0 (2). Errors are in metres (meshopt_SimplifyErrorAbsolute);
// the weighted attribute deviation counts as metres too, so the levels also keep shading and texturing.
constexpr float kAttributeWeights[5] = {0.5f, 0.5f, 0.5f, 1.f, 1.f};
constexpr size_t kAttributes = 5;

struct Vec3
{
    float x, y, z;
};

Vec3 position(const MeshBuffer &buffer, const MeshObject &object, uint32_t index)
{
    Vec3 p;
    std::memcpy(&p, buffer.vertices + size_t(buffer.stride) * (object.first_vertex + index), sizeof(p));
    return p;
}

// Union-find over the triangles of an object.
struct Groups
{
    std::vector<uint32_t> parent;
    explicit Groups(uint32_t count) : parent(count)
    {
        std::iota(parent.begin(), parent.end(), 0u);
    }
    uint32_t find(uint32_t t)
    {
        while (parent[t] != t)
            t = parent[t] = parent[parent[t]];
        return t;
    }
    void join(uint32_t a, uint32_t b)
    {
        a = find(a), b = find(b);
        if (a != b)
            parent[std::max(a, b)] = std::min(a, b);
    }
};

// One object's triangles split into clusters (lists of the object's triangle numbers, ascending), and which of its
// triangles tie in depth.
struct Split
{
    std::vector<std::vector<uint32_t>> clusters;
    std::vector<bool> tied;
};

// `ties`: the object's own depth-tie pairs.
Split split(const MeshObject &object, const MeshBuffer &buffer, std::span<const uint16_t> triangles,
            std::span<const std::pair<uint32_t, uint32_t>> ties)
{
    Split result;
    const uint32_t count = object.triangle_count;
    const uint16_t *indices = triangles.data() + 3 * size_t(object.first_triangle);
    result.tied.assign(count, false);
    Groups groups(count);
    for (const auto &[a, b] : ties)
    {
        groups.join(a, b);
        result.tied[a] = result.tied[b] = true;
    }
    const uint32_t cluster_count = std::max(1u, (count + kClusterTriangles / 2) / kClusterTriangles);
    std::vector<uint32_t> cluster_of(count, 0);
    if (cluster_count > 1)
    {
        // The triangles' centroids in meshoptimizer's spatial order, cut into runs of equal size.
        std::vector<Vec3> centroids(count);
        for (uint32_t t = 0; t < count; ++t)
        {
            const Vec3 a = position(buffer, object, indices[3 * t]), b = position(buffer, object, indices[3 * t + 1]),
                       c = position(buffer, object, indices[3 * t + 2]);
            centroids[t] = {(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3};
        }
        std::vector<uint32_t> place(count); // place[triangle] = its position in the spatial order
        meshopt_spatialSortRemap(place.data(), &centroids[0].x, count, sizeof(Vec3));
        for (uint32_t t = 0; t < count; ++t)
            cluster_of[t] = std::min(cluster_count - 1, static_cast<uint32_t>(uint64_t(place[t]) * cluster_count / count));
        // Triangles that tie in depth stay together: in their group's first triangle's cluster.
        for (uint32_t t = 0; t < count; ++t)
            cluster_of[t] = cluster_of[groups.find(t)];
    }
    result.clusters.resize(cluster_count);
    for (uint32_t t = 0; t < count; ++t)
        result.clusters[cluster_of[t]].push_back(t);
    std::erase_if(result.clusters, [](const auto &cluster) { return cluster.empty(); });
    return result;
}

// The key of a position for the seam lock: its exact bits (seams between clusters and objects share vertices or
// copies of them with the same coordinates).
struct PositionKey
{
    uint32_t bits[3];
    bool operator==(const PositionKey &other) const
    {
        return std::memcmp(bits, other.bits, sizeof(bits)) == 0;
    }
};
struct PositionHash
{
    size_t operator()(const PositionKey &key) const
    {
        uint64_t h = 1469598103934665603ull;
        for (const uint32_t b : key.bits)
            h = (h ^ b) * 1099511628211ull;
        return h;
    }
};
PositionKey key(const Vec3 &p)
{
    PositionKey k;
    std::memcpy(k.bits, &p, sizeof(k.bits));
    return k;
}

constexpr uint32_t kShared = UINT32_MAX;

// An edge by its corners' positions, the smaller first.
struct EdgeKey
{
    PositionKey a, b;
    bool operator==(const EdgeKey &other) const
    {
        return a == other.a && b == other.b;
    }
};
struct EdgeHash
{
    size_t operator()(const EdgeKey &e) const
    {
        return PositionHash()(e.a) * 1099511628211ull ^ PositionHash()(e.b);
    }
};
EdgeKey edgeKey(PositionKey a, PositionKey b)
{
    if (std::memcmp(a.bits, b.bits, sizeof(a.bits)) > 0)
        std::swap(a, b);
    return {a, b};
}

// Drops the parts of `level` (sets of triangles connected through shared positions) that are smaller than `error`
// (half their box's diagonal) and touch no locked vertex: meshopt_SimplifyPrune would also drop parts at a seam,
// which opens a crack. Returns the largest dropped half-diagonal.
float prune(const MeshObject &object, const MeshBuffer &buffer, std::span<const unsigned char> locks, float error,
            std::vector<uint32_t> &level)
{
    std::unordered_map<PositionKey, uint32_t, PositionHash> welded;
    std::vector<uint32_t> weld(level.size());
    for (size_t i = 0; i < level.size(); ++i)
        weld[i] = welded.try_emplace(key(position(buffer, object, level[i])), static_cast<uint32_t>(welded.size()))
                      .first->second;
    Groups parts(static_cast<uint32_t>(welded.size()));
    for (size_t t = 0; t < level.size(); t += 3)
        parts.join(weld[t], weld[t + 1]), parts.join(weld[t], weld[t + 2]);
    struct Part
    {
        Vec3 lo{INFINITY, INFINITY, INFINITY}, hi{-INFINITY, -INFINITY, -INFINITY};
        bool locked = false;
    };
    std::unordered_map<uint32_t, Part> boxes;
    for (size_t i = 0; i < level.size(); ++i)
    {
        Part &part = boxes[parts.find(weld[i])];
        const Vec3 p = position(buffer, object, level[i]);
        part.lo = {std::min(part.lo.x, p.x), std::min(part.lo.y, p.y), std::min(part.lo.z, p.z)};
        part.hi = {std::max(part.hi.x, p.x), std::max(part.hi.y, p.y), std::max(part.hi.z, p.z)};
        part.locked = part.locked || locks[level[i]] != 0;
    }
    float dropped = 0;
    const auto size = [](const Part &part) {
        const float dx = part.hi.x - part.lo.x, dy = part.hi.y - part.lo.y, dz = part.hi.z - part.lo.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz) / 2;
    };
    size_t out = 0;
    for (size_t t = 0; t < level.size(); t += 3)
    {
        const Part &part = boxes[parts.find(weld[t])];
        if (!part.locked && size(part) <= error)
        {
            dropped = std::max(dropped, size(part));
            continue;
        }
        level[out++] = level[t], level[out++] = level[t + 1], level[out++] = level[t + 2];
    }
    level.resize(out);
    return dropped;
}

void sphere(const MeshObject &object, const MeshBuffer &buffer, std::span<const uint32_t> indices, Cluster &cluster)
{
    Vec3 lo{INFINITY, INFINITY, INFINITY}, hi{-INFINITY, -INFINITY, -INFINITY};
    for (const uint32_t i : indices)
    {
        const Vec3 p = position(buffer, object, i);
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    cluster.center = {(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2};
    float radius = 0;
    for (const uint32_t i : indices)
    {
        const Vec3 p = position(buffer, object, i);
        const float dx = p.x - cluster.center[0], dy = p.y - cluster.center[1], dz = p.z - cluster.center[2];
        radius = std::max(radius, dx * dx + dy * dy + dz * dz);
    }
    cluster.radius = std::sqrt(radius);
}
} // namespace

bool Wanted(std::string_view model, float radius, bool animated)
{
    const auto folder = [&](std::string_view name) {
        if (model.size() <= name.size() || (model[name.size()] != '\\' && model[name.size()] != '/'))
            return false;
        for (size_t i = 0; i < name.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(model[i])) != name[i])
                return false;
        return true;
    };
    return !animated && (folder("islands") || folder("ships") || (folder("locations") && radius >= kLocationRadius));
}

Lods Build(std::span<const MeshObject> objects, std::span<const MeshBuffer> buffers, std::span<const uint16_t> triangles,
           const Parallel &parallel)
{
    const auto run = [&](size_t count, const std::function<void(size_t, size_t)> &work) {
        if (parallel)
            parallel(count, work);
        else if (count > 0)
            work(0, count);
    };
    // 1. Depth ties over the whole model: inside an object (kept in order, in one cluster) and between objects (a
    // decal of its own over another object's surface: both kept exact). Then the clusters of every object.
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> own_ties(objects.size());
    std::vector<std::vector<uint32_t>> exact(objects.size()); // per object: triangles never simplified (other ties)
    for (const auto &[a, b] : ModelDepthTies(objects, buffers, triangles))
        if (a.object == b.object)
            own_ties[a.object].push_back({a.triangle, b.triangle});
        else
        {
            exact[a.object].push_back(a.triangle);
            exact[b.object].push_back(b.triangle);
        }
    std::vector<Split> splits(objects.size());
    run(objects.size(), [&](size_t first, size_t last) {
        for (size_t o = first; o < last; ++o)
            if (objects[o].triangle_count > 0)
                splits[o] = split(objects[o], buffers[objects[o].vertex_buffer], triangles, own_ties[o]);
    });

    // 2. Owner of every position: the one (object, cluster) using it, or kShared when two use it.
    std::unordered_map<PositionKey, uint32_t, PositionHash> owners;
    struct Work
    {
        uint32_t object, cluster;
    };
    // The same for edges: a seam is an edge two of them have.
    std::unordered_map<EdgeKey, uint32_t, EdgeHash> edge_owners;
    std::vector<Work> work;
    for (size_t o = 0; o < objects.size(); ++o)
    {
        const MeshObject &object = objects[o];
        const MeshBuffer &buffer = buffers[object.vertex_buffer];
        const uint16_t *indices = triangles.data() + 3 * size_t(object.first_triangle);
        for (size_t c = 0; c < splits[o].clusters.size(); ++c)
        {
            const auto owner = static_cast<uint32_t>(work.size());
            for (const uint32_t t : splits[o].clusters[c])
                for (int k = 0; k < 3; ++k)
                {
                    const PositionKey here = key(position(buffer, object, indices[3 * t + k]));
                    const auto [it, added] = owners.try_emplace(here, owner);
                    if (!added && it->second != owner)
                        it->second = kShared;
                    const auto [edge, new_edge] = edge_owners.try_emplace(
                        edgeKey(here, key(position(buffer, object, indices[3 * t + (k + 1) % 3]))), owner);
                    if (!new_edge && edge->second != owner)
                        edge->second = kShared;
                }
            work.push_back({static_cast<uint32_t>(o), static_cast<uint32_t>(c)});
        }
    }

    // 3. Per object: the simplification's attributes (normal, uv0) and locks (seams, depth ties).
    struct Prepared
    {
        std::vector<float> attributes;
        std::vector<unsigned char> locks;
    };
    std::vector<Prepared> prepared(objects.size());
    run(objects.size(), [&](size_t first, size_t last) {
        for (size_t o = first; o < last; ++o)
        {
            const MeshObject &object = objects[o];
            if (object.triangle_count == 0)
                continue;
            const MeshBuffer &buffer = buffers[object.vertex_buffer];
            const uint8_t *base = buffer.vertices + size_t(buffer.stride) * object.first_vertex;
            Prepared &p = prepared[o];
            p.attributes.resize(size_t(object.vertex_count) * kAttributes);
            p.locks.assign(object.vertex_count, 0);
            for (uint32_t v = 0; v < object.vertex_count; ++v)
            {
                std::memcpy(&p.attributes[kAttributes * v], base + size_t(buffer.stride) * v + 12, 3 * sizeof(float));
                std::memcpy(&p.attributes[kAttributes * v + 3], base + size_t(buffer.stride) * v + 28, 2 * sizeof(float));
                Vec3 position;
                std::memcpy(&position, base + size_t(buffer.stride) * v, sizeof(position));
                if (const auto it = owners.find(key(position)); it != owners.end() && it->second == kShared)
                    p.locks[v] = 1;
            }
            // A tie triangle never changes: its corners stay.
            const uint16_t *indices = triangles.data() + 3 * size_t(object.first_triangle);
            for (uint32_t t = 0; t < object.triangle_count; ++t)
                if (splits[o].tied[t])
                    for (int k = 0; k < 3; ++k)
                        p.locks[indices[3 * t + k]] = 1;
            for (const uint32_t t : exact[o])
                for (int k = 0; k < 3; ++k)
                    p.locks[indices[3 * t + k]] = 1;
        }
    });

    // 4. Every cluster's levels, on its own (the cluster's `first` relative to its own indices).
    struct Made
    {
        Cluster cluster;
        std::vector<uint16_t> indices;
    };
    std::vector<Made> made(work.size());
    run(work.size(), [&](size_t first, size_t last) {
        std::vector<uint32_t> source, simplified, ordered, ties, all, lost;
        std::vector<unsigned char> own_locks;
        std::vector<std::pair<EdgeKey, uint32_t>> seams;
        std::unordered_map<EdgeKey, std::pair<int, uint32_t>, EdgeHash> open;
        std::unordered_set<EdgeKey, EdgeHash> drawn;
        for (size_t w = first; w < last; ++w)
        {
            const MeshObject &object = objects[work[w].object];
            const MeshBuffer &buffer = buffers[object.vertex_buffer];
            const Split &parts = splits[work[w].object];
            const Prepared &p = prepared[work[w].object];
            const uint16_t *indices = triangles.data() + 3 * size_t(object.first_triangle);
            const auto *positions = reinterpret_cast<const float *>(buffer.vertices + size_t(buffer.stride) * object.first_vertex);
            Made &out = made[w];
            Cluster &cluster = out.cluster;
            source.clear();
            ties.clear();
            for (const uint32_t t : parts.clusters[work[w].cluster])
                (parts.tied[t] ? ties : source).insert((parts.tied[t] ? ties : source).end(), indices + 3 * t,
                                                       indices + 3 * t + 3);
            all = source;
            all.insert(all.end(), ties.begin(), ties.end());
            sphere(object, buffer, all, cluster);
            // The cluster's seams: its open edges that another cluster or object also has, with the triangle that has
            // them here. Every level must still draw each of them, or a crack opens at a distance where the
            // neighbour is finer.
            seams.clear();
            open.clear();
            for (size_t i = 0; i < source.size(); i += 3)
                for (int k = 0; k < 3; ++k)
                {
                    const EdgeKey e = edgeKey(key(position(buffer, object, source[i + k])),
                                              key(position(buffer, object, source[i + (k + 1) % 3])));
                    auto &[count, triangle] = open[e];
                    ++count;
                    triangle = static_cast<uint32_t>(i);
                }
            for (const auto &[e, entry] : open)
                if (entry.first == 1)
                    if (const auto it = edge_owners.find(e); it != edge_owners.end() && it->second == kShared)
                        seams.push_back({e, entry.second});

            const auto append = [&](const std::vector<uint32_t> &level, int l) {
                ordered.resize(level.size());
                if (!level.empty())
                    meshopt_optimizeVertexCache(ordered.data(), level.data(), level.size(), object.vertex_count);
                cluster.first[l] = static_cast<uint32_t>(out.indices.size() / 3);
                cluster.count[l] = static_cast<uint32_t>((ordered.size() + ties.size()) / 3);
                for (const uint32_t i : ordered)
                    out.indices.push_back(static_cast<uint16_t>(i));
                for (const uint32_t i : ties) // last, in the file's order
                    out.indices.push_back(static_cast<uint16_t>(i));
            };
            append(source, 0);
            cluster.error[0] = 0;
            size_t previous = source.size();
            for (int l = 1; l < kLevels; ++l)
            {
                float error = 0;
                size_t kept = source.size();
                bool sealed = true;
                if (source.size() / 3 >= kSmallCluster)
                {
                    // A seam the level lost: the corners of its triangle are locked and the level made again, until
                    // every seam is drawn (a triangle with all corners locked stays).
                    const unsigned char *locks = p.locks.data();
                    for (int attempt = 0;; ++attempt)
                    {
                        simplified.resize(source.size());
                        kept = meshopt_simplifyWithAttributes(
                            simplified.data(), source.data(), source.size(), positions, object.vertex_count,
                            buffer.stride, p.attributes.data(), kAttributes * sizeof(float), kAttributeWeights,
                            kAttributes, locks, 0, kLevelError[l],
                            meshopt_SimplifySparse | meshopt_SimplifyErrorAbsolute | meshopt_SimplifyPermissive,
                            &error);
                        simplified.resize(kept);
                        error = std::max(error, prune(object, buffer, {locks, object.vertex_count}, kLevelError[l],
                                                      simplified));
                        kept = simplified.size();
                        drawn.clear();
                        for (size_t i = 0; i < simplified.size(); i += 3)
                            for (int k = 0; k < 3; ++k)
                                drawn.insert(edgeKey(key(position(buffer, object, simplified[i + k])),
                                                     key(position(buffer, object, simplified[i + (k + 1) % 3]))));
                        lost.clear();
                        for (const auto &[e, triangle] : seams)
                            if (!drawn.contains(e))
                                lost.push_back(triangle);
                        if (lost.empty())
                            break;
                        if (attempt == kSealAttempts)
                        {
                            sealed = false;
                            break;
                        }
                        if (locks != own_locks.data())
                        {
                            own_locks = p.locks;
                            locks = own_locks.data();
                        }
                        for (const uint32_t i : lost)
                            for (int k = 0; k < 3; ++k)
                                own_locks[source[i + k]] = 1;
                    }
                }
                if (source.size() / 3 < kSmallCluster || !sealed || double(kept) > kMinimumGain * double(previous))
                {
                    // Not worth it: the previous level stands in.
                    cluster.first[l] = cluster.first[l - 1];
                    cluster.count[l] = cluster.count[l - 1];
                    cluster.error[l] = cluster.error[l - 1];
                    continue;
                }
                append(simplified, l);
                cluster.error[l] = error;
                previous = kept;
            }
        }
    });

    // 5. One index array, the clusters in object order.
    Lods lods;
    lods.objects.resize(objects.size());
    size_t w = 0;
    for (size_t o = 0; o < objects.size(); ++o)
    {
        lods.objects[o].first_cluster = static_cast<uint32_t>(lods.clusters.size());
        for (; w < work.size() && work[w].object == o; ++w)
        {
            Cluster cluster = made[w].cluster;
            const auto offset = static_cast<uint32_t>(lods.indices.size() / 3);
            for (int l = 0; l < kLevels; ++l)
                cluster.first[l] += offset;
            lods.indices.insert(lods.indices.end(), made[w].indices.begin(), made[w].indices.end());
            lods.clusters.push_back(cluster);
        }
        lods.objects[o].cluster_count = static_cast<uint32_t>(lods.clusters.size()) - lods.objects[o].first_cluster;
    }
    return lods;
}

namespace
{
constexpr char kMagic[4] = {'F', 'L', 'O', 'D'};

struct Header
{
    char magic[4];
    uint32_t version, objects, clusters, indices, encoded;
};

template <class T> void put(std::vector<uint8_t> &out, const T *data, size_t count)
{
    const auto *bytes = reinterpret_cast<const uint8_t *>(data);
    out.insert(out.end(), bytes, bytes + sizeof(T) * count);
}

template <class T> bool take(std::span<const uint8_t> &in, T *data, size_t count)
{
    const size_t size = sizeof(T) * count;
    if (in.size() < size)
        return false;
    std::memcpy(data, in.data(), size);
    in = in.subspan(size);
    return true;
}
} // namespace

std::vector<uint8_t> Encode(const Lods &lods)
{
    const std::vector<uint32_t> indices(lods.indices.begin(), lods.indices.end());
    std::vector<uint8_t> encoded(meshopt_encodeIndexBufferBound(indices.size(), 65536));
    encoded.resize(meshopt_encodeIndexBuffer(encoded.data(), encoded.size(), indices.data(), indices.size()));
    Header header;
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = kVersion;
    header.objects = static_cast<uint32_t>(lods.objects.size());
    header.clusters = static_cast<uint32_t>(lods.clusters.size());
    header.indices = static_cast<uint32_t>(lods.indices.size());
    header.encoded = static_cast<uint32_t>(encoded.size());
    std::vector<uint8_t> out;
    put(out, &header, 1);
    put(out, lods.objects.data(), lods.objects.size());
    put(out, lods.clusters.data(), lods.clusters.size());
    put(out, encoded.data(), encoded.size());
    return out;
}

std::optional<Lods> Decode(std::span<const uint8_t> data, std::span<const MeshObject> objects)
{
    Header header;
    // Sizes the model can have at most: no level exceeds level 0, which is the model's triangles; no cluster is empty
    // at level 0. Checked before anything is allocated (the cache is not trusted).
    uint64_t model_triangles = 0;
    for (const MeshObject &object : objects)
        model_triangles += object.triangle_count;
    if (!take(data, &header, 1) || std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 || header.version != kVersion ||
        header.objects != objects.size() || header.indices % 3 != 0 ||
        header.indices > 3 * uint64_t(kLevels) * model_triangles || header.clusters > model_triangles ||
        data.size() != size_t(header.encoded) + sizeof(ObjectLods) * header.objects +
                           sizeof(Cluster) * size_t(header.clusters))
        return std::nullopt;
    Lods lods;
    lods.objects.resize(header.objects);
    lods.clusters.resize(header.clusters);
    lods.indices.resize(header.indices);
    if (!take(data, lods.objects.data(), lods.objects.size()) || !take(data, lods.clusters.data(), lods.clusters.size()) ||
        meshopt_decodeIndexBuffer(lods.indices.data(), lods.indices.size(), sizeof(uint16_t), data.data(), data.size()) != 0)
        return std::nullopt;
    // Every range inside the data and every index inside its object's vertex range: the cache is not trusted.
    const uint64_t triangle_count = header.indices / 3;
    for (size_t o = 0; o < objects.size(); ++o)
    {
        const ObjectLods &object = lods.objects[o];
        if (uint64_t(object.first_cluster) + object.cluster_count > lods.clusters.size() ||
            (objects[o].triangle_count == 0) != (object.cluster_count == 0))
            return std::nullopt;
        for (uint32_t c = object.first_cluster; c < object.first_cluster + object.cluster_count; ++c)
            for (int l = 0; l < kLevels; ++l)
            {
                const Cluster &cluster = lods.clusters[c];
                // A level never has more triangles than the one before (the composed lists are sized by level 0).
                if (uint64_t(cluster.first[l]) + cluster.count[l] > triangle_count ||
                    (l > 0 && cluster.count[l] > cluster.count[l - 1]))
                    return std::nullopt;
                for (uint64_t i = 3 * uint64_t(cluster.first[l]); i < 3 * (uint64_t(cluster.first[l]) + cluster.count[l]); ++i)
                    if (lods.indices[i] >= objects[o].vertex_count)
                        return std::nullopt;
            }
    }
    return lods;
}

} // namespace storm::geometry::far_lod
