#include "storm/fx/wetness.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{

void Wetness::Update(float rain, float seconds)
{
    const float target = std::clamp(rain, 0.0f, 1.0f);
    if (fresh_)
    {
        level_ = target;
        fresh_ = false;
        return;
    }
    const float dt = std::max(seconds, 0.0f);
    if (level_ < target)
        level_ = std::min(level_ + dt / kWetRiseSeconds, target);
    else
        level_ = std::max(level_ - dt / kWetDrySeconds, target);
}

} // namespace storm::fx
