#include "hull_waterline.h"

#include "core.h"
#include "geos.h"
#include "sea_base.h"
#include "storm_glm.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

namespace storm::ship
{

std::shared_ptr<const HullProfile> HullProfile::Get(const std::string &model, GEOS &geo)
{
    static std::mutex mutex;
    static std::map<std::string, std::shared_ptr<const HullProfile>> cache;
    const std::lock_guard lock(mutex);
    if (const auto it = cache.find(model); it != cache.end())
        return it->second;

    GEOS::INFO info;
    geo.GetInfo(info);
    auto profile = std::make_shared<HullProfile>();
    const float hx = 0.5f * info.boxsize.x, hy = 0.5f * info.boxsize.y, hz = 0.5f * info.boxsize.z;
    profile->z0 = info.boxcenter.z - hz;
    profile->z1 = info.boxcenter.z + hz;
    profile->y0 = info.boxcenter.y - hy;
    profile->y1 = info.boxcenter.y + hy;
    // Rays across the hull at every section and height, from outside the box to the centre plane, one per side:
    // where each first meets the hull is its half beam there (0: no hull, e.g. beyond the stem or under the keel).
    const float x_out = std::fabs(info.boxcenter.x) + hx + 1.0f;
    int hits = 0;
    for (int side = 0; side < 2; ++side)
    {
        const float sign = side == 0 ? 1.0f : -1.0f;
        auto &beam = profile->beam_[side];
        beam.assign(kAlong * kUp, 0.0f);
        for (int i = 0; i < kAlong; ++i)
            for (int j = 0; j < kUp; ++j)
            {
                const float z = profile->z0 + (profile->z1 - profile->z0) * (static_cast<float>(i) + 0.5f) / kAlong;
                const float y = profile->y0 + (profile->y1 - profile->y0) * (static_cast<float>(j) + 0.5f) / kUp;
                GEOS::VERTEX src{sign * x_out, y, z}, dst{0.0f, y, z};
                const float t = geo.Trace(src, dst);
                if (t >= 0.0f && t <= 1.0f)
                {
                    beam[i * kUp + j] = x_out * (1.0f - t);
                    ++hits;
                }
            }
    }
    if (hits == 0)
    {
        core.Trace("ERROR: waves from objects: the hull of %s has no collision BSP (or the traces met nothing): its "
                   "ripples and wake take its bounding box instead (logged once per model)",
                   model.c_str());
        profile.reset();
    }
    cache[model] = profile;
    return profile;
}

float HullProfile::HalfBeam(int side, float z, float y) const
{
    const float fz = std::clamp((z - z0) / (z1 - z0) * kAlong - 0.5f, 0.0f, kAlong - 1.0f);
    const float fy = std::clamp((y - y0) / (y1 - y0) * kUp - 0.5f, 0.0f, kUp - 1.0f);
    const int iz = std::min(static_cast<int>(fz), kAlong - 2), iy = std::min(static_cast<int>(fy), kUp - 2);
    const float tz = fz - iz, ty = fy - iy;
    const auto &b = beam_[side];
    const auto at = [&](int i, int j) { return b[i * kUp + j]; };
    // A section ending (half beam 0 next to a hull) is the hull's end, not a slope down to it: the nearer value.
    const auto lerp = [](float a, float c, float t) {
        if (a <= 0.0f || c <= 0.0f)
            return t < 0.5f ? a : c;
        return a + (c - a) * t;
    };
    return lerp(lerp(at(iz, iy), at(iz + 1, iy), tz), lerp(at(iz, iy + 1), at(iz + 1, iy + 1), tz), ty);
}

namespace
{
// Local height at which the hull side at local (x, z) meets the sea: the sea's height under the point, brought into
// the hull's frame along its local up (a few fixed-point steps: the sea varies slowly over a hull's beam).
float WaterHeight(const glm::mat4 &world, SEA_BASE &sea, float x, float z, float y)
{
    const float up = std::max(world[1][1], 0.3f); // world y per local y
    for (int step = 0; step < 3; ++step)
    {
        const glm::vec3 p = storm::math::transformPoint(world, glm::vec3(x, y, z));
        y -= (p.y - sea.WaveXZ(p.x, p.z)) / up;
    }
    return y;
}
} // namespace

bool HullWaterline(const HullProfile &profile, const glm::mat4 &world, const glm::vec3 &origin,
                   const glm::vec2 &forward, SEA_BASE &sea, render::EffectsWaveSource &source)
{
    constexpr int kStations = render::EffectsWaveSource::kHullStations;
    constexpr int kScan = 48;
    // The water along the centre line, then the waterline's ends: where the hull there has any beam at that height.
    std::array<float, kScan> water{};
    float y = 0.5f * (profile.y0 + profile.y1);
    const auto z_at = [&](float t) { return profile.z0 + (profile.z1 - profile.z0) * t; };
    for (int i = 0; i < kScan; ++i)
    {
        const float z = z_at((static_cast<float>(i) + 0.5f) / kScan);
        y = WaterHeight(world, sea, 0.0f, z, y);
        water[i] = y;
    }
    int first = -1, last = -1;
    for (int i = 0; i < kScan; ++i)
    {
        const float z = z_at((static_cast<float>(i) + 0.5f) / kScan);
        if (profile.HalfBeam(0, z, water[i]) + profile.HalfBeam(1, z, water[i]) > 0.05f)
        {
            if (first < 0)
                first = i;
            last = i;
        }
    }
    if (first < 0)
        return false;
    // The ends half a scan step beyond the last sections with beam.
    const float za = z_at(static_cast<float>(first) / kScan), zb = z_at(static_cast<float>(last + 1) / kScan);
    const glm::vec2 right(-forward.y, forward.x);
    const auto flat = [&](const glm::vec3 &p) { return glm::vec2(p.x - origin.x, p.z - origin.z); };
    const auto along_of = [&](float z, float yy) {
        return glm::dot(flat(storm::math::transformPoint(world, glm::vec3(0.0f, yy, z))), forward);
    };
    const auto water_at = [&](float z) {
        const float f = std::clamp((z - profile.z0) / (profile.z1 - profile.z0) * kScan - 0.5f, 0.0f, kScan - 1.0f);
        const int i = std::min(static_cast<int>(f), kScan - 2);
        return water[i] + (water[i + 1] - water[i]) * (f - i);
    };
    float a0 = along_of(za, water_at(za)), a1 = along_of(zb, water_at(zb));
    const bool reversed = a1 < a0; // the model's +z is its bow in the game, but do not rely on it
    source.stern = std::min(a0, a1);
    source.stem = std::max(a0, a1);
    float widest = 0.0f;
    for (int k = 0; k < kStations; ++k)
    {
        // Stations from stern to stem, the end ones just inside the ends (their beam is what the ends close to).
        float t = (static_cast<float>(k) + 0.5f) / kStations;
        if (reversed)
            t = 1.0f - t;
        const float z = za + (zb - za) * t;
        glm::vec2 edge[2];
        for (int side = 0; side < 2; ++side)
        {
            const float sign = side == 0 ? 1.0f : -1.0f;
            float yy = water_at(z), x = 0.0f;
            for (int step = 0; step < 2; ++step)
            {
                x = sign * profile.HalfBeam(side, z, yy);
                yy = WaterHeight(world, sea, x, z, yy);
            }
            x = sign * profile.HalfBeam(side, z, yy);
            edge[side] = flat(storm::math::transformPoint(world, glm::vec3(x, yy, z)));
        }
        const float s0 = glm::dot(edge[0], right), s1 = glm::dot(edge[1], right);
        source.station_side[k] = 0.5f * (s0 + s1);
        source.station_half[k] = 0.5f * std::fabs(s0 - s1);
        widest = std::max(widest, source.station_half[k]);
    }
    source.half_length = 0.5f * (source.stem - source.stern);
    source.half_width = widest;
    return true;
}

} // namespace storm::ship
