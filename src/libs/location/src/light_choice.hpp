// Which of a location's light sources light a model: the `budget` that are strongest at the model's own position.
// The choice depends only on that position and the sources (never on the camera or on time). When the model or a
// source moves, a source enters the set exactly when it has become as strong there as the one it replaces, so the
// amount of light does not jump.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace storm::location
{

// D3D fixed-function point light: brightness `luminance / (att0 + att1 d + att2 d²)` within `range`, zero beyond.
inline float LightStrength(float luminance, float att0, float att1, float att2, float range, float distance)
{
    if (distance > range)
        return 0.0f;
    return luminance / std::max(att0 + att1 * distance + att2 * distance * distance, 1e-6f);
}

// `strength_at(i)`: how strongly source i lights the model's position (LightStrength of its steady colour: a
// flicker must not reorder the sources). Returns the indices of the strongest `budget` sources, strongest first;
// equal strengths keep the list order; sources that do not reach the position (strength <= 0) are left out.
template <typename StrengthAt> std::vector<uint32_t> ChooseLights(size_t count, StrengthAt &&strength_at, size_t budget)
{
    std::vector<std::pair<float, uint32_t>> order;
    order.reserve(count);
    for (size_t i = 0; i < count; i++)
        if (const float strength = strength_at(i); strength > 0.0f)
            order.emplace_back(-strength, static_cast<uint32_t>(i));
    const size_t chosen = std::min(order.size(), budget);
    std::partial_sort(order.begin(), order.begin() + chosen, order.end());
    std::vector<uint32_t> result;
    result.reserve(chosen);
    for (size_t i = 0; i < chosen; i++)
        result.push_back(order[i].second);
    return result;
}

} // namespace storm::location
