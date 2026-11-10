#pragma once

#include "weather_base.h"
#include "sea_base.h"
#include "ship_base.h"
#include "typedef.h"
#include <string>
#include <vector>

#define NUM_SEA_DROPS 500
#define D3DRAINVERTEX_FORMAT (D3DFVF_XYZ | D3DFVF_DIFFUSE)
#define D3DSEADROPVERTEX_FORMAT (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1)

using RAINVERTEX = struct
{
    glm::vec3 vPos;
    uint32_t dwColor;
};

struct SEADROPVERTEX
{
    glm::vec3 vPos;
    uint32_t dwColor;
    float tu, tv;
};

using rainblock_t = struct
{
    float fWindFlaw, fWindSpeedJitter;
    uint32_t dwTime;
    glm::vec3 vPos;
    glm::vec3 vAng;
};

class RAIN : public Entity
{
    struct ship_t
    {
        entid_t eid;
        SHIP_BASE *pShip;
    };

    struct seadrop_t
    {
        glm::vec3 vPos;
        float fTime;
        float fLifeTime;
    };

    struct drop_t
    {
        glm::vec3 vPos;
        float fLifeTime;
        int32_t iShip;
        // SHIP_BASE * pShip;
    };

  private:
    int32_t iRainDropsTexture;
    float fDropsDeltaTime;
    std::vector<RS_RECT> aRects;
    std::vector<drop_t> aDrops;
    std::vector<seadrop_t> aSeaDrops;
    std::vector<ship_t> aShips;

    bool bShow;

    WEATHER_BASE *pWeather;

    uint32_t dwRainMaxBlend, dwRainColor, dwRainR, dwRainG, dwRainB;
    float fDropLength, fRainWindSpeed, fRainSpeed, fWindPower, fWindAngle, fRainJitter, fRainWindSpeedJitter;

    bool bRainbowEnable;
    int32_t iRainbowTex;
    std::string sRainbowTexture;

    uint32_t dwRainTimeBlend;
    uint32_t dwNumRainBlocks;
    rainblock_t *pRainBlocks;

    glm::vec3 vCamPos, vCamAng;

    float fRainHeight, fRainRadius;
    uint32_t dwNumDrops;

    int32_t iVertexBuffer;

    uint32_t dwDropsColor;
    uint32_t dwDropsNearNum, dwDropsFarNum;
    float fDropsNearRadius, fDropsFarRadius;
    float fDropsLifeTime;
    float fDropsSize;
    std::string sDropsTexture, sSeaDropsTexture;

    int32_t iSeaDropTex;
    int32_t iIBSeaDrops;
    int32_t iVBSeaDrops;

    VDX9RENDER *rs;
    COLLIDE *cs;

    void GenerateRandomDrop(glm::vec3 *vPos) const;
    void GenerateRain();
    void InitialSomeBlockParameters(int32_t iIdx) const;
    void Release();
    void RealizeDrops(uint32_t Delta_Time);

  public:
    RAIN();
    ~RAIN() override;

    void SetDevice();
    bool Init() override;
    uint32_t NumDrops() const { return dwNumDrops; }
    void Realize(uint32_t Delta_Time);
    void Execute(uint32_t Delta_Time);
    bool CreateState(ENTITY_STATE_GEN *state_gen);
    bool LoadState(ENTITY_STATE *state);
    uint64_t ProcessMessage(MESSAGE &message) override;
    uint32_t AttributeChanged(ATTRIBUTES *pAttribute) override;

    void ProcessStage(Stage stage, uint32_t delta) override
    {
        switch (stage)
        {
        case Stage::execute:
            Execute(delta);
            break;
        case Stage::realize:
            Realize(delta);
            break;
            /*case Stage::lost_render:
              LostRender(delta); break;
            case Stage::restore_render:
              RestoreRender(delta); break;*/
        }
    }
};
