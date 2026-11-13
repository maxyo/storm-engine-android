#pragma once

#include "cannon_trace.h"
#include "storm/render/effects.hpp"

class SEA_BASE : public CANNON_TRACE_BASE
{
  public:
    virtual float WaveXZ(float x, float z, glm::vec3 *vNormal = nullptr) = 0;
    // Something on or in the water this frame (a hull, a swimmer, a wader): the sea hands this frame's sources to
    // the renderer's waves from objects when it draws (storm::render::SetEffectsSeaFrame). Execute stage.
    virtual void AddWaveSource(const storm::render::EffectsWaveSource &source) = 0;
};
