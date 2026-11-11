#pragma once

#include "island_base.h"
#include "renderer.h"
#include "ship_base.h"
#include <vector>

#define D3DTLVERTEX_FORMAT (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

using D3DTLVERTEX = struct
{
    glm::vec3 vPos;
    float fRHW;
    uint32_t dwDiffuse;
};

#include "island_impact.hpp"

struct TOUCH_SHIP
{
    entid_t eID;                  // ship id
    glm::vec3 vContourTemp[2][128]; // temporary storage / must be deleted
    TOUCH_PARAMS TP[2];           // touch params    for collision
    SHIP_BASE *pShip;             // ship pointer
    glm::vec3 vKickPoint;           // temp
    glm::vec3 vKickAngle;           // temp
    glm::vec3 vContour[128];        // initial contour // must be dynamic
    glm::vec3 vContourRect[4];      // rect contour
    int32_t iNumVContour;            // num points in contour
    storm::touch::IslandContact islandContact; // damage bookkeeping of island contacts (island_impact.hpp)
};

class TOUCH : public Entity
{
  protected:
    VRenderer *pRS;
    ISLAND_BASE *pIslandBase;

    bool bUseTouch;

    float fScale;
    float fCollisionDepth;

    uint32_t dwDeltaTime;
    int32_t iDeltaTime;
    float fIslandIntoSpeed = 0.0f; // knots towards the island of the last island contact (IsIntersectShipsReal)

    TOUCH_SHIP *pShips[256];
    int32_t iNumShips;

    BOOL BuildContour(int32_t ship_idx);
    BOOL IsPointInContour(glm::vec3 *vP, glm::vec3 *vContour, int32_t numvcontour);

    bool IsSinked(int32_t iIndex);

    float Touch(int32_t idx, int32_t skip_idx, glm::vec3 *vPos, glm::vec3 *vAng, float fPower, float fSlide);
    BOOL FakeTouch();
    BOOL IsIntersectShipsRects(int32_t idx1, int32_t idx2);
    BOOL IsIntersectShipsReal(int32_t idx1, int32_t cidx, glm::vec3 *vPos, glm::vec3 *vAng, glm::vec3 *vRecoil, float *fPower,
                              float *fSlide);
    void GetLineABC(glm::vec3 &v1, glm::vec3 &v2, float &A, float &B, float &C);
    glm::vec3 GetLineIntersectPoint(glm::vec3 &v1, glm::vec3 &v2, glm::vec3 &o1, glm::vec3 &o2);
    int32_t ProcessImpulse(int32_t iOurIdx, glm::vec3 vPos, glm::vec3 vDir, float fPowerApplied);
    int32_t GetTouchPoint(int32_t iIdx, const glm::vec3 &vPos);

    void DrawLine(std::vector<RS_LINE2D> &aLines, float x1, float y1, float x2, float y2, uint32_t color);
    void DrawShips();
    glm::vec3 GetPoint(float x, float y, float xx, float yy, float xscale, float yscale, float fCos, float fSin,
                     POINT ss);

    void SetDevices();

  public:
    ~TOUCH() override;
    TOUCH();
    void LoadServices();
    bool Init() override;
    void Realize(uint32_t Delta_Time);
    void Execute(uint32_t Delta_Time);
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
