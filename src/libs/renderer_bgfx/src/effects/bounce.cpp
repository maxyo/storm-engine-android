#include "effects/bounce.hpp"

#include "effects/occluder_log.hpp"

#include "effects/casters.hpp"
#include "effects/visibility.hpp"
#include "shim/shim.hpp"
#include "texture/core/cache.hpp"
#include "texture/core/service.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <thread>

namespace storm::effects
{

namespace
{
constexpr texture::DerivedShape kShape{.magic = {'S', 'T', 'X', 'C', 'B', 'N', 'C', 'E'}, .version = fx::kBounceBakeVersion};
constexpr size_t kMaxCacheBytes = 96u << 20;
constexpr float kSunStepDegrees = 2.0f;  // as the town light: the sun's field is gathered again when it moved this far
constexpr float kColorStep = 0.03f;      // the weather's sun colour is written again when a channel moved this far

unsigned Threads()
{
    return fx::BakeThreads();
}

double Since(std::chrono::steady_clock::time_point t)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

float Angle(const std::array<float, 3> &a, const std::array<float, 3> &b)
{
    return std::acos(std::clamp(a[0] * b[0] + a[1] * b[1] + a[2] * b[2], -1.0f, 1.0f)) * 57.29578f;
}

// Surfels of a metre, or of a town's 1.3 m cells: the bounce is a soft light; finer ones only cost the lamps' and the
// sun's rays (a tavern had 370k at 0.5 m).
fx::BounceParams ParamsFor(const fx::BounceGrid &grid)
{
    fx::BounceParams params;
    params.surfel = std::max(1.0f, grid.cell);
    params.threads = Threads();
    return params;
}
} // namespace

void Bounce::set_enabled(bool enabled)
{
    if (enabled == enabled_)
        return;
    enabled_ = enabled;
    if (!enabled_)
        restart();
}

void Bounce::set_geometry(const render::EffectsLocationGeometry &geometry)
{
    name_ = std::string(geometry.name);
    positions_.assign(geometry.positions.begin(), geometry.positions.end());
    uvs_.assign(geometry.uvs.begin(), geometry.uvs.end());
    indices_.assign(geometry.indices.begin(), geometry.indices.end());
    // The textures' colours (main thread: their CPU copies) and what their alpha cuts away, per triangle.
    textures_.clear();
    triangle_texture_.assign(indices_.size() / 3, fx::kBounceNoTexture);
    cut_.assign(indices_.size() / 3, {});
    unreadable_textures_ = 0;
    std::map<const shim::TexStore *, uint32_t> seen;
    if (uvs_.size() == positions_.size())
        for (const auto &surface : geometry.surfaces)
        {
            shim::TexStore *store = shim::store_of(surface.texture);
            if (!store || surface.first_index + surface.index_count > indices_.size())
                continue;
            auto [at, added] = seen.try_emplace(store, static_cast<uint32_t>(textures_.size()));
            if (added)
            {
                textures_.push_back(store->albedo);
                if (!store->albedo)
                    ++unreadable_textures_;
            }
            const CasterMaterial material = location_caster_material(store);
            for (uint32_t t = surface.first_index / 3; t < (surface.first_index + surface.index_count) / 3; ++t)
            {
                triangle_texture_[t] = at->second;
                if (material.cut > 0.0f)
                    cut_[t] = {store->cutout, material.cut};
            }
        }
    restart();
}

void Bounce::restart()
{
    scene_job_.reset(); // a worker still running finishes into its own job, which nobody reads
    light_job_.reset();
    scene_.reset();
    sun_field_.reset();
    lamp_field_.reset();
    sun_baked_ = {0.0f, 0.0f, 0.0f};
    color_written_ = {-1.0f, -1.0f, -1.0f};
    lamps_baked_ = 0xffffffffu;
    grid_ = {};
    failed_ = false;
    generation_ = 0;
}

void Bounce::start_scene(const fx::BounceGrid &grid)
{
    auto job = std::make_shared<SceneJob>();
    texture::Service &service = texture::Service::instance();
    scene_job_.start("bounce scene of " + name_, job,
                 [job, name = name_, positions = positions_, uvs = uvs_, indices = indices_, triangle_texture = triangle_texture_,
                  textures = textures_, cut = cut_, grid, directory = service.cache_directory()](fx::BakeStage stage) mutable {
        const auto started = std::chrono::steady_clock::now();
        auto scene = std::make_shared<Scene>();
        // Built in the stage that finishes the scene (the load when it is in the cache, else the bake).
        // Only what the rays from the grid's cells can reach (the sun's run farthest): a backdrop is left out.
        auto build_occluders = [&] {
            const fx::Vec3 far{grid.origin[0] + grid.cell * static_cast<float>(grid.size[0]),
                               grid.origin[1] + grid.cell * static_cast<float>(grid.size[1]),
                               grid.origin[2] + grid.cell * static_cast<float>(grid.size[2])};
            const fx::Vec3 corners[2] = {grid.origin, far};
            scene->occluders = std::make_shared<const fx::Occluders>(positions, indices, uvs, cut,
                                                                     fx::BoundsOf(corners, fx::kSunRayReach));
            LogExcluded("bounce", name, *scene->occluders);
        };
        std::vector<const fx::BounceTexture *> images;
        for (const auto &t : textures)
            images.push_back(t.get());
        const fx::BounceGeometry geometry{positions, uvs, indices, triangle_texture, images};
        const fx::BounceParams params = ParamsFor(grid);
        const uint64_t key = fx::BounceKey(geometry, grid, params);
        if (stage == fx::BakeStage::Load)
        {
            if (!directory.empty())
                if (std::optional<texture::Blob> cached = texture::read_derived_file(directory, key, kShape, kMaxCacheBytes))
                {
                    std::string error;
                    if (std::optional<fx::BounceScene> decoded = fx::DecodeBounceScene({cached->data.get(), cached->size}, key, error))
                    {
                        scene->scene = std::move(*decoded);
                        build_occluders();
                        char how[96];
                        std::snprintf(how, sizeof(how), "from the cache in %.0f ms", Since(started));
                        job->how = how;
                        job->scene = std::move(scene);
                        job->state = 1;
                        return true;
                    }
                    shim::logf("ERROR: effects: bounce of %s: cached scene refused (%s): baking it again", name.c_str(), error.c_str());
                }
            return false; // not in the cache: baked on the bake thread
        }
        build_occluders();
        scene->scene = fx::BuildBounceScene(*scene->occluders, geometry, grid, params);
        if (scene->scene.Empty())
        {
            job->state = 2;
            return true;
        }
        char how[160];
        std::snprintf(how, sizeof(how), "not in the cache: baked in %.0f ms on %u threads", Since(started), params.threads);
        job->how = how;
        if (directory.empty())
            job->how += " (no cache directory: baked again next time)";
        else
        {
            const std::vector<uint8_t> bytes = fx::EncodeBounceScene(scene->scene, key);
            if (bytes.size() > kMaxCacheBytes)
                job->how += " (too large for the cache: baked again next time)";
            else if (texture::write_derived_file(directory, key, kShape, bytes.data(), bytes.size()) == 0)
                job->how += " (ERROR: cache write failed: baked again next time)";
        }
        job->scene = std::move(scene);
        job->state = 1;
        return true;
    });
}

void Bounce::update(Visibility &visibility, const Visibility::Bound *bound, const Light &light)
{
    if (!enabled_ || indices_.empty() || failed_ || !bound)
    {
        visibility.set_stacked(false);
        return;
    }
    if (!visibility.set_stacked(true))
        return; // too tall for both blocks (logged)
    const fx::BounceGrid grid{bound->size, bound->origin, bound->cell};
    if (grid.size != grid_.size || grid.origin != grid_.origin || grid.cell != grid_.cell)
    {
        restart();
        grid_ = grid;
        start_scene(grid);
    }
    if (scene_job_)
    {
        const int state = scene_job_->state.load();
        if (state == 0)
            return;
        if (state == 2)
        {
            shim::logf("ERROR: effects: bounce of %s: nothing to bake (%zu triangles): no bounce here", name_.c_str(),
                       indices_.size() / 3);
            failed_ = true;
            scene_job_.reset();
            return;
        }
        scene_ = scene_job_->scene;
        const fx::BounceScene &s = scene_->scene;
        shim::logf("effects: bounce of %s: %zu surfels, %dx%dx%d points (every %d cells of %.2f m) x %d rays, %zu textures "
                   "(%u unreadable: %u triangles grey), %s",
                   name_.c_str(), s.position.size(), s.points[0], s.points[1], s.points[2], s.stride, s.grid.cell, s.rays,
                   textures_.size(), unreadable_textures_, s.untextured, scene_job_->how.c_str());
        scene_job_.reset();
    }
    if (!scene_)
        return;
    // A finished light job: its fields, and the texture.
    if (light_job_ && light_job_->state.load() == 1)
    {
        LightJob &job = *light_job_;
        if (job.sun)
            sun_field_ = job.sun;
        if (job.lamps)
            lamp_field_ = job.lamps;
        visibility.set_bounce(job.directions, std::move(job.block), job.lamp_share);
        ++generation_;
        shim::logf("effects: bounce of %s: field %u written (sun %s, lamps %s, lamp share %.2f, light mean %.3f p99 %.3f, "
                   "written in %.0f ms)",
                   name_.c_str(), generation_,
                   job.sun ? (std::to_string(std::lround(job.sun_ms)) + " ms").c_str() : "kept",
                   job.lamps ? (std::to_string(std::lround(job.lamps_ms)) + " ms").c_str() : "kept", job.lamp_share,
                   job.mean, job.p99, job.write_ms);
        light_job_.reset();
    }
    if (light_job_)
        return;
    // What changed: the sun (our own step, or the town light's field), its colour, the lamps.
    const std::array<float, 3> sun = light.sun_present ? light.sun : std::array<float, 3>{0.0f, -1.0f, 0.0f};
    bool new_sun = false;
    std::shared_ptr<const fx::BounceField> given;
    if (light.town_sun)
    {
        given = light.town_field;
        new_sun = given && given != sun_field_;
    }
    else
        new_sun = !sun_field_ || Angle(sun, sun_baked_) > kSunStepDegrees;
    const bool new_lamps = light.lamp_layout != lamps_baked_ || !lamp_field_;
    bool new_color = false;
    for (int k = 0; k < 3; ++k)
        new_color = new_color || std::fabs(light.sun_color[k] - color_written_[k]) > kColorStep;
    if (!new_sun && !new_lamps && !new_color)
        return;
    if (light.town_sun && !given && !sun_field_)
        return; // the town light's first field is on its way
    auto job = std::make_shared<LightJob>();
    std::vector<fx::Lamp> lamps = light.lamps ? *light.lamps : std::vector<fx::Lamp>{};
    std::vector<float> holders = light.holders ? *light.holders : std::vector<float>{};
    if (new_sun && !light.town_sun)
        sun_baked_ = sun;
    if (new_lamps)
        lamps_baked_ = light.lamp_layout;
    color_written_ = light.sun_color;
    const bool bake_sun = new_sun && !light.town_sun;
    light_job_.start("bounce light of " + name_, job,
                                      [job, scene = scene_, sun_field = new_sun && light.town_sun ? given : sun_field_,
                                       lamp_field = lamp_field_, bake_sun, sun, new_lamps, lamps = std::move(lamps),
                                       holders = std::move(holders), color = light.sun_color] {
        const unsigned threads = Threads();
        const fx::BounceScene &s = scene->scene;
        std::shared_ptr<const fx::BounceField> sun_now = sun_field, lamps_now = lamp_field;
        if (bake_sun)
        {
            const auto t = std::chrono::steady_clock::now();
            const fx::Vec3 dir{sun[0], sun[1], sun[2]};
            // No sun (night, indoors): no field.
            job->sun = dir[1] <= 0.0f ? std::make_shared<const fx::BounceField>()
                                      : std::make_shared<const fx::BounceField>(fx::GatherBounce(
                                            s, fx::SunOnSurfels(*scene->occluders, s, dir, {1.0f, 1.0f, 1.0f}, threads), threads));
            job->sun_ms = Since(t);
            sun_now = job->sun;
        }
        else if (sun_field != nullptr)
            job->sun = sun_field; // the town light's, taken in
        if (new_lamps)
        {
            const auto t = std::chrono::steady_clock::now();
            job->lamps = std::make_shared<const fx::BounceField>(
                fx::GatherBounce(s, fx::LampsOnSurfels(*scene->occluders, s, lamps, holders, threads), threads));
            job->lamps_ms = Since(t);
            lamps_now = job->lamps;
        }
        const auto t = std::chrono::steady_clock::now();
        const size_t cells = static_cast<size_t>(s.grid.size[0]) * s.grid.size[1] * s.grid.size[2];
        job->directions.assign(cells * 4, 0);
        const fx::BounceField none;
        const fx::BounceField &sun_part = sun_now ? *sun_now : none;
        if (sun_part.Empty())
            // Lamps only (indoors, at night): their field alone, unscaled.
            fx::EncodeBounceBlocks(s.grid, lamps_now ? *lamps_now : none, {1.0f, 1.0f, 1.0f}, nullptr, job->directions, job->block, threads);
        else
            fx::EncodeBounceBlocks(s.grid, sun_part, color, lamps_now.get(), job->directions, job->block, threads);
        const double lamp_energy = lamps_now ? lamps_now->Energy() : 0.0;
        const double sun_energy =
            sun_part.Empty() ? 0.0 : sun_part.Energy() * (0.299 * color[0] + 0.587 * color[1] + 0.114 * color[2]);
        job->lamp_share = lamp_energy + sun_energy > 0.0 ? static_cast<float>(lamp_energy / (lamp_energy + sun_energy)) : 0.0f;
        job->write_ms = Since(t);
        // The light it holds, for the log: the mean and the 99th percentile of c's luminance over the cells.
        std::vector<float> lum(cells);
        for (size_t c = 0; c < cells; ++c)
        {
            float l = 0.0f;
            for (int ch = 0; ch < 3; ++ch)
            {
                const float e = job->block[c * 4 + ch] / 255.0f;
                l += (ch == 0 ? 0.299f : ch == 1 ? 0.587f : 0.114f) * e * e * fx::kBounceRange;
            }
            lum[c] = l;
        }
        double sum = 0.0;
        for (const float l : lum)
            sum += l;
        job->mean = static_cast<float>(sum / std::max<size_t>(1, cells));
        std::nth_element(lum.begin(), lum.begin() + cells * 99 / 100, lum.end());
        job->p99 = cells ? lum[cells * 99 / 100] : 0.0f;
        job->state = 1;
    });
}

void Bounce::shutdown()
{
    restart();
}

} // namespace storm::effects
