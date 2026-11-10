#pragma once

#include "storm_glm.h"
#include <entity.h>

class VPARTICLE_SYSTEM
{
  public:
    VPARTICLE_SYSTEM(){};

    virtual ~VPARTICLE_SYSTEM(){};
    virtual void Stop() = 0;
    virtual void SetEmitter(glm::vec3 p, glm::vec3 a) = 0;
    virtual void LinkToObject(entid_t id, glm::vec3 _LinkPos) = 0;
    virtual void SetDelay(int32_t _delay) = 0;
    virtual void SetLifeTime(uint32_t time) = 0;

    virtual void StopEmitter(){};

    virtual void Pause(bool _bPause)
    {
    }
};
