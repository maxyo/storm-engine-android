#include "geom_far_lod.hpp"

#include <storm/render/far_lod.hpp>
#include <storm/render/perf_stats.hpp>
#include <storm/render/texture_cache.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numeric>
#include <spdlog/spdlog.h>
#include <strings.h>

namespace far_lod = storm::geometry::far_lod;
namespace render_lod = storm::render::far_lod;

namespace
{
constexpr char kCacheMagic[8] = {'S', 'T', 'M', 'F', 'L', 'O', 'D', '1'};
constexpr uint64_t kKeySeed = 0x464152204c4f4453ull; // "FAR LODS"
constexpr size_t kMaxCacheBytes = 256u << 20;
// A cluster goes to a coarser level only when that level's error is this much under the limit, so a cluster at the
// threshold distance does not switch back and forth.
constexpr float kHysteresis = 1.25f;

storm::render::DerivedDataShape cacheShape(const GeomFarLod::Source &source)
{
    storm::render::DerivedDataShape shape;
    std::memcpy(shape.magic, kCacheMagic, sizeof(shape.magic));
    shape.version = far_lod::kVersion;
    shape.width = static_cast<uint32_t>(source.triangles.size() / 3);
    shape.height = static_cast<uint32_t>(source.objects.size());
    return shape;
}

// What the LODs are made from: the objects, the triangles and of every vertex its position, normal and uv0 (not its
// colour: the baked lighting differs per time of day and does not change the LODs).
uint64_t cacheKey(const GeomFarLod::Source &source)
{
    uint64_t key = storm::render::DerivedDataHash(&far_lod::kVersion, sizeof(far_lod::kVersion), kKeySeed);
    key = storm::render::DerivedDataHash(source.objects.data(), source.objects.size() * sizeof(far_lod::MeshObject), key);
    key = storm::render::DerivedDataHash(source.triangles.data(), source.triangles.size() * sizeof(uint16_t), key);
    std::vector<uint8_t> shape;
    for (size_t b = 0; b < source.buffers.size(); ++b)
    {
        const uint32_t stride = source.strides[b];
        const size_t count = source.buffers[b].size() / stride;
        shape.resize(count * 32);
        for (size_t v = 0; v < count; ++v)
        {
            const uint8_t *vertex = source.buffers[b].data() + v * stride;
            std::memcpy(&shape[32 * v], vertex, 24);           // position, normal
            std::memcpy(&shape[32 * v + 24], vertex + 28, 8);  // uv0
        }
        key = storm::render::DerivedDataHash(shape.data(), shape.size(), key);
    }
    return key;
}

std::vector<far_lod::MeshBuffer> views(const GeomFarLod::Source &source)
{
    std::vector<far_lod::MeshBuffer> result;
    for (size_t b = 0; b < source.buffers.size(); ++b)
        result.push_back({source.buffers[b].data(), source.strides[b],
                          static_cast<uint32_t>(source.buffers[b].size() / source.strides[b])});
    return result;
}

} // namespace

struct GeomFarLod::Job
{
    Source source;
    // 0 queued or being made, 1 ready, 2 failed, 3 dropped before it finished (the bakes shut down with the device:
    // the app went to the background): queued again at the next draw.
    std::atomic<int> state{0};
    std::optional<uint64_t> key; // the cache key, computed by the first stage that runs
    std::optional<far_lod::Lods> lods;
    std::string failure;
    bool from_cache = false;
    double ms = 0;
};

namespace
{
// Marks the job dropped when the bake queue lets go of its step without the job having finished.
struct DropGuard
{
    std::shared_ptr<GeomFarLod::Job> job;
    ~DropGuard()
    {
        int running = 0;
        job->state.compare_exchange_strong(running, 3);
    }
};

// The LODs from the cache, or nothing. A damaged entry is logged (and made again).
bool readCache(GeomFarLod::Job &job)
{
    const auto started = std::chrono::steady_clock::now();
    const auto data = storm::render::ReadDerivedData(*job.key, cacheShape(job.source), kMaxCacheBytes);
    if (!data)
        return false;
    job.lods = far_lod::Decode(*data, job.source.objects);
    if (!job.lods)
    {
        spdlog::error("far LOD: the cache entry of {} does not decode into its objects: made again", job.source.name);
        return false;
    }
    job.from_cache = true;
    job.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return true;
}

// Done (ready or failed): the copies of the model's data go, only the object table stays.
void finish(GeomFarLod::Job &job, int state)
{
    job.source.buffers = {};
    job.source.strides = {};
    job.source.triangles = {};
    job.state = state;
}
} // namespace

bool GeomFarLod::Wanted(const char *file_name, float radius, bool animated)
{
    std::string_view model(file_name);
    constexpr std::string_view kModels = "resource\\models\\";
    if (model.size() > kModels.size() && strncasecmp(file_name, kModels.data(), kModels.size()) == 0)
        model.remove_prefix(kModels.size());
    return far_lod::Wanted(model, radius, animated);
}

GeomFarLod::GeomFarLod(Source source, GEOM_SERVICE &srv) : srv_(srv), name_(source.name)
{
    job_ = std::make_shared<Job>();
    job_->source = std::move(source);
}

void GeomFarLod::post()
{
    posted_ = true;
    ++render_lod::pending;
    auto guard = std::make_shared<DropGuard>(DropGuard{job_});
    ticket_ = storm::fx::PostBake("far LOD " + name_, [guard](storm::fx::BakeStage stage) {
        Job &job = *guard->job;
        if (!job.key)
            job.key = cacheKey(job.source);
        if (stage == storm::fx::BakeStage::Load)
        {
            try
            {
                if (!readCache(job))
                    return false;
            }
            catch (const std::exception &error)
            {
                spdlog::error("far LOD: reading the cache entry of {} failed ({}): made again", job.source.name,
                              error.what());
                return false;
            }
            finish(job, 1);
            return true;
        }
        // Another copy of the same model (ships of one class) may have made them since this one looked.
        if (readCache(job))
        {
            finish(job, 1);
            return true;
        }
        const auto started = std::chrono::steady_clock::now();
        try
        {
            const auto buffers = views(job.source);
            job.lods = far_lod::Build(job.source.objects, buffers, job.source.triangles,
                                      [](size_t count, const std::function<void(size_t, size_t)> &work) {
                                          storm::fx::ParallelRanges(count, storm::fx::BakeThreads(), work, 1);
                                      });
        }
        catch (const storm::fx::BakeCancelled &)
        {
            throw; // the owner let go, or the bakes shut down: the bake thread logs it
        }
        catch (const std::exception &error)
        {
            job.failure = error.what();
            finish(job, 2);
            return true;
        }
        job.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        const std::vector<uint8_t> encoded = far_lod::Encode(*job.lods);
        if (!storm::render::DerivedDataCacheEnabled())
            spdlog::error("far LOD: no cache directory: the LODs of {} are made again on every load", job.source.name);
        else if (!storm::render::WriteDerivedData(*job.key, cacheShape(job.source), encoded))
            spdlog::error("far LOD: cannot write the LODs of {} into the cache", job.source.name);
        finish(job, 1);
        return true;
    });
}

GeomFarLod::~GeomFarLod()
{
    ticket_.reset(); // cancels the job if it is still queued or running
    if (job_ && posted_)
        --render_lod::pending;
    if (static_buffer_ != -1)
        srv_.ReleaseIndexBuffer(static_buffer_);
    for (auto &passes : slots_)
        for (Slot &slot : passes)
            if (slot.buffer != -1)
                srv_.ReleaseIndexBuffer(slot.buffer);
}

bool GeomFarLod::Ready()
{
    if (lods_)
        return true;
    if (!job_ || render_lod::pixels <= 0.f)
        return false; // failed before, or far LODs off: nothing is made while they are off
    if (!posted_)
    {
        post();
        return false;
    }
    switch (job_->state.load())
    {
    case 0:
        return false;
    case 3:
        spdlog::info("far LOD: making the LODs of {} stopped with the background work (the device was let go): "
                     "queued again", name_);
        --render_lod::pending;
        job_->state = 0;
        post();
        return false;
    default:
        install();
        return lods_.has_value();
    }
}

void GeomFarLod::install()
{
    std::shared_ptr<Job> job = std::move(job_);
    ticket_.reset();
    --render_lod::pending;
    if (job->state == 2)
    {
        spdlog::error("far LOD: making the LODs of {} failed ({}): it is drawn whole", name_, job->failure);
        return;
    }
    far_lod::Lods &lods = *job->lods;
    const size_t bytes = lods.indices.size() * sizeof(uint16_t);
    static_buffer_ = srv_.CreateIndexBuffer(static_cast<int32_t>(bytes));
    auto *target = static_cast<uint16_t *>(srv_.LockIndexBuffer(static_buffer_));
    if (target == nullptr)
    {
        spdlog::error("far LOD: no index buffer of {} bytes for {}: it is drawn whole", bytes, name_);
        return;
    }
    std::memcpy(target, lods.indices.data(), bytes);
    srv_.UnlockIndexBuffer(static_buffer_);
    slots_.resize(lods.objects.size());
    std::array<uint64_t, far_lod::kLevels> kept{};
    for (const far_lod::Cluster &cluster : lods.clusters)
        for (int l = 0; l < far_lod::kLevels; ++l)
            kept[l] += cluster.count[l];
    std::string levels;
    for (int l = 1; l < far_lod::kLevels; ++l)
        levels += fmt::format(" {:.0f}%", 100.0 * kept[l] / std::max<uint64_t>(1, kept[0]));
    // Made: once per model and game data (info); read back from the cache on every later load (debug).
    spdlog::log(job->from_cache ? spdlog::level::debug : spdlog::level::info,
                "far LOD: {}: {} clusters, {} triangles, levels{} ({} in {:.0f} ms)", name_, lods.clusters.size(),
                kept[0], levels, job->from_cache ? "read from the cache" : "made", job->ms);
    lods_ = std::move(lods);
}

bool GeomFarLod::choose(Slot &slot, uint32_t first_cluster, uint32_t count, const render_lod::View &view) const
{
    const float limit = render_lod::pixels;
    if (slot.levels.size() != count)
        slot.levels.assign(count, 0);
    bool changed = false;
    for (uint32_t c = 0; c < count; ++c)
    {
        const far_lod::Cluster &cluster = lods_->clusters[first_cluster + c];
        float per_metre = view.pixels_per_unit; // pixels of one metre of error
        if (!view.ortho)
        {
            const float dx = cluster.center[0] - view.camera[0], dy = cluster.center[1] - view.camera[1],
                        dz = cluster.center[2] - view.camera[2];
            const float distance = (std::sqrt(dx * dx + dy * dy + dz * dz) - cluster.radius) * view.scale;
            // Not clamped to the near plane: an island's depth slices move it, and the choice must be the same in
            // all of them (one composed list serves every slice).
            per_metre /= std::max(distance, 0.01f);
        }
        const auto error = [&](int l) { return cluster.error[l] * view.scale * per_metre; };
        int level = slot.levels[c];
        while (level > 0 && error(level) > limit)
            --level;
        while (level + 1 < far_lod::kLevels && error(level + 1) * kHysteresis < limit)
            ++level;
        if (level != slot.levels[c])
        {
            changed = true;
            slot.levels[c] = static_cast<uint8_t>(level);
        }
    }
    return changed;
}

bool GeomFarLod::compose(Slot &slot, uint32_t first_cluster, uint32_t count)
{
    const render_lod::View &view = render_lod::view;
    if (slot.buffer == -1)
    {
        slot.capacity = 0;
        for (uint32_t c = 0; c < count; ++c)
            slot.capacity += lods_->clusters[first_cluster + c].count[0];
        slot.buffer = srv_.CreateIndexBuffer(static_cast<int32_t>(slot.capacity * 3 * sizeof(uint16_t)));
    }
    // Nearest first: a depth slice draws one run of them (Draw).
    slot.order.resize(count);
    std::iota(slot.order.begin(), slot.order.end(), 0u);
    std::vector<float> distance(count);
    for (uint32_t c = 0; c < count; ++c)
    {
        const far_lod::Cluster &cluster = lods_->clusters[first_cluster + c];
        const float dx = cluster.center[0] - view.camera[0], dy = cluster.center[1] - view.camera[1],
                    dz = cluster.center[2] - view.camera[2];
        distance[c] = dx * dx + dy * dy + dz * dz;
    }
    std::ranges::sort(slot.order, [&](uint32_t a, uint32_t b) { return distance[a] < distance[b]; });
    auto *target = static_cast<uint16_t *>(srv_.LockIndexBuffer(slot.buffer));
    if (target == nullptr)
    {
        // Out of index buffers: this object of this pass is drawn whole from now on.
        spdlog::error("far LOD: no index buffer of {} triangles for an object of {}: that object is drawn whole",
                      slot.capacity, name_);
        slot.failed = true;
        return false;
    }
    slot.offset.resize(count);
    slot.triangles = 0;
    for (const uint32_t c : slot.order)
    {
        const far_lod::Cluster &cluster = lods_->clusters[first_cluster + c];
        const int level = slot.levels[c];
        slot.offset[c] = slot.triangles;
        std::memcpy(target + 3 * size_t(slot.triangles), lods_->indices.data() + 3 * size_t(cluster.first[level]),
                    3 * sizeof(uint16_t) * cluster.count[level]);
        slot.triangles += cluster.count[level];
    }
    srv_.UnlockIndexBuffer(slot.buffer);
    slot.composed = true;
    ++render_lod::counters.composes;
    render_lod::counters.compose_bytes += 3 * sizeof(uint16_t) * size_t(slot.triangles);
    return true;
}

bool GeomFarLod::Draw(int32_t o, const GEOS::OBJECT &object, int32_t stride)
{
    const render_lod::View &view = render_lod::view;
    if (render_lod::pixels <= 0.f || !view.valid || !lods_)
        return false;
    const far_lod::ObjectLods &lod = lods_->objects[o];
    if (lod.cluster_count == 0)
        return false;
    const int pass = render_lod::pass;
    Slot &slot = slots_[o][pass];
    uint32_t first = 0, triangles = 0;
    GEOS::ID buffer = static_buffer_;
    if (lod.cluster_count == 1)
    {
        choose(slot, lod.first_cluster, 1, view);
        const far_lod::Cluster &cluster = lods_->clusters[lod.first_cluster];
        first = cluster.first[slot.levels[0]];
        triangles = cluster.count[slot.levels[0]];
    }
    else
    {
        if (slot.failed)
            return false;
        // A rewrite of the list mid-frame is safe: a draw from a buffer written this frame takes its indices as they
        // are at that draw (the shim copies them into the frame's transient indices).
        if ((choose(slot, lod.first_cluster, lod.cluster_count, view) || !slot.composed) &&
            !compose(slot, lod.first_cluster, lod.cluster_count))
            return false;
        buffer = slot.buffer;
        // Only the clusters that reach into the depth range drawn (the island's depth slices): one run of the
        // nearest-first order covers them all.
        uint32_t lo = UINT32_MAX, hi = 0;
        for (uint32_t k = 0; k < lod.cluster_count; ++k)
        {
            const uint32_t c = slot.order[k];
            const far_lod::Cluster &cluster = lods_->clusters[lod.first_cluster + c];
            if (!view.ortho)
            {
                const float depth = ((cluster.center[0] - view.camera[0]) * view.forward[0] +
                                     (cluster.center[1] - view.camera[1]) * view.forward[1] +
                                     (cluster.center[2] - view.camera[2]) * view.forward[2]) *
                                    view.scale;
                const float radius = cluster.radius * view.scale;
                if (depth + radius < view.near_plane || depth - radius > view.far_plane)
                    continue;
            }
            lo = std::min(lo, k), hi = std::max(hi, k);
        }
        if (lo == UINT32_MAX)
        {
            render_lod::counters.full_triangles[pass] += object.ntriangles;
            return true; // nothing of it in this depth range
        }
        const uint32_t last = slot.order[hi];
        first = slot.offset[slot.order[lo]];
        triangles = slot.offset[last] + lods_->clusters[lod.first_cluster + last].count[slot.levels[last]] - first;
    }
    render_lod::counters.full_triangles[pass] += object.ntriangles;
    render_lod::counters.drawn_triangles[pass] += triangles;
    if (triangles == 0)
        return true;
    srv_.SetIndexBuffer(buffer);
    srv_.DrawIndexedPrimitive(object.start_vertex, object.num_vertices, stride, static_cast<int32_t>(first * 3),
                              static_cast<int32_t>(triangles));
    return true;
}
