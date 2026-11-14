#pragma once

#include "ai_flow_graph.h"
#include "island_base.h"
#include "collide.h"
#include "renderer.h"
#include "model.h"
#include "sea_base.h"
#include "vma.hpp"

enum FOAMTYPE
{
    FOAM_TYPE_1 = 1,
    FOAM_TYPE_2 = 2,

    FOAM_MODE_FORCEDWORD = 0x7FFFFFFF
};

class CoastFoam : public Entity
{
  public:
    CoastFoam();
    ~CoastFoam();

    bool Init();
    void Realize(uint32_t Delta_Time);
    void Execute(uint32_t Delta_Time);
    uint32_t AttributeChanged(ATTRIBUTES *pA);

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

  private:
    struct FoamVertex
    {
        glm::vec3 vPos;
        uint32_t dwColor;
        float tu, tv;
    };

    struct FoamPart
    {
        glm::vec3 v[2];
    };

    struct SubPart
    {
        // float tv;
        float fPos;
        float fSpeed;
        // float fA;
    };

    struct WorkPart
    {
        SubPart p[8];
        glm::vec3 v[2];
        float tu;
        // FOAMMODE    Mode;
    };

    struct Foam
    {
        Foam()
        {
            iTexture = -1;
        }

        std::vector<FoamPart> aFoamParts;
        std::vector<WorkPart> aWorkParts;
        float fMove[2];
        float fSX[2];
        float fSpeed[2], fBraking[2];
        int32_t iMode[2];
        int32_t iTexture;
        std::string sTexture;
        int32_t iNumFoams;

        FOAMTYPE Type;

        float fAlphaColor[2], fAlpha[2], fAlphaMin, fAlphaMax;
        float fSpeedMin, fSpeedMax;
        float fBrakingMin, fBrakingMax;
        float fAppearMin, fAppearMax;
        float fTexScaleX;
    };

    int32_t iEditFoam, iEditFoamPart, iEditFoamVertex;
    bool bEditMode, bMoved, bCanEdit;
    float fCursorPosX, fCursorPosY;
    int32_t iFoamTexture, iCursorTex;
    std::vector<Foam *> aFoams;
    int32_t iVBuffer, iIBuffer;
    // The strands of one texture that follow each other, drawn together (FlushStrands): one draw drew one strand
    std::vector<FoamVertex> aBatchVertices;
    std::vector<uint16_t> aBatchIndices;
    int32_t iBatchTexture = -1;
    PLANE *pFrustumPlanes;

    float fMaxFoamDistance, fFoamDeltaY;
    int32_t iFoamDivides;

    uint32_t dwNumPenasExecuted;

    VRenderer *rs;

    void ExtractRay(const D3DVIEWPORT9 &viewport, float fCursorX, float fCursorY, glm::vec3 &raystart, glm::vec3 &rayend);

    bool IsClipped(Foam *pF);

    void Load();
    void clear();

    void InitNewFoam(Foam *pF);
    void RecalculateFoam(int32_t iFoam);
    void ExecuteFoamType2(Foam *pF, float fDeltaTime, SEA_BASE &Sea);
    void ExecuteFoamType1(Foam *pF, float fDeltaTime, SEA_BASE &Sea);
    // Room in the batch for a strand of `vertices` vertices and `indices` indices drawn with `texture`: the batch is
    // drawn first when the texture differs or the buffers would overflow; base is the strand's first vertex.
    void AddStrand(int32_t texture, uint32_t vertices, uint32_t indices, FoamVertex *&pFV, uint16_t *&pI, uint16_t &base);
    void FlushStrands();
};
