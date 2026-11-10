#pragma once

#include "base.h"

class PointEmitter : public BaseEmitter
{
  protected:
    // Get a position for emission of new particles
    glm::vec3 GetNewParticlePosition(float DeltaTime) override;

  public:
    // Constructor / destructor
    PointEmitter(ParticleSystem *pSystem);
    ~PointEmitter() override;
};
