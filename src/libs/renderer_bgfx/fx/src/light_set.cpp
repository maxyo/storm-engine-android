#include "storm/fx/light_set.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{
namespace
{
float Peak(Vec3 c)
{
    return std::max({c[0], c[1], c[2]});
}
} // namespace

LightShape ShapeOf(const PointLight &light)
{
    return {light.range, light.intensity, light.core};
}

float LightAttenuation(const PointLight &light, float distance)
{
    return LightFalloffGammaAt(ShapeOf(light), distance);
}

LightSelection SelectLights(std::span<const PointLight> lights, Vec3 center, float radius, int max_count, bool include_baked,
                            bool fade_rank)
{
    struct Scored
    {
        float score;
        const PointLight *light;
    };
    std::vector<Scored> scored;
    scored.reserve(lights.size());
    for (const PointLight &light : lights)
    {
        if (light.baked && !include_baked)
            continue;
        const float dx = light.position[0] - center[0], dy = light.position[1] - center[1], dz = light.position[2] - center[2];
        const float nearest = std::max(std::sqrt(dx * dx + dy * dy + dz * dz) - radius, 0.0f);
        // Brightness at the closest point of the object; attenuation is clamped so a light inside the object
        // does not win by an infinite margin.
        const float score = Peak(light.color) * std::min(LightAttenuation(light, nearest), 4.0f);
        if (score > 1.0f / 512.0f)
            scored.push_back({score, &light});
    }
    const size_t keep = std::min<size_t>(scored.size(), static_cast<size_t>(std::clamp(max_count, 0, kMaxPointLights)));
    std::partial_sort(scored.begin(), scored.begin() + static_cast<std::ptrdiff_t>(keep), scored.end(),
                      [](const Scored &a, const Scored &b) { return a.score > b.score; });
    LightSelection selection;
    selection.dropped = static_cast<int>(scored.size() - keep);
    for (size_t i = 0; i < keep; ++i)
        selection.lights[selection.count++] = *scored[i].light;
    if (fade_rank && keep > 0 && scored.size() > keep)
    {
        // partial_sort leaves the rest unordered: the strongest left-out light is the largest of them.
        float next = 0.0f;
        for (size_t i = keep; i < scored.size(); ++i)
            next = std::max(next, scored[i].score);
        const float last = scored[keep - 1].score;
        const float weight = std::clamp((last - next) / (kRankFade * last), 0.0f, 1.0f);
        for (float &c : selection.lights[keep - 1].color)
            c *= weight;
    }
    return selection;
}

void FlashLights::Add(Vec3 position, Vec3 peak_color, float range, float duration)
{
    if (duration <= 0.0f || range <= 0.0f)
        return;
    if (flashes_.size() >= kCapacity)
        flashes_.erase(std::max_element(flashes_.begin(), flashes_.end(),
                                        [](const Flash &a, const Flash &b) { return a.age / a.duration < b.age / b.duration; }));
    flashes_.push_back({position, peak_color, range, duration, 0.0f});
}

void FlashLights::Update(float delta_seconds)
{
    for (Flash &flash : flashes_)
        flash.age += delta_seconds;
    std::erase_if(flashes_, [](const Flash &flash) { return flash.age >= flash.duration; });
}

void FlashLights::AppendTo(std::vector<PointLight> &lights, const LightFalloff &falloff) const
{
    for (const Flash &flash : flashes_)
    {
        const float left = 1.0f - flash.age / flash.duration;
        PointLight light;
        light.position = flash.position;
        // Squared fade: most of the energy in the first frames, then a soft afterglow.
        for (int i = 0; i < 3; ++i)
            light.color[i] = flash.color[i] * left * left;
        // Linear + quadratic attenuation scaled to the range (about 1/20 of the peak at its end), shaped as every light.
        const LightShape shape = ShapeOfGameLight(flash.range, 1.0f, 3.0f / flash.range, 16.0f / (flash.range * flash.range), falloff);
        light.range = shape.range;
        light.intensity = shape.intensity;
        light.core = shape.core;
        lights.push_back(light);
    }
}

PointLightUniforms PackPointLights(const LightSelection &selection, const std::array<float, 16> &view)
{
    PointLightUniforms u;
    for (int i = 0; i < selection.count; ++i)
    {
        const PointLight &l = selection.lights[i];
        auto &position = u.u_fxPoint[4 * i], &color = u.u_fxPoint[4 * i + 1], &params = u.u_fxPoint[4 * i + 2], &spot = u.u_fxPoint[4 * i + 3];
        for (int j = 0; j < 3; ++j)
        {
            position[j] = l.position[0] * view[j] + l.position[1] * view[4 + j] + l.position[2] * view[8 + j] + view[12 + j];
            spot[j] = l.direction[0] * view[j] + l.direction[1] * view[4 + j] + l.direction[2] * view[8 + j];
            color[j] = l.color[j];
        }
        position[3] = l.range;
        color[3] = l.intensity;
        params[0] = l.core * l.core;
        params[1] = 0.0f;
        // Spot cone as saturate(rho * scale + bias); a point light gets scale 0, bias 1.
        const bool is_spot = l.cos_outer > -1.0f;
        const float width = std::max(l.cos_inner - l.cos_outer, 1e-4f);
        params[2] = is_spot ? 1.0f / width : 0.0f;
        params[3] = is_spot ? -l.cos_outer / width : 1.0f;
        spot[3] = l.falloff;
    }
    u.u_fxPointCount = {static_cast<float>(selection.count), 0.0f, 0.0f, 0.0f};
    u.dropped = selection.dropped;
    return u;
}

} // namespace storm::fx
