#pragma once

#include "collide.h"
#include "renderer.h"
#include "vma.hpp"
#include <vector>

#define MAX_BLOOD_TRIANGLES 10000
#define ON_LIVETIME_BLOOD_TRIANGLES (MAX_BLOOD_TRIANGLES * 3 / 4)
#define MAX_CLIPPING_TRIANGLES 64

class Blood : public Entity
{
    struct BloodVertex
    {
        glm::vec3 pos;
        uint32_t dwCol;
        float u, v;
    };

    struct BloodTriangle
    {
        BloodVertex v[3];
    };

    struct BloodInfo
    {
        int32_t nStartIdx;
        int32_t nIdxQ;
        float fLiveTime;
        glm::vec3 cpos;
    };

    struct ClipTriangle
    {
        glm::vec3 v[3];
    };

    // --------------------------------------------------------------------------------------------
    // Construction, destruction
    // --------------------------------------------------------------------------------------------
  public:
    Blood();
    virtual ~Blood();

    // Initialization
    bool Init() override;
    // Work
    void Execute(uint32_t delta_time);
    void Realize(uint32_t delta_time);
    //
    uint64_t ProcessMessage(MESSAGE &message) override;

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

    static bool AddClipPoligon(const glm::vec3 *v, int32_t nv);

  protected:
    VRenderer *pRS;
    COLLIDE *pCol;
    int32_t texID;

    BloodTriangle pvBloodT[MAX_BLOOD_TRIANGLES];
    int32_t nStartT;
    int32_t nUsedTQ;

    std::vector<BloodInfo> aBlood;

    std::vector<entid_t> aModels;

    static ClipTriangle clipT[MAX_CLIPPING_TRIANGLES];
    static int32_t nClipTQ;
    static glm::vec3 normal;

    void AddBlood(const glm::vec3 &pos);
    void BuildBloodDataByCollision(const glm::vec3 &cpos);
    void SetVertexByPos(BloodVertex &v, const glm::vec3 &pos, const glm::vec3 &vc, float fU0, float fV0);
    int32_t CheckBloodQuantityInRadius(const glm::vec3 &cpos, float fDist, int32_t nLimitQ);
};
