// ==================================================================== ||
// Ship tie camera                                                      ||
// ==================================================================== ||
#pragma once

#include "common_camera.h"
#include "dx9render.h"
#include "model.h"
#include "vma.hpp"

#define D3DFVF_VERTEX (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1)
#define AIM_SIZE 0.05f

class DECK_CAMERA : public COMMON_CAMERA
{
    struct VERTEX
    {
        D3DVECTOR v;
        D3DCOLOR color;
        float tu;
        float tv;
    };

    VERTEX tri[4];

    // parameters loaded from INI file //
    // --------------------------------------
    float fSensivityDistance, fSensivityHeightAngle, fSensivityAzimuthAngle;
    float fRockingX, fRockingZ;

    float MEN_STEP_UP;
    float MEN_STEP_MIN;
    float HEIGHT_MAX;
    float HEIGHT_MIN;
    float HEIGHT_STEP;
    float CAMERA_MAX_X, CAMERA_MIN_X;
    float DEFAULT_HEIGHT;
    //--------------------------------------

    glm::vec3 g_gv0, g_gv1, g_gv2;

    MODEL *pModel;
    VDX9RENDER *RenderService;
    glm::vec3 camera_pos, camera_ang;
    float h_eye;
    RECT Screen_Rect;
    int32_t lock_x;
    int32_t lock_y;
    int32_t vb_id;
    bool bLoad;
    NODE *pathNode;
    void SetStartPos();
    bool GetCrossXZ(glm::vec3 &spos, glm::vec3 &dv, glm::vec3 &p1, glm::vec3 &p2, glm::vec3 &res);
    bool FindPath();
    void SetViewPoint(glm::vec3 &cViewPoint);
    float MultiTrace(const glm::vec3 &cvBase, const glm::vec3 &cvDown, float fHBase);

  public:
    DECK_CAMERA();
    ~DECK_CAMERA() override;

    void SetCharacter(ATTRIBUTES *_pACharacter) override;

    void SetDevice();
    bool Init() override;
    void Move(uint32_t DeltaTime);
    void Realize(uint32_t Delta_Time);
    void Execute(uint32_t Delta_Time);
    bool CreateState(ENTITY_STATE_GEN *state_gen);
    bool LoadState(ENTITY_STATE *state);
    uint32_t AttributeChanged(ATTRIBUTES *pAttr) override;
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

    void Save(CSaveLoad *pSL) override;
    void Load(CSaveLoad *pSL) override;
};
