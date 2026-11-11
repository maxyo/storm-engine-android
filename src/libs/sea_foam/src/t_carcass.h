#pragma once

#include "storm_glm.h"
#include "storm_glm.h"
#include "renderer.h"
#include "geometry.h"

#define MAX_MEASURE_POINTS 10
#define MAX_LEVELS 20
#define CARCASS_VERTEX_FORMAT (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXTUREFORMAT2)

#define BOUND_UPPER(a, b)                                                                                              \
    if ((a) > (b))                                                                                                     \
        (a) = (b);

//--------------------------------------------------------------------
struct tCarcassVertex
{
    glm::vec3 pos;
    uint32_t color;
    float u, v;
};

struct tMeasure
{
    float deltaPointX[MAX_MEASURE_POINTS];
    float deltaPointY[MAX_MEASURE_POINTS];
    int pointsCount;
};

///////////////////////////////////////////////////////////////////
// CLASS DEFINITION
///////////////////////////////////////////////////////////////////
class TCarcass
{
  public:
    TCarcass(int _levelsCount, int _measurePointsCount, VRenderer *_renderer, bool _normalsInverted = false);
    virtual ~TCarcass();

    void InitCircleMeasure(float _d, float _kx, float _ky);
    bool Initialize();
    void Uninitialize();
    void Execute(uint32_t dTime, glm::mat4 &_mtx, const glm::vec3 *_starts);
    void Realize(const char *_technique);

    void SetSpeed(float _uSpeed, float _vSpeed, float _speedA);

  private:
    void RebuildLevels(tCarcassVertex *_vBuffer, bool _firstDraw, uint32_t dTime);
    void RebuildIndexes(uint16_t *_iBuffer);

    bool normalsInverted;
    tMeasure measure;
    glm::vec3 levelStarts[MAX_LEVELS];
    int levelsCount;
    glm::mat4 sceneMatrix;
    int32_t ivElementIndex;
    bool indexesCreated;
    uint32_t time;

    float uSpeed, vSpeed, speedA;
    VRenderer *renderer;
    int iBuffer, vBuffer;
};
