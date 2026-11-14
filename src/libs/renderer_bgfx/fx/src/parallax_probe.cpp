#include "storm/fx/parallax_probe.hpp"

#include <cmath>

namespace storm::fx
{

bool OnCentreRay(const float view[16], const float proj[16], const float center[3], float radius)
{
    // The centre in view space (row vectors: v = c * V).
    float v[3];
    for (int k = 0; k < 3; ++k)
        v[k] = center[0] * view[0 * 4 + k] + center[1] * view[1 * 4 + k] + center[2] * view[2 * 4 + k] + view[3 * 4 + k];
    // The centre pixel: clip x = y = 0, i.e. x * _11 + z * _31 = 0 and y * _22 + z * _32 = 0.
    if (proj[0] == 0.0f || proj[5] == 0.0f)
        return false;
    float d[3] = {-proj[2 * 4 + 0] / proj[0], -proj[2 * 4 + 1] / proj[5], 1.0f};
    const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (float &c : d)
        c /= length;
    const float along = v[0] * d[0] + v[1] * d[1] + v[2] * d[2];
    if (along + radius <= 0.0f)
        return false; // behind the camera
    float off2 = 0.0f;
    for (int k = 0; k < 3; ++k)
    {
        const float o = v[k] - along * d[k];
        off2 += o * o;
    }
    return off2 <= radius * radius;
}

ProbePick PickProbeTarget(std::span<const int32_t> samples)
{
    ProbePick pick;
    for (size_t i = 0; i < samples.size(); ++i)
    {
        if (samples[i] == kProbeNoResult)
            return {};
        if (samples[i] > 0)
            pick.index = static_cast<int>(i);
    }
    pick.conclusive = true;
    return pick;
}

std::vector<uint16_t> ProbeViewOrder(uint16_t probe, uint16_t last)
{
    std::vector<uint16_t> order;
    if (last <= probe)
        return order;
    for (uint16_t id = probe + 1; id <= last; ++id)
        order.push_back(id);
    order.push_back(probe);
    return order;
}

} // namespace storm::fx
