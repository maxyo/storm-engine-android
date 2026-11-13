#include "effects/visibility.hpp"

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
constexpr texture::DerivedShape kShape{.magic = {'S', 'T', 'X', 'C', 'V', 'I', 'S', 'V'}, .version = fx::kVisibilityBakeVersion};
constexpr size_t kMaxCacheBytes = 4u << 20;
constexpr float kInteriorCell = 0.5f; // metres: doorways, galleries and stairs resolve
constexpr float kOutdoorCell = 1.0f;  // awnings, arcades and alleys; grown under the cell budget
constexpr uint32_t kOutdoorCells = 1u << 19;
constexpr float kFocusMargin = 8.0f;  // metres around a town's ground (its walls stand on its edge)
constexpr float kFocusAir = 25.0f;    // metres above its highest ground: roofs, towers, palms
} // namespace

fx::VisibilityParams Visibility::params_for() const
{
    fx::VisibilityParams params;
    params.cell = interior_ ? kInteriorCell : kOutdoorCell;
    if (interior_)
    {
        if (room_hi_[0] > room_lo_[0] && room_hi_[1] > room_lo_[1] && room_hi_[2] > room_lo_[2])
        {
            params.lo = {room_lo_[0], room_lo_[1], room_lo_[2]};
            params.hi = {room_hi_[0], room_hi_[1], room_hi_[2]};
        }
        return params;
    }
    params.max_cells = kOutdoorCells;
    if (focus_name_ == name_ && focus_hi_[0] > focus_lo_[0])
    {
        params.lo = {focus_lo_[0] - kFocusMargin, focus_lo_[1] - 2.0f, focus_lo_[2] - kFocusMargin};
        params.hi = {focus_hi_[0] + kFocusMargin, focus_hi_[1] + kFocusAir, focus_hi_[2] + kFocusMargin};
    }
    return params;
}

void Visibility::set_geometry(std::string name, std::vector<fx::Vec3> positions, std::vector<uint32_t> indices, bool interior,
                              const std::array<float, 3> &room_lo, const std::array<float, 3> &room_hi)
{
    if (name == name_ && positions == positions_ && indices == indices_ && interior == interior_ && room_lo == room_lo_ &&
        room_hi == room_hi_)
        return; // the same location again: keep its volume
    name_ = std::move(name);
    positions_ = std::move(positions);
    indices_ = std::move(indices);
    interior_ = interior;
    room_lo_ = room_lo;
    room_hi_ = room_hi;
    restart();
}

void Visibility::set_focus(std::string_view name, const std::array<float, 3> &lo, const std::array<float, 3> &hi)
{
    focus_name_ = std::string(name);
    focus_lo_ = lo;
    focus_hi_ = hi;
    if (focus_name_ == name_ && !interior_)
    {
        const fx::VisibilityParams params = params_for();
        if (params.lo != params_.lo || params.hi != params_.hi)
            restart();
    }
}

void Visibility::restart()
{
    destroy_texture();
    job_.reset(); // a worker still running finishes into its own job, which nobody reads
    params_ = params_for();
    state_ = indices_.empty() ? State::None : State::Building;
}

void Visibility::start()
{
    auto job = std::make_shared<Job>();
    job_ = job;
    texture::Service &service = texture::Service::instance();
    service.post([job, name = name_, positions = positions_, indices = indices_, params = params_,
                  directory = service.cache_directory()]() mutable {
        params.threads = std::max(1u, std::thread::hardware_concurrency() / 2);
        const uint64_t key = fx::VisibilityKey(positions, indices, params);
        if (!directory.empty())
            if (std::optional<texture::Blob> cached = texture::read_derived_file(directory, key, kShape, kMaxCacheBytes))
            {
                std::string error;
                if (std::optional<fx::VisibilityVolume> volume = fx::DecodeVisibility({cached->data.get(), cached->size}, key, error))
                {
                    job->volume = std::move(*volume);
                    job->how = "from the cache";
                    job->state = 1;
                    return;
                }
                shim::logf("ERROR: effects: visibility of %s: cached entry refused (%s): baking it again", name.c_str(),
                           error.c_str());
            }
        const auto started = std::chrono::steady_clock::now();
        const fx::Occluders occluders(positions, indices);
        job->volume = fx::BakeVisibility(occluders, positions, indices, params);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        if (job->volume.Empty())
        {
            job->state = 2;
            return;
        }
        char how[120];
        std::snprintf(how, sizeof(how), "not in the cache: baked in %.0f ms on %u threads", ms, params.threads);
        job->how = how;
        if (directory.empty())
            job->how += " (no cache directory: baked again next time)";
        else
        {
            const std::vector<uint8_t> bytes = fx::EncodeVisibility(job->volume, key);
            if (texture::write_derived_file(directory, key, kShape, bytes.data(), bytes.size()) == 0)
                job->how += " (ERROR: cache write failed: baked again next time)";
        }
        job->state = 1;
    });
}

const Visibility::Bound *Visibility::update()
{
    if (state_ == State::None || state_ == State::Failed)
        return nullptr;
    if (state_ == State::Ready)
        return &bound_;
    if (!job_)
        start();
    switch (job_->state.load())
    {
    case 1:
    {
        const fx::VisibilityVolume &v = job_->volume;
        bound_.texture = bgfx::createTexture3D(
            static_cast<uint16_t>(v.size[0]), static_cast<uint16_t>(v.size[1]), static_cast<uint16_t>(v.size[2]), false,
            bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP,
            bgfx::copy(v.rgba.data(), static_cast<uint32_t>(v.rgba.size())));
        if (!bgfx::isValid(bound_.texture))
        {
            shim::logf("ERROR: effects: visibility of %s: the %dx%dx%d RGBA8 3D texture cannot be created: everything "
                       "here counts as under open sky",
                       name_.c_str(), v.size[0], v.size[1], v.size[2]);
            state_ = State::Failed;
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
        bound_.size = v.size;
        float top = -1e30f;
        for (const uint32_t i : indices_)
            top = std::max(top, positions_[i][1]);
        shim::logf("effects: visibility of %s: %dx%dx%d cells of %.2f m from y %.1f to %.1f (%zu collision triangles up to %.1f), %s",
                   name_.c_str(), v.size[0], v.size[1], v.size[2], v.cell, v.origin[1], v.origin[1] + v.cell * v.size[1],
                   indices_.size() / 3, top,
                   job_->how.c_str());
        job_->volume = {};
        state_ = State::Ready;
        return &bound_;
    }
    case 2:
        shim::logf("ERROR: effects: visibility of %s: nothing to bake (%zu positions, %zu indices): everything here counts "
                   "as under open sky",
                   name_.c_str(), positions_.size(), indices_.size());
        state_ = State::Failed;
        return nullptr;
    default:
        return nullptr; // on the worker
    }
}

void Visibility::destroy_texture()
{
    if (bgfx::isValid(bound_.texture))
        bgfx::destroy(bound_.texture);
    bound_ = {};
}

void Visibility::shutdown()
{
    destroy_texture();
    job_.reset();
    state_ = indices_.empty() ? State::None : State::Building;
}

void Visibility::on_device_lost()
{
    bound_ = {}; // went with the device
    job_.reset(); // the data was released after the upload: load it again (cache) for the new device
    if (state_ == State::Ready)
        state_ = State::Building;
}

} // namespace storm::effects
