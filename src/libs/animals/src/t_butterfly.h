#pragma once

#include "storm_glm.h"
#include "iv_buffer_manager.h"
#include "collide.h"
#include "model.h"

///////////////////////////////////////////////////////////////////
// DEFINES & TYPES
///////////////////////////////////////////////////////////////////
#define MAX_ACTIVITY_TIME 20000
#define MAX_WAIT_TIME 20000
#define MAX_EFFECT_RADIUS 1.5f
#define VELOCITY 1.1f // [m/sec]
#define MIN_Y_DELTA 0.05f
#define MIN_ACTIVITY 0.4f
#define RISE_IMPROBABILITY 6 // improbability of back effect
#define FALL_IMPROBABILITY 10
#define MAX_DISPLACE_TIME 300
#define DISPLACE_SPEED 10.0f // [m/(sec^2)]
#define MAX_REMOTE_DISTANCE 10.0f
#define MAX_HEIGHT 4.0f
#define MODEL_SIDE 0.05f
#define WINGS_TIME_K 30.0f
#define Y_SPEED 1.0f
#define SINGLE_SIZE 0.25f

#define BUTTERFLY_VERTEX_TYPE (D3DFVF_XYZ | D3DFVF_TEX1 | D3DFVF_TEXTUREFORMAT2)

#pragma pack(push, 1)
struct tButterflyVertex
{
    glm::vec3 pos;
    float tu, tv;
};
#pragma pack(pop)
//--------------------------------------------------------------------
class TButterfly
{
  public:
    TButterfly();
    virtual ~TButterfly();

    void Initialize(const glm::vec3 &_center, float _radius, int32_t _bufferIndex, int _tI, int _tJ);
    void Calculate(int32_t _dTime, COLLIDE *_collide, entity_container_cref its);
    void Effect(const glm::vec3 &_position);

    static void SetCenter(const glm::vec3 &_center)
    {
        center = _center;
    };

    const glm::vec3 GetDirection()
    {
        return centerVelocity;
    }

    const glm::vec3 GetPosition()
    {
        return centerPosition;
    }

    void SetMinY(float _minY)
    {
        minY = _minY;
        maxY = minY + MAX_HEIGHT;
    }

    void Draw(HDC _dc);
    void Draw(IVBufferManager *_ivManager);
    void Draw(VDX9RENDER *_renderer, MODEL *_model);

  private:
    glm::vec3 centerPosition, centerVelocity, oldPos;
    glm::vec3 displaceVector;
    int32_t timeToNextDisplace;
    int32_t fullActiveTime, activeTime, waitTime;
    int32_t bufferIndex;
    bool active;

    static glm::vec3 center;
    float minY, maxY;
    bool firstDraw;
    float time;

    float tI, tJ;
};

//--------------------------------------------------------------------
