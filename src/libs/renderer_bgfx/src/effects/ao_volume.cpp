#include "effects/ao_volume.hpp"

#include "shim/shim.hpp"
#include "texture/core/cache.hpp"
#include "texture/core/service.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

namespace storm::effects
{

namespace
{
// One cache kind for every volume: the size is inside the payload (fx::EncodeAoVolume), checked by DecodeAoVolume.
constexpr texture::DerivedShape kShape{.magic = {'S', 'T', 'X', 'C', 'A', 'O', 'V', 'L'}, .version = fx::kAoBakeVersion};
constexpr size_t kMaxCacheBytes = 8u << 20;
} // namespace

void AoVolumes::set_geometry(std::string name, std::vector<fx::Vec3> positions, std::vector<uint32_t> indices,
                             const fx::Vec3 &bounds_lo, const fx::Vec3 &bounds_hi, std::vector<fx::Vec3> walk)
{
    const bool bounded = bounds_lo[0] < bounds_hi[0];
    const fx::Vec3 lo = bounded ? bounds_lo : fx::Vec3{1.0f, 1.0f, 1.0f};
    const fx::Vec3 hi = bounded ? bounds_hi : fx::Vec3{-1.0f, -1.0f, -1.0f};
    if (name == name_ && positions == positions_ && indices == indices_ && lo == bounds_lo_ && hi == bounds_hi_ && walk == walk_)
        return; // the same location again: keep its volume
    bounds_lo_ = lo;
    bounds_hi_ = hi;
    destroy_texture();
    job_.reset(); // a worker still running finishes into its own job, which nobody reads
    failure_reported_ = false;
    name_ = std::move(name);
    positions_ = std::move(positions);
    indices_ = std::move(indices);
    walk_ = std::move(walk);
}

void AoVolumes::start()
{
    auto job = std::make_shared<Job>();
    job->name = name_;
    job_ = job;
    texture::Service &service = texture::Service::instance();
    service.post([job, positions = positions_, indices = indices_, lo = bounds_lo_, hi = bounds_hi_, walk = walk_,
                  directory = service.cache_directory()] {
        fx::AoBakeParams params;
        params.bounds_lo = lo;
        params.bounds_hi = hi;
        params.walk = walk;
        params.threads = std::max(1u, std::thread::hardware_concurrency() / 2);
        const fx::AoGeometry geometry{positions, indices};
        const uint64_t key = fx::AoBakeKey(geometry, params);
        if (!directory.empty())
            if (std::optional<texture::Blob> cached = texture::read_derived_file(directory, key, kShape, kMaxCacheBytes))
            {
                std::string error;
                if (std::optional<fx::AoVolume> volume = fx::DecodeAoVolume({cached->data.get(), cached->size}, key, error))
                {
                    job->volume = std::move(*volume);
                    job->how = "from the cache";
                    job->state = Job::Ready;
                    return;
                }
                shim::logf("ERROR: effects: AO volume of %s: cached entry refused (%s): baking it again", job->name.c_str(),
                           error.c_str());
            }
        const auto started = std::chrono::steady_clock::now();
        job->volume = fx::BakeAoVolume(geometry, params);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        if (job->volume.Empty())
        {
            job->state = Job::Failed;
            return;
        }
        char how[96];
        std::snprintf(how, sizeof(how), "baked in %.0f ms on %u threads", ms, params.threads);
        job->how = how;
        if (directory.empty())
            job->how += " (no cache directory: baked again next time)";
        else
        {
            const std::vector<uint8_t> bytes = fx::EncodeAoVolume(job->volume, key);
            if (texture::write_derived_file(directory, key, kShape, bytes.data(), bytes.size()) == 0)
                job->how += " (ERROR: cache write failed: baked again next time)";
        }
        job->state = Job::Ready;
    });
}

const AoVolumes::Bound *AoVolumes::update(bool wanted)
{
    if (!wanted || positions_.empty())
        return nullptr;
    if (!job_)
        start();
    switch (job_->state.load())
    {
    case Job::Ready:
        if (!bgfx::isValid(bound_.texture))
        {
            const fx::AoVolume &v = job_->volume;
            bound_.texture = bgfx::createTexture3D(
                static_cast<uint16_t>(v.size[0]), static_cast<uint16_t>(v.size[1]), static_cast<uint16_t>(v.size[2]), false,
                bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP,
                bgfx::copy(v.visibility.data(), static_cast<uint32_t>(v.visibility.size())));
            if (!bgfx::isValid(bound_.texture))
            {
                shim::logf("ERROR: effects: AO volume of %s: the %dx%dx%d RGBA8 3D texture cannot be created: no ambient "
                           "occlusion here",
                           name_.c_str(), v.size[0], v.size[1], v.size[2]);
                job_->state = Job::Failed;
                failure_reported_ = true;
                return nullptr;
            }
            bound_.world_to_uvw = {};
            for (int i = 0; i < 3; ++i)
            {
                const float extent = v.cell * static_cast<float>(v.size[i]);
                bound_.world_to_uvw[i * 5] = 1.0f / extent;
                bound_.world_to_uvw[12 + i] = -v.origin[i] / extent;
            }
            bound_.world_to_uvw[15] = 1.0f;
            bound_.cell = v.cell;
            shim::logf("effects: AO volume of %s: %dx%dx%d cells of %.2f m %s", name_.c_str(), v.size[0], v.size[1],
                       v.size[2], v.cell, job_->how.c_str());
            job_->volume = {};
        }
        return &bound_;
    case Job::Failed:
        if (!failure_reported_)
            shim::logf("ERROR: effects: AO volume of %s: nothing to bake (%zu positions, %zu indices): no ambient "
                       "occlusion here",
                       name_.c_str(), positions_.size(), indices_.size());
        failure_reported_ = true;
        return nullptr;
    default:
        return nullptr; // on the worker: appears a few frames later, behind the loading fader
    }
}

void AoVolumes::destroy_texture()
{
    if (bgfx::isValid(bound_.texture))
        bgfx::destroy(bound_.texture);
    bound_ = {};
}

void AoVolumes::shutdown()
{
    destroy_texture();
    job_.reset();
}

void AoVolumes::on_device_lost()
{
    bound_ = {};
    if (job_ && job_->state == Job::Ready)
        job_.reset(); // the volume data was released after the upload: load it again (cache) for the new device
}

} // namespace storm::effects
