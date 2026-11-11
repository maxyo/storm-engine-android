#pragma once

#include "cannon_trace.h"

struct FRECT;

class ISLAND_BASE : public CANNON_TRACE_BASE
{
  public:
    virtual entid_t GetModelEID() = 0;

    // True when the box (centre, size; vSize.y = height above the water, 0 = no height check) is in water shallower
    // than fMinDepth or touches island geometry above the water (island_overhang.hpp).
    virtual bool Check2DBoxDepth(glm::vec3 vPos, glm::vec3 vSize, float fAngY, float fMinDepth) = 0;
    // Island geometry between the water and fHeight above it at (x, z) (island_overhang.hpp).
    virtual bool OverheadContact(float x, float z, float fHeight) = 0;
    virtual bool GetDepth(float x, float z, float *fRes = nullptr) = 0;
    virtual bool GetDepthFast(float x, float z, float *fRes = nullptr) = 0;
    virtual bool GetDepth(FRECT *pRect, float *fMinH, float *fMaxH) = 0;

    virtual bool GetShadow(float x, float z, float *fRes = nullptr) = 0;

    virtual bool GetMovePoint(glm::vec3 &vSrc, glm::vec3 &vDst, glm::vec3 &vRes) = 0;

    virtual entid_t GetSeabedEID() = 0;

    virtual float GetCurrentImmersion() = 0;
};
