#pragma once

#include "collide.h"
#include "dx9render.h"
#include "model.h"

#include <filesystem>

#define SR_MOVE 1
#define SR_STOPROTATE 2
#define SR_YROTATE 4

class MAST : public Entity
{
    struct MountData
    {
        NODE *pNode;
        entid_t shipEI;
        entid_t modelEI;
    } m_mount_param;

    bool bUse;
    bool bFallUnderWater;
    int wMoveCounter;
    VDX9RENDER *RenderService;
    COLLIDE *pCollide;
    bool bModel;
    entid_t model_id, oldmodel_id;
    entid_t ship_id;
    std::filesystem::file_time_type ft_old;
    NODE *m_pMastNode;

  public:
    MAST();
    ~MAST() override;
    void SetDevice();
    bool Init() override;
    void Move();
    void Execute(uint32_t Delta_Time);
    void Realize(uint32_t Delta_Time);

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

    bool CreateState(ENTITY_STATE_GEN *state_gen);
    bool LoadState(ENTITY_STATE *state);
    uint64_t ProcessMessage(MESSAGE &message) override;

  protected:
    void AllRelease();
    void Mount(entid_t modelEI, entid_t shipEI, NODE *mastNodePointer);
    void LoadIni();
    void doMove(uint32_t DeltaTime);
    int GetSlide(entid_t mod, glm::vec3 &pbeg, glm::vec3 &pend, glm::vec3 &dp, glm::vec3 &lrey, glm::vec3 &rrey, float &angl);

    struct MASTMOVE
    {
        glm::vec3 bp, ep;     // mast coordinates
        glm::vec3 brey, erey; // rhea coordinates

        glm::vec3 ang, dang, sdang;
        glm::vec3 mov, dmov, sdmov;
    };

    MASTMOVE mm;
};

class HULL : public Entity
{
    struct MountData
    {
        NODE *pNode;
        entid_t shipEI;
        entid_t modelEI;
    } m_mount_param;

    VDX9RENDER *RenderService;
    COLLIDE *pCollide;
    NODE *m_pHullNode;
    bool bModel;

    bool bUse;
    int wMoveCounter;

    entid_t model_id, oldmodel_id;
    entid_t ship_id;

  public:
    HULL();
    ~HULL() override;
    void SetDevice();
    bool Init() override;
    bool CreateState(ENTITY_STATE_GEN *state_gen);
    bool LoadState(ENTITY_STATE *state);
    uint64_t ProcessMessage(MESSAGE &message) override;

    void Execute(uint32_t Delta_Time);
    void Realize(uint32_t Delta_Time);

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
        }
    }

  protected:
    void AllRelease();
    void Mount(entid_t modelEI, entid_t shipEI, NODE *hullNodePointer);
};
