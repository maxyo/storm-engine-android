#pragma once

#include "storm_glm.h"
#include "iv_buffer_manager.h"
#include "t_flotsam.h"
#include "t_sink_splash.h"
#include "dx9render.h"
#include "sea_base.h"

///////////////////////////////////////////////////////////////////
// CLASS DEFINITION
///////////////////////////////////////////////////////////////////
class TSink
{
  public:
    TSink();
    virtual ~TSink();

    void Initialize(INIFILE *_ini, SEA_BASE *sea, VDX9RENDER *_renderer);
    void Release();
    void Start(const glm::vec3 &_pos, float _radius);
    void Process(uint32_t dTime);
    void Realize(uint32_t dTime);
    bool Enabled();

  private:
    bool enabled;

    VDX9RENDER *renderer;
    SEA_BASE *sea;
    int32_t texture;
    IVBufferManager *ivManager;
    uint32_t time;

    TSinkSplash splashes[sink_effect::MAX_SPLASHES];
    int32_t times[sink_effect::MAX_SPLASHES];
    int32_t ivIndexes[sink_effect::MAX_SPLASHES];
    int32_t flotsamTimes[sink_effect::MAX_FLOTSAMS];
    TFlotsam flotsams[sink_effect::MAX_FLOTSAMS];
    float distortDivider;
    float radius;
    glm::vec3 center;
};
