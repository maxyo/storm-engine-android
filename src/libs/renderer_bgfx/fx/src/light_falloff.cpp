#include "storm/fx/light_falloff.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{
namespace
{
// (1 - x⁴)² over 0..1: the window both the game's lamps had on baked surfaces and the new falloff end with.
float Window(float x)
{
    if (x >= 1.0f)
        return 0.0f;
    const float x2 = x * x;
    const float w = 1.0f - x2 * x2;
    return w * w;
}
} // namespace

LightShape ShapeOfGameLight(float range, float att0, float att1, float att2, const LightFalloff &falloff)
{
    if (!(range > 0.0f) || (att0 <= 0.0f && att1 <= 0.0f && att2 <= 0.0f))
        return {};
    const auto denominator = [&](float d) { return std::max(att0 + att1 * d + att2 * d * d, 1e-3f); };
    // Where the game's light falls to the floor: att0 + att1 d + att2 d² = 1 / floor.
    const float limit = std::min(range, kGameLightReach);
    const float target = 1.0f / kGameLightFloor;
    float reach = limit;
    if (denominator(limit) > target)
    {
        float lo = 0.0f, hi = limit; // the denominator only grows with the distance
        for (int i = 0; i < 24; ++i)
        {
            const float mid = 0.5f * (lo + hi);
            (denominator(mid) > target ? hi : lo) = mid;
        }
        reach = hi;
    }
    reach = std::max(reach, kGameLightNear * 2.0f);
    // The game's light in linear light, windowed at its reach as the relight had it.
    const auto game = [&](float d) {
        const float g = Window(d / reach) / denominator(std::max(d, kGameLightNear));
        return g * g;
    };
    LightShape shape;
    shape.range = std::max(falloff.reach, 1.0f) * reach;
    const float level = std::max(falloff.level, 0.0f);
    const float match = kMatchShare * reach;
    const float window = Window(match / shape.range);
    const float at_match = game(match);
    // The core: no nearer light than `level` times the game's next to the flame: I / core² <= level game(near), with
    // I = level game(match) (match² + core²) / window.
    const float k = game(kGameLightNear) * window / std::max(at_match, 1e-12f);
    float core = k > 1.0f ? match / std::sqrt(k - 1.0f) : shape.range; // (a game light as strong near as in the middle)
    core = std::clamp(std::max(core, falloff.core), 0.01f, shape.range);
    shape.core = core;
    shape.intensity = level * at_match * (match * match + core * core) / std::max(window, 1e-6f);
    return shape;
}

float LightFalloffAt(const LightShape &shape, float distance)
{
    if (!(distance < shape.range))
        return 0.0f;
    return shape.intensity * Window(distance / shape.range) / (distance * distance + shape.core * shape.core);
}

float LightFalloffGammaAt(const LightShape &shape, float distance)
{
    return std::sqrt(LightFalloffAt(shape, distance));
}

} // namespace storm::fx
