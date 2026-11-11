#pragma once

#include "storm_glm.h"
#include "renderer.h"
#include "sea_base.h"

//#include "../sound_service/v_sound_service.h"

class INIFILE;
///////////////////////////////////////////////////////////////////
// CLASS DEFINITION
///////////////////////////////////////////////////////////////////
struct GRID_VERTEX
{
    glm::vec3 pos;
    uint32_t color;
    float tu, tv;
};

struct GRID_VERTEX2
{
    glm::vec3 pos;
    uint32_t color;
    float tu, tv;
    float tu2, tv2;
};

class TSplash
{
  public:
    TSplash();
    virtual ~TSplash();

    void Initialize(INIFILE *_ini, SEA_BASE *sea, VRenderer *_renderer);
    void Release();
    void Start(const glm::vec3 &_pos, const glm::vec3 &_dir);
    bool Process(uint32_t dTime);
    bool Process2(uint32_t dTime);
    void PostProcess();
    void PostProcess2();
    void Realize(uint32_t dTime);
    void Realize2(uint32_t dTime);
    bool Enabled();

    static bool startRender;
    static uint64_t lockTicks, fillTicks, unlockTicks, realizeTicks;
    static uint32_t processCount, topIndex, topIndex2;

  private:
    float HeightF(uint32_t time, float _r, float _k);

    bool enabled;

    static VRenderer *renderer;
    SEA_BASE *sea;

    static int32_t vBuffer, iBuffer, texture, texture2, vBuffer2, iBuffer2;
    static int buffersUsage;
    uint32_t time;

    float width2;
    float distortDivider;
    glm::vec3 center, dir;
    float growK;
    float height, midY;
};
