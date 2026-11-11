#pragma once

#include "storm_glm.h"
#include "v_sound_service.h"
#include "renderer.h"
#include "sea_base.h"
#include "seps.h"

///////////////////////////////////////////////////////////////////
// CLASS DEFINITION
///////////////////////////////////////////////////////////////////
struct SINK_VERTEX
{
    glm::vec3 pos;
    uint32_t color;
    float tu, tv;
};

class TSinkSplash
{
  public:
    TSinkSplash();
    virtual ~TSinkSplash();

    void Initialize(INIFILE *_ini, SEA_BASE *sea);
    void Release();
    void Start(const glm::vec3 &_pos, uint16_t *_indexes, SINK_VERTEX *_vertexes, int32_t vOffset);
    void Process(uint32_t dTime, uint16_t *_indexes, SINK_VERTEX *_vertexes);
    bool Enabled();
    void Reset(uint16_t *_indexes, SINK_VERTEX *_vertexes);
    void AdditionalRealize(uint32_t dTime);

  private:
    float HeightF(uint32_t time, float _r, float _k);

    SEPS_PS *ps;
    bool enabled;
    SEA_BASE *sea;
    uint32_t time;

    float distortDivider;
    glm::vec3 center, dir;
    float growK;
};
