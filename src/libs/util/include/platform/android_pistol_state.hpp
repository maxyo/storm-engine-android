#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace storm::android
{
// Observations only: never run the game's IsFire event to draw a control.
// Loading is expressed in shots, so a multi-shot weapon can fire before it is
// fully reloaded. Unknown/custom script templates retain their original input.
struct PistolState
{
    std::string state = "unknown";
    int progress = 0;
    bool operator==(const PistolState &) const = default;
};
inline PistolState pistolState(bool equipped, bool playerTemplate,
                              std::optional<double> charge, std::optional<double> charging)
{
    if (!equipped) return {"no_weapon", 0};
    if (!playerTemplate || !charge || !charging || !std::isfinite(*charge) ||
        !std::isfinite(*charging) || *charge < 0) return {};
    if (*charge >= 1) return {"loaded", 20};
    return {"unloaded", static_cast<int>(std::clamp(std::floor(*charge * 20), 0.0, 19.0))};
}
}
