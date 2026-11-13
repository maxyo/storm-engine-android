#pragma once

#include <array>
#include <vector>

#include "collide.h"
#include "d_timer.h"
#include "renderer.h"
#include "fire_place.h"
#include "geometry.h"
#include "island_base.h"
#include "model.h"
#include "save_load.h"
#include "sea_base.h"
#include "ship_base.h"
#include "ship_msg.h"
#include "hull_waterline.h"

#include <memory>

#define DELTA_TIME(x) ((x)*0.001f)
#define DELTA_TIME_ROTATE(x) ((x)*1.0f / 10.0f)

#define MAX_STRENGTH 16
#define STRENGTH_MAIN 0
#define RESERVED_STRENGTH 1

#define MAX_KEEL_POINTS 5

#define MAST_IDENTIFY "mast"
#define MAST_FIRST 1
#define TOPMAST_BEGIN 100 // start of topmast numbering (if any)
#define HULL_IDENTIFY "shatter"

class SHIP : public SHIP_BASE
{
  protected:
    // struct section
    struct mast_t
    {
        NODE *pNode;        // node pointer in model
        glm::vec3 vSrc, vDst; // src and dest vectors
        int32_t iMastNum;   // mast number
        bool bBroken;       // if mast is broken then pNode = 0
        float fDamage;
    };

    struct hull_t
    {
        NODE *pNode;        // node pointer in model
        glm::vec3 vSrc, vDst; // src and dest vectors
        int32_t iHullNum;   // hull detail number
        bool bBroken;       // if hull detail is broken then pNode = 0
        float fDamage;
    };

    struct ship_point_t
    {
        float fSpeed;
        float fGravity; // acceleration
        float fExpulsiveForce;
        bool bUnderWater;

        float fY;
        float fA;
        float fV;
    };

    // init parameters
    inline static layer_index_t RealizeLayer, ExecuteLayer;
    char cShipIniName[256];
    int32_t iShipPriorityExecute, iShipPriorityRealize;
    float fGravity;

    float fRockingY, fRockingAZ;

    // entid_t section
    entid_t model_id, sphere[36];
    entid_t sail_id, rope_id, flag_id, cannon_id, vant_id, vantl_id, vantz_id, touch_id, sea_id, blots_id;

    static VRenderer *pRS;
    static SEA_BASE *pSea;
    static ISLAND_BASE *pIsland;
    static COLLIDE *pCollide;
    static VGEOMETRY *pGS;

    glm::mat4 mRoot;
    glm::vec3 vSpeed, vSpeedsA;
    float fMinusVolume;
    float fXOffset, fZOffset;

    bool bModelUpperShip;
    MODEL *pModelUpperShip;
    float fUpperShipAY, fUpperShipY;

    // Ships lights
    entid_t shipLights;
    entid_t flagEntity_{};

    // Fire places
    std::vector<FirePlace> aFirePlaces;

    // Sound section

    // temporary used
    bool bMassaShow;
    int32_t uniIDX;
    bool bUse;
    std::array<std::array<ship_point_t, 16>, 16> ShipPoints;

    // executed parameters
    glm::vec3 vSpeedAccel;
    ship_t SP;
    glm::vec3 vPos, vAng;
    float fSailState;
    float fWaterLine;
    bool bDead, bVisible;
    glm::vec3 vDeadDir, vCurDeadDir;
    glm::vec3 vKeelContour[MAX_KEEL_POINTS];
    int32_t iNumMasts;
    int32_t iNumHulls;
    std::vector<mast_t> pMasts;
    std::vector<hull_t> pHulls;
    bool bShip2Strand;
    bool bMounted;
    bool bKeelContour;
    glm::vec3 vOldAng, vOldPos;
    // Waves from objects: the last position and the smoothed speed / heave (AddWaveSource).
    glm::vec3 vWaveLastPos{};
    bool bWaveLastPos = false;
    float fWaveSpeed = 0.0f, fWaveHeave = 0.0f;
    std::shared_ptr<const storm::ship::HullProfile> hullProfile_; // the hull's waterline (shared per model)
    void AddWaveSource(float fDeltaTime);
    bool bSetFixed;
    float fFixedSpeed;

    // fast turn perk parameters
    bool bPerkTurnActive;
    float fInitialPerkAngle, fResultPerkAngle;

    std::array<STRENGTH, MAX_STRENGTH> Strength;

    DTimer dtMastTrace, dtUpdateParameters;

    // executed functions
    glm::vec3 ShipRocking(float fDeltaTime);
    BOOL ApplyStrength(float dtime, BOOL bCollision);
    BOOL CalculateNewSpeedVector(glm::vec3 *Speed, glm::vec3 *Rotate);
    void CalculateImmersion(); //
    void CheckShip2Strand(float fDeltaTime);
    void MastFall(mast_t *pM);
    void HullFall(hull_t *pM);
    void FakeFire(const char *sBort, float fRandTime);

    glm::mat4 UpdateModelMatrix();
    void RecalculateWorldOffset();

    // init section
    void ScanShipForFirePlaces();
    void LoadPositionFromAttributes();
    BOOL LoadShipParameters();
    void CalcRealBoxsize();

    void SetDead() override;

    bool isDead() override
    {
        return bDead;
    };

    bool isVisible()
    {
        return bVisible;
    };

    void InitSailState();

    // The character that fired a ball, by the index the ball carries; throws if there is none.
    static ATTRIBUTES *BallOwnerCharacter(int32_t iBallOwner);

  public:
    ~SHIP() override;
    SHIP();

    float GetMaxSpeedZ() override;
    float GetMaxSpeedY() override;
    float GetWindAgainst() override;
    int32_t AddStrength(STRENGTH *strength) override;
    bool DelStrength(int32_t iIdx) override;

    BOOL BuildContour(glm::vec3 *vContour, int32_t &iNumVContour) override;
    bool BuildMasts();
    bool BuildHulls();
    BOOL Move(uint32_t DeltaTime, BOOL bCollision);
    BOOL TouchMove(uint32_t DeltaTime, TOUCH_PARAMS *pTPOld, TOUCH_PARAMS *pTPNew) override;

    void LoadServices();

    // inherit functions SHIP_BASE
    bool bSetLightAndFog;
    uint32_t dwSaveAmbient, dwSaveFogColor;
    D3DLIGHT9 saveLight;

    void SetLightAndFog(bool bSetLight) override;
    void RestoreLightAndFog() override;

    void SetSailState(float fSpeed) override;
    float GetSailState() override;

    void SetRotate(float fRotSpeed) override;
    float GetRotate() override;

    float GetBrakingDistance(float *pfTime = nullptr) override;
    float GetRotationAngle(float *pfTime = nullptr) override;

    float GetCurrentSpeed() override;

    void SetLights() override;
    void UnSetLights() override;
    void Fire(const glm::vec3 &vPos) override;

    void SetFixedSpeed(bool bSetFixed, float fFixedSpeed) override;

    // inherit functions COLLISION_OBJECT
    float Trace(const glm::vec3 &src, const glm::vec3 &dst) override;

    bool Clip(const PLANE *planes, int32_t nplanes, const glm::vec3 &center, float radius,
              ADD_POLYGON_FUNC addpoly) override
    {
        return false;
    };

    const char *GetCollideMaterialName() override
    {
        return nullptr;
    };

    bool GetCollideTriangle(TRIANGLE &triangle) override
    {
        return false;
    };

    // inherit functions CANNON_TRACE_BASE
    float Cannon_Trace(int32_t iBallOwner, const glm::vec3 &src, const glm::vec3 &dst) override;

    // inherit functions VAI_OBJBASE
    void SetACharacter(ATTRIBUTES *pAP) override;

    glm::mat4 *GetMatrix() override;
    void SetMatrix(glm::mat4 &mtx);
    MODEL *GetModel() const override;
    entid_t GetModelEID() const override;
    glm::vec3 GetPos() const override;
    glm::vec3 GetAng() const override;
    glm::vec3 GetBoxsize() const override;
    glm::vec3 GetRealBoxsize() const override;

    void SetPos(const glm::vec3 &vNewPos) override;

    bool Mount(ATTRIBUTES *) override;

    // inherit functions Entity
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

    void Save(CSaveLoad *pSL) override;
    void Load(CSaveLoad *pSL) override;
};
