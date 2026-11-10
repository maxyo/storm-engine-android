#pragma once

#include "cannon_trace.h"

class SEA_BASE : public CANNON_TRACE_BASE
{
  public:
    virtual float WaveXZ(float x, float z, glm::vec3 *vNormal = nullptr) = 0;
};
