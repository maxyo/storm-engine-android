// The waterline of a ship's hull for the waves from objects (storm::render::EffectsWaveSource): the hull's half beam
// on each side as a function of the height and the position along it, traced once per hull model through its
// collision BSP and shared by every ship of that model; then, each frame, the waterline where that hull meets the
// sea (its draft, heel and pitch included) as the stations the wave field and the Kelvin wake take.
#pragma once

#include "storm/render/effects.hpp"

#include "storm_glm.h"

#include <memory>
#include <string>
#include <vector>

class GEOS;
class SEA_BASE;

namespace storm::ship
{

class HullProfile
{
  public:
    static constexpr int kAlong = 32; // sections along the hull (its local z)
    static constexpr int kUp = 16;    // heights per section (its local y)

    // The profile of `geo` (the hull's geometry, node 0 of the ship model), traced once per `model` name and kept for
    // the process. nullptr (logged once per model) when the geometry has no collision BSP or the traces find no hull.
    static std::shared_ptr<const HullProfile> Get(const std::string &model, GEOS &geo);

    // Half beam (local metres, >= 0) on the port (+x, side 0) or starboard (-x, side 1) side at local z, y.
    float HalfBeam(int side, float z, float y) const;
    float z0 = 0.0f, z1 = 0.0f, y0 = 0.0f, y1 = 0.0f;

  private:
    std::vector<float> beam_[2]; // [along][up]
};

// The waterline of `profile` this frame for the hull whose geometry has the world matrix `world` (absolute world,
// including the ship's rocking), as seen from `origin` with the heading `forward` (`source` gets stern, stem, the
// stations, half_length and half_width). The sea's height comes from `sea`. False when the hull is out of the water
// (no waterline found).
bool HullWaterline(const HullProfile &profile, const glm::mat4 &world, const glm::vec3 &origin,
                   const glm::vec2 &forward, SEA_BASE &sea, render::EffectsWaveSource &source);

} // namespace storm::ship
