#pragma once

//#include <windows.h>
#include "storm_glm.h"

#define ATTRACT_FACTOR 0.9f // 0.01f
#define DEFLECT_FACTOR 10.5f
#define MOVEMENT_FACTOR 0.012f // 0.015f

//--------------------------------------------------------------------
class TDynamicObject
{
  public:
    TDynamicObject();
    virtual ~TDynamicObject();
    void Initialize(const glm::vec3 &_center, float _radius);
    void Calculate(TDynamicObject **a, int aCount, TDynamicObject **d, int dCount, float _k = 1.0f);
    void SetXYZ(const glm::vec3 &_pos);

    float GetAngle()
    {
        return (float)atan2(velocity.z, velocity.x);
    }

    glm::vec3 GetDirection()
    {
        return velocity;
    }

    glm::vec3 GetXYZ()
    {
        return pos;
    }

    // void Draw(HDC _dc, DWORD _color);
  private:
    glm::vec3 pos, velocity;
    // float ang;
};

//--------------------------------------------------------------------
