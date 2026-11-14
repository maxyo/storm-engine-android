// The CPU side of the parallax probe (fx_pom_probe, effects/parallax_probe.hpp): which relief texture is under the
// screen centre, for the owner's tuning of the parallax per texture.
//
// The renderer finds it exactly on the GPU: while the probe is on, every relief draw into the game picture whose
// bounding sphere lies on the centre ray (OnCentreRay) is drawn a second time, after the whole scene, with the depth
// test EQUAL against the scene's final depth, no colour or depth write, a one-pixel scissor at the centre and an
// occlusion query. The draw that wrote the centre pixel's depth passes; one hidden there (a table behind the wall it is
// seen through, a wall behind a lamp post, the ground under the cobbles' decal) fails, alpha-tested holes (fences)
// discard as in the scene. The queries come back a few frames later (PickProbeTarget).
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace storm::fx
{

// Whether a sphere (`center`, `radius`, in the space `view` maps from) can cover the centre pixel of a perspective
// picture: it reaches the ray through the picture's centre, in front of the camera. D3D matrices, row vectors
// (16 floats row by row); an off-centre projection's centre ray follows its _31 / _32.
bool OnCentreRay(const float view[16], const float proj[16], const float center[3], float radius);

// One frame's probe draws, in submission order: their queries' samples (kProbeNoResult while a query has not come back).
inline constexpr int32_t kProbeNoResult = -1;
struct ProbePick
{
    bool conclusive = false; // every query of the frame came back
    int index = -1;          // the draw under the centre: the last one that passed (a decal over its wall); -1 none
};
ProbePick PickProbeTarget(std::span<const int32_t> samples);

// The view order that runs view `probe` right after view `last` (probe < last): the ids for the positions
// probe .. last, as bgfx::setViewOrder(probe, last - probe + 1, order) takes them after an identity order.
std::vector<uint16_t> ProbeViewOrder(uint16_t probe, uint16_t last);

// An edge-triggered action from a settings value (fx_pom_toggle): every applied value that differs from the previous
// one fires, except the first value seen (the value a start or a re-application restores, not a change by the
// owner). Settings reach the engine as stored values, not as events: the menu stores a new value and «Готово» applies
// the whole set, so a change of the value is the press.
class EdgeTrigger
{
  public:
    bool Apply(int value)
    {
        const bool fired = seen_ && value != value_;
        seen_ = true;
        value_ = value;
        return fired;
    }

  private:
    bool seen_ = false;
    int value_ = 0;
};

} // namespace storm::fx
