#include "effects/town_light.hpp"

#include "shim/shim.hpp"
#include "texture/core/cache.hpp"
#include "texture/core/formats.hpp"
#include "texture/core/service.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <cstring>
#include <thread>

namespace storm::effects
{

namespace
{
constexpr float kSunStepDegrees = 2.0f; // the sun's visibility is baked again when the hour's sun moved this far
constexpr float kBlendSeconds = 3.0f;   // ... and the lightmap turns to it over this time
constexpr float kUploadSeconds = 0.2f;  // a blending lightmap is composed and uploaded this often
constexpr size_t kFitSamples = 1500;
// A place's bake on disk, in two entries (the app's cache: an entry that is gone is baked again):
//   * its geometry's: per lightmap the sky's visibility on its texels, keyed by the geometry alone (the heavy part,
//     once per town);
//   * its lightmap set's (the game swaps sets by the hour: morning, day1..4, evening): the direction the set was
//     baked with, the sun's light on its texels from there and its tone, keyed by the geometry and the set.
constexpr texture::DerivedShape kSkyShape{.magic = {'S', 'T', 'X', 'C', 'T', 'L', 'S', 'K'}, .version = 1};
constexpr texture::DerivedShape kSetShape{.magic = {'S', 'T', 'X', 'C', 'T', 'L', 'S', 'T'}, .version = 2};
constexpr size_t kMaxPlaceBytes = 64u << 20;

// One cache entry read back in order, or written in order.
struct Entry
{
    std::optional<texture::Blob> blob;
    size_t cursor = 0;
    std::vector<uint8_t> payload;
    bool take(void *out, size_t size)
    {
        if (!blob || cursor + size > blob->size)
            return false;
        std::memcpy(out, blob->data.get() + cursor, size);
        cursor += size;
        return true;
    }
    void put(const void *in, size_t size)
    {
        payload.insert(payload.end(), static_cast<const uint8_t *>(in), static_cast<const uint8_t *>(in) + size);
    }
};

double Since(std::chrono::steady_clock::time_point t)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

uint64_t Hash(uint64_t h, const void *data, size_t size)
{
    const auto *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i)
        h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

float Angle(const std::array<float, 3> &a, const std::array<float, 3> &b)
{
    return std::acos(std::clamp(a[0] * b[0] + a[1] * b[1] + a[2] * b[2], -1.0f, 1.0f)) * 57.29578f;
}

unsigned Threads()
{
    return fx::BakeThreads();
}

// The level `src` (w x h, RGBA8) halved by a box filter.
std::vector<uint8_t> Half(const std::vector<uint8_t> &src, int w, int h)
{
    const int hw = std::max(1, w / 2), hh = std::max(1, h / 2);
    std::vector<uint8_t> out(static_cast<size_t>(hw) * hh * 4);
    for (int y = 0; y < hh; ++y)
        for (int x = 0; x < hw; ++x)
            for (int c = 0; c < 4; ++c)
            {
                int sum = 0;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx)
                        sum += src[(static_cast<size_t>(std::min(h - 1, 2 * y + dy)) * w + std::min(w - 1, 2 * x + dx)) * 4 + c];
                out[(static_cast<size_t>(y) * hw + x) * 4 + c] = static_cast<uint8_t>((sum + 2) / 4);
            }
    return out;
}

} // namespace

void TownLight::set_lightmaps(std::string_view name, std::span<const std::array<float, 3>> positions,
                              std::span<const std::array<float, 2>> uvs, std::span<const uint32_t> indices,
                              std::span<const uint32_t> triangle_lightmap, std::span<const render::EffectsLightmap> lightmaps)
{
    destroy_textures();
    maps_.clear();
    place_job_.reset();
    sun_job_.reset();
    ready_ = false;
    waiting_reported_ = false;
    sun_bounce_.reset();
    name_ = std::string(name);
    positions_.assign(positions.begin(), positions.end());
    uvs_.assign(uvs.begin(), uvs.end());
    indices_.assign(indices.begin(), indices.end());
    triangle_lightmap_.assign(triangle_lightmap.begin(), triangle_lightmap.end());
    lightmaps_.assign(lightmaps.begin(), lightmaps.end());
    if (indices_.empty())
    {
        if (!name_.empty())
            shim::logf("effects: town light of %s: no lightmapped geometry reported: none here", name_.c_str());
        return;
    }
    if (!enabled_)
        shim::logf("effects: town light of %s: baked, but shown only with fx_town_light on (the preset or the option "
                   "has it off)", name_.c_str());
    if (occluders_name_ == name_ && !occluder_indices_.empty())
        start_place();
    else
        shim::logf("effects: town light of %s: waiting for the place's still geometry (%s reported)", name_.c_str(),
                   occluders_name_.empty() ? "none" : occluders_name_.c_str());
}

void TownLight::set_occluders(std::string_view name, std::span<const std::array<float, 3>> positions,
                              std::span<const std::array<float, 2>> uvs, std::span<const uint32_t> indices,
                              std::vector<fx::Occluders::Cut> cut)
{
    occluders_name_ = std::string(name);
    occluder_positions_.assign(positions.begin(), positions.end());
    occluder_indices_.assign(indices.begin(), indices.end());
    occluder_uvs_.assign(uvs.begin(), uvs.end());
    occluder_cut_ = std::move(cut);
    if (!indices_.empty() && occluders_name_ == name_ && !occluder_indices_.empty() && !place_job_ && maps_.empty())
        start_place();
}

void TownLight::start_place()
{
    auto job = std::make_shared<PlaceJob>();
    place_job_ = job;
    // The game's lightmaps' textures, looked up here (main thread): the draws are matched by them.
    std::vector<const shim::TexStore *> stores;
    for (const render::EffectsLightmap &m : lightmaps_)
        stores.push_back(shim::store_of(m.texture));
    texture::Service &service = texture::Service::instance();
    service.post([job, stores, positions = positions_, uvs = uvs_, indices = indices_, triangle_lightmap = triangle_lightmap_,
                  lightmaps = lightmaps_, occ_positions = occluder_positions_, occ_indices = occluder_indices_,
                  occ_uvs = occluder_uvs_, occ_cut = occluder_cut_,
                  directory = service.cache_directory()] {
        const auto started = std::chrono::steady_clock::now();
        uint64_t key = 1469598103934665603ull;
        key = Hash(key, positions.data(), positions.size() * sizeof(positions[0]));
        key = Hash(key, uvs.data(), uvs.size() * sizeof(uvs[0]));
        key = Hash(key, indices.data(), indices.size() * sizeof(uint32_t));
        key = Hash(key, triangle_lightmap.data(), triangle_lightmap.size() * sizeof(uint32_t));
        key = Hash(key, occ_positions.data(), occ_positions.size() * sizeof(occ_positions[0]));
        key = Hash(key, occ_indices.data(), occ_indices.size() * sizeof(uint32_t));
        for (const fx::Occluders::Cut &c : occ_cut)
        {
            const uint8_t solid = c.mask ? 1 : 0;
            key = Hash(key, &solid, 1);
        }
        for (const render::EffectsLightmap &lm : lightmaps)
        {
            const uint32_t size[2] = {lm.width, lm.height};
            key = Hash(key, size, sizeof(size));
        }
        uint64_t set_key = key;
        for (const render::EffectsLightmap &lm : lightmaps)
            set_key = Hash(set_key, lm.data.data(), lm.data.size());
        Entry sky_entry, set_entry;
        if (!directory.empty())
        {
            sky_entry.blob = texture::read_derived_file(directory, key, kSkyShape, kMaxPlaceBytes);
            set_entry.blob = texture::read_derived_file(directory, set_key, kSetShape, kMaxPlaceBytes);
        }
        bool sky_cached = sky_entry.blob.has_value(), set_cached = set_entry.blob.has_value();
        double sky_ms = 0.0, fit_ms = 0.0, sun_ms = 0.0;
        const fx::Occluders occluders(occ_positions, occ_indices, occ_uvs, occ_cut);
        const unsigned threads = Threads();
        std::vector<fx::SunFitSample> samples;
        for (size_t m = 0; m < lightmaps.size(); ++m)
        {
            const render::EffectsLightmap &lm = lightmaps[m];
            uint32_t code; // the .tx format tag is the D3DFORMAT FourCC
            std::memcpy(&code, lm.format.data(), sizeof(code));
            const char kind = lm.format[3];
            Map map;
            map.store = stores[m];
            map.game.resize(static_cast<size_t>(lm.width) * lm.height * 4);
            if (!stores[m] || lm.format[0] != 'D' || !(kind == '1' || kind == '3' || kind == '5') || lm.width < 4 ||
                lm.height < 4 || lm.data.size() < texture::d3d_level_bytes(code, lm.width, lm.height) ||
                !texture::decode_rgba8(code, lm.data.data(), lm.width, lm.height, map.game.data()))
            {
                job->how += " (a lightmap that is no DXT1/3/5 of its size, or has no texture, keeps the game's)";
                continue;
            }
            map.texels = std::make_shared<const fx::LightmapTexels>(fx::RasterizeLightmap(
                positions, uvs, indices, triangle_lightmap, static_cast<uint32_t>(m), static_cast<int>(lm.width),
                static_cast<int>(lm.height)));
            if (map.texels->texel.empty())
                continue;
            map.sky.resize(map.texels->texel.size());
            map.baked.resize(map.texels->texel.size());
            if (!(sky_cached && sky_entry.take(map.sky.data(), map.sky.size())))
            {
                sky_cached = false;
                const auto t = std::chrono::steady_clock::now();
                map.sky = fx::SkyVisibility(occluders, *map.texels, 10.0f, 32, threads);
                sky_ms += Since(t);
            }
            if (!(set_cached && set_entry.take(map.baked.data(), map.baked.size()) && set_entry.take(&map.tone, sizeof(map.tone))))
                set_cached = false;
            // The ground's texels open to the sky, with the game's luminance over its neighbourhood's (the paint's
            // grass and sand are broad, the shadows' edges sharp): what the bake's sun is fitted to.
            const int w = map.texels->width, h = map.texels->height, r = 6;
            std::vector<float> lum(static_cast<size_t>(w) * h), blur(lum.size()), row(lum.size());
            for (size_t i = 0; i < lum.size(); ++i)
                lum[i] = (0.299f * map.game[i * 4] + 0.587f * map.game[i * 4 + 1] + 0.114f * map.game[i * 4 + 2]) / 255.0f;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    float sum = 0.0f;
                    for (int d = -r; d <= r; ++d)
                        sum += lum[static_cast<size_t>(y) * w + std::clamp(x + d, 0, w - 1)];
                    row[static_cast<size_t>(y) * w + x] = sum / (2 * r + 1);
                }
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    float sum = 0.0f;
                    for (int d = -r; d <= r; ++d)
                        sum += row[static_cast<size_t>(std::clamp(y + d, 0, h - 1)) * w + x];
                    blur[static_cast<size_t>(y) * w + x] = sum / (2 * r + 1);
                }
            for (size_t i = 0; i < map.texels->texel.size(); i += 5)
                if (map.texels->normal[i][1] > 0.7f && map.sky[i] > 128)
                {
                    const uint32_t at = map.texels->texel[i];
                    samples.push_back({map.texels->position[i], std::min(2.0f, lum[at] / std::max(blur[at], 0.02f))});
                }
            job->maps.push_back(std::move(map));
        }
        if (samples.size() > kFitSamples)
        {
            std::vector<fx::SunFitSample> spread;
            for (size_t i = 0; i < kFitSamples; ++i)
                spread.push_back(samples[i * samples.size() / kFitSamples]);
            samples.swap(spread);
        }
        auto store = [&](uint64_t entry_key, const texture::DerivedShape &shape, const Entry &entry) {
            if (directory.empty())
                return std::string(" (no cache directory: baked again next time)");
            if (texture::write_derived_file(directory, entry_key, shape, entry.payload.data(), entry.payload.size()) == 0)
                return std::string(" (ERROR: cache write failed: baked again next time)");
            return std::string();
        };
        if (!sky_cached)
        {
            for (const Map &map : job->maps)
                sky_entry.put(map.sky.data(), map.sky.size());
            job->how += store(key, kSkyShape, sky_entry);
        }
        if (!(set_cached && set_entry.take(&job->bake, sizeof(job->bake))))
        {
            set_cached = false;
            auto t = std::chrono::steady_clock::now();
            job->bake = fx::FitBakedSun(occluders, samples, 200.0f, threads);
            fit_ms = Since(t);
            t = std::chrono::steady_clock::now();
            for (Map &map : job->maps)
            {
                map.baked = fx::SunVisibility(occluders, *map.texels, job->bake.dir, 0.3f, 4, threads);
                map.tone = fx::CalibrateTownLight(*map.texels, map.game, map.baked, map.sky);
                set_entry.put(map.baked.data(), map.baked.size());
                set_entry.put(&map.tone, sizeof(map.tone));
            }
            sun_ms = Since(t);
            set_entry.put(&job->bake, sizeof(job->bake));
            job->how += store(set_key, kSetShape, set_entry);
        }
        char how[300];
        std::snprintf(how, sizeof(how),
                      "ready in %.0f ms on %u threads (all but 2 of %u cores): sky %s, lightmap set %s (fit %.0f ms, its sun %.0f ms)",
                      Since(started), threads, std::thread::hardware_concurrency(), sky_cached ? "from the cache" : (std::string("not in the cache: baked in ") +
                                                                                 std::to_string(std::lround(sky_ms)) + " ms").c_str(),
                      set_cached ? "from the cache" : "not in the cache: fitted", fit_ms, sun_ms);
        job->how = how + job->how;
        job->state = 1;
    });
}

void TownLight::start_sun(const std::array<float, 3> &sun)
{
    auto job = std::make_shared<SunJob>();
    job->sun = sun;
    shim::logf("effects: town light of %s: baking the sun's visibility for (%.3f, %.3f, %.3f), %.1f° up", name_.c_str(),
               sun[0], sun[1], sun[2], std::asin(std::clamp(sun[1], -1.0f, 1.0f)) * 57.29578f);
    sun_job_ = job;
    std::shared_ptr<const fx::Occluders> occluders = occluders_;
    std::vector<std::shared_ptr<const fx::LightmapTexels>> texels;
    for (const Map &m : maps_)
        texels.push_back(m.texels);
    std::vector<float> texel_metres;
    for (const Map &m : maps_)
        texel_metres.push_back(0.3f);
    job->baked_bounce = bounce_scene_ && !maps_.empty() && maps_.front().bounce_baked.empty();
    texture::Service::instance().post([job, occluders, texels, texel_metres, scene = bounce_scene_, bake = bake_dir_] {
        const auto started = std::chrono::steady_clock::now();
        const fx::Vec3 dir{job->sun[0], job->sun[1], job->sun[2]};
        for (size_t m = 0; m < texels.size(); ++m)
            job->visibility.push_back(fx::SunVisibility(*occluders, *texels[m], dir, texel_metres[m], 4, Threads()));
        job->ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        if (scene)
        {
            // The sun's bounce for the same direction (a white sun: our lightmaps' sun term is relative to it), read
            // one cell off the texels as the shader reads the volume; once per place the bake's sun's too.
            const auto t = std::chrono::steady_clock::now();
            const fx::BounceScene &s = scene->scene;
            auto gather = [&](const fx::Vec3 &towards) {
                return std::make_shared<const fx::BounceField>(fx::GatherBounce(
                    s, fx::SunOnSurfels(*scene->occluders, s, towards, {1.0f, 1.0f, 1.0f}, Threads()), Threads()));
            };
            job->field = gather(dir);
            for (size_t m = 0; m < texels.size(); ++m)
                job->bounce.push_back(fx::TexelBounce(*texels[m], *job->field, s.grid.cell, Threads()));
            if (job->baked_bounce)
            {
                const std::shared_ptr<const fx::BounceField> baked = gather(bake);
                for (size_t m = 0; m < texels.size(); ++m)
                    job->bounce_baked.push_back(fx::TexelBounce(*texels[m], *baked, s.grid.cell, Threads()));
            }
            job->bounce_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
        }
        job->state = 1;
    });
}

void TownLight::update(const std::array<float, 3> &sun, bool present, float seconds)
{
    if (place_job_ && place_job_->state.load() == 1)
    {
        maps_ = std::move(place_job_->maps);
        occluders_ = std::make_shared<const fx::Occluders>(occluder_positions_, occluder_indices_, occluder_uvs_, occluder_cut_);
        size_t texels = 0, valid = 0;
        for (const Map &m : maps_)
        {
            texels += m.texels->texel.size();
            valid += m.tone.valid ? 1 : 0;
        }
        const fx::SunFit &bake = place_job_->bake;
        bake_dir_ = bake.dir;
        shim::logf("effects: town light of %s: %zu lightmaps (%zu texels, %zu relit), the game's baked with the sun at "
                   "(%.2f, %.2f, %.2f), %.0f° up (r %.2f), %s",
                   name_.c_str(), maps_.size(), texels, valid, bake.dir[0], bake.dir[1], bake.dir[2],
                   std::asin(std::clamp(bake.dir[1], -1.0f, 1.0f)) * 57.29578f, bake.r, place_job_->how.c_str());
        for (const Map &m : maps_)
            shim::logf("effects: town light of %s: a lightmap of %dx%d lit %.3f + %.3f sun + %.3f sky (R² %.2f, sun from %u "
                       "shadow edges), the game's sky for ours 0.06..0.94: %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f%s",
                       name_.c_str(), m.texels->width, m.texels->height, m.tone.a, m.tone.b, m.tone.c, m.tone.r2, m.tone.edges,
                       m.tone.game_sky[0], m.tone.game_sky[1], m.tone.game_sky[2], m.tone.game_sky[3], m.tone.game_sky[4],
                       m.tone.game_sky[5], m.tone.game_sky[6], m.tone.game_sky[7],
                       m.tone.valid ? "" : ": not explained, keeps the game's");
        // A lightmap whose tone the game's does not explain keeps the game's.
        std::erase_if(maps_, [](const Map &m) { return !m.tone.valid; });
        place_job_.reset();
    }
    if (maps_.empty())
        return;
    if (!present && !ready_)
    {
        // The place's weather has not given its sun yet (the first frames after the load), or it is night: the game's
        // lightmap stays until there is a sun to bake for.
        if (!waiting_reported_)
            shim::logf("effects: town light of %s: no sun in the sky yet: the game's lightmap until there is",
                       name_.c_str());
        waiting_reported_ = true;
        return;
    }
    const std::array<float, 3> target = present ? sun : std::array<float, 3>{0.0f, -1.0f, 0.0f};
    if (sun_job_ && sun_job_->state.load() == 1)
    {
        const bool first = !ready_;
        for (size_t m = 0; m < maps_.size() && m < sun_job_->visibility.size(); ++m)
        {
            Map &map = maps_[m];
            if (first || map.sun_to.empty())
                map.sun_from = sun_job_->visibility[m];
            else
            {
                // From where the blend stands now.
                for (size_t i = 0; i < map.sun_from.size(); ++i)
                    map.sun_from[i] = static_cast<uint8_t>(std::lround((1.0f - blend_) * map.sun_from[i] + blend_ * map.sun_to[i]));
            }
            map.sun_to = std::move(sun_job_->visibility[m]);
            if (m < sun_job_->bounce.size())
            {
                if (m < sun_job_->bounce_baked.size())
                    map.bounce_baked = std::move(sun_job_->bounce_baked[m]);
                if (first || map.bounce_to.size() != sun_job_->bounce[m].size())
                    map.bounce_from = sun_job_->bounce[m];
                else
                    for (size_t i = 0; i < map.bounce_from.size(); ++i)
                        map.bounce_from[i] =
                            static_cast<uint8_t>(std::lround((1.0f - blend_) * map.bounce_from[i] + blend_ * map.bounce_to[i]));
                map.bounce_to = std::move(sun_job_->bounce[m]);
            }
        }
        if (sun_job_->field)
            sun_bounce_ = sun_job_->field;
        sun_baked_ = sun_job_->sun;
        if (bounce_scene_ && !sun_job_->field)
            sun_baked_ = {0.0f, 0.0f, 0.0f}; // baked before the bounce scene came: again, with it
        ++generation_;
        shim::logf("effects: town light of %s: sun bake %u for (%.3f, %.3f, %.3f), %.1f° up, in %.0f ms on %u threads (all but 2 "
                   "of the cores)%s",
                   name_.c_str(), generation_, sun_baked_[0], sun_baked_[1], sun_baked_[2],
                   std::asin(std::clamp(sun_baked_[1], -1.0f, 1.0f)) * 57.29578f, sun_job_->ms, Threads(),
                   sun_job_->field ? (", its bounce in " + std::to_string(std::lround(sun_job_->bounce_ms)) + " ms" +
                                      (sun_job_->baked_bounce ? " (with the bake's sun's)" : ""))
                                         .c_str()
                                   : "");
        sun_job_.reset();
        blend_ = first ? 1.0f : 0.0f;
        ready_ = true;
        compose_and_upload();
    }
    if (!sun_job_ && occluders_ && (!ready_ || Angle(target, sun_baked_) > kSunStepDegrees))
        start_sun(target);
    if (ready_ && blend_ < 1.0f)
    {
        blend_ = std::min(1.0f, blend_ + seconds / kBlendSeconds);
        since_upload_ += seconds;
        if (since_upload_ >= kUploadSeconds || blend_ >= 1.0f)
            compose_and_upload();
        if (blend_ >= 1.0f)
            shim::logf("effects: town light of %s: sun bake %u blended in", name_.c_str(), generation_);
    }
}

void TownLight::compose_and_upload()
{
    since_upload_ = 0.0f;
    for (Map &map : maps_)
    {
        if (map.sun_from.size() != map.texels->texel.size() || map.sun_to.size() != map.texels->texel.size())
            continue;
        const int w = map.texels->width, h = map.texels->height;
        if (map.rgba.empty())
            map.rgba = map.game; // texels outside every triangle and the gutter keep the game's
        fx::ComposeTownLight(*map.texels, map.tone, map.game, map.baked, map.sun_from, map.sun_to, blend_, map.sky, map.rgba,
                             {map.bounce_from, map.bounce_to, map.bounce_baked, blend_});
        if (debug_)
            for (size_t i = 0; i < map.texels->texel.size(); ++i)
            {
                const uint8_t v = static_cast<uint8_t>(std::lround((1.0f - blend_) * map.sun_from[i] + blend_ * map.sun_to[i]));
                uint8_t *px = &map.rgba[static_cast<size_t>(map.texels->texel[i]) * 4];
                px[0] = px[1] = px[2] = static_cast<uint8_t>(32 + v * 3 / 4); // MODULATE2X: 0.25..1.0 of the paint x2
            }
        int levels = 1;
        for (int s = std::max(w, h); s > 1; s /= 2)
            ++levels;
        if (!bgfx::isValid(map.texture))
            map.texture = bgfx::createTexture2D(static_cast<uint16_t>(w), static_cast<uint16_t>(h), true, 1,
                                                bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE);
        std::vector<uint8_t> level = map.rgba;
        int lw = w, lh = h;
        for (int l = 0; l < levels; ++l)
        {
            bgfx::updateTexture2D(map.texture, 0, static_cast<uint8_t>(l), 0, 0, static_cast<uint16_t>(lw), static_cast<uint16_t>(lh),
                                  bgfx::copy(level.data(), static_cast<uint32_t>(level.size())));
            if (lw == 1 && lh == 1)
                break;
            level = Half(level, lw, lh);
            lw = std::max(1, lw / 2);
            lh = std::max(1, lh / 2);
        }
    }
}

void TownLight::set_bounce_scene(std::shared_ptr<const Bounce::Scene> scene)
{
    if (scene == bounce_scene_)
        return;
    bounce_scene_ = std::move(scene);
    sun_bounce_.reset();
    for (Map &map : maps_)
    {
        map.bounce_baked.clear();
        map.bounce_from.clear();
        map.bounce_to.clear();
    }
    // The next sun bake gathers the bounce (at once: a bake running now has none).
    sun_baked_ = {0.0f, 0.0f, 0.0f};
    if (ready_)
        compose_and_upload();
}

void TownLight::set_debug(bool debug)
{
    if (debug == debug_)
        return;
    debug_ = debug;
    if (ready_)
    {
        for (Map &map : maps_)
            map.rgba.clear(); // the game's again under the gutter and outside the triangles
        compose_and_upload();
    }
}

bgfx::TextureHandle TownLight::replacement(const shim::TexStore *store)
{
    ++counts_[0];
    if (!active() || !store)
        return BGFX_INVALID_HANDLE;
    for (const Map &map : maps_)
        if (map.store == store && bgfx::isValid(map.texture))
        {
            ++counts_[1];
            return map.texture;
        }
    return BGFX_INVALID_HANDLE;
}

void TownLight::end_frame()
{
    last_counts_ = counts_;
    counts_ = {};
}

void TownLight::destroy_textures()
{
    for (Map &map : maps_)
        if (bgfx::isValid(map.texture))
        {
            bgfx::destroy(map.texture);
            map.texture = BGFX_INVALID_HANDLE;
        }
}

void TownLight::shutdown()
{
    destroy_textures();
    maps_.clear();
    place_job_.reset();
    sun_job_.reset();
    ready_ = false;
}

void TownLight::on_device_lost()
{
    for (Map &map : maps_)
    {
        map.texture = BGFX_INVALID_HANDLE; // went with the device
        map.rgba.clear();
    }
    if (ready_)
        compose_and_upload();
}

} // namespace storm::effects
