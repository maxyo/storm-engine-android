#pragma once

#include "cannon_trace.h"

#include <cstdint>
#include <vector>

struct FRECT;

// The island's depth map as land and water: `size` × `size` cells, cell (i, j) covers x from
// x0 + i * stepX and z from z0 + j * stepZ; land[j * size + i] is 1 where the island stands
// above the water (or overhangs it), 0 over water. Read by the minimap (battle navigator).
struct IslandLandMap
{
    uint32_t size = 0;
    float x0 = 0.f, z0 = 0.f, stepX = 0.f, stepZ = 0.f;
    std::vector<uint8_t> land;
};

class ISLAND_BASE : public CANNON_TRACE_BASE
{
  public:
    // False while the island has no depth map (no sea island mounted).
    virtual bool GetLandMap(IslandLandMap &out) = 0;
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
