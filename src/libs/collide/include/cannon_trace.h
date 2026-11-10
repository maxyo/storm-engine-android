#pragma once

#include "object.h"

class CANNON_TRACE_BASE : public COLLISION_OBJECT
{
  public:
    ~CANNON_TRACE_BASE() override = default;

    virtual float Cannon_Trace(int32_t iBallOwner, const glm::vec3 &src, const glm::vec3 &dst) = 0;
};
