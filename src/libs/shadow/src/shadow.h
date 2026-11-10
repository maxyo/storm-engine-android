#pragma once

#include "collide.h"
#include "dx9render.h"
#include "model.h"
#include "vma.hpp"

#include <vector>

class Shadow : public Entity
{
    VDX9RENDER *rs;
    COLLIDE *col;
    void FindPlanes(const glm::mat4 &view, const glm::mat4 &proj);
    PLANE planes[6];
    entid_t entity;
    void Smooth();
    float shading;
    uint32_t blendValue;

    // Shadow maps of a frame are rendered together (prepareAll), the projection stays in Realize.
    struct Prepared
    {
        bool visible = false;
        glm::mat4 trans, lightmtx;
        float perspective = 0.0f, radius = 0.0f;
        glm::vec3 dir, cen, objPos, ObjPos, lightPos, camPos;
        PLANE planes[4];
        int32_t shade = 0;
    };
    Prepared prepared_;
    IDirect3DTexture9 *blurTex_ = nullptr; // this shadow's blurred map, read by project()
    uint32_t lastEpoch_ = UINT32_MAX;
    static std::vector<Shadow *> instances_;
    static uint32_t epoch_, preparedEpoch_;
    void prepareAll(uint32_t deltaTime);
    void prepare(uint32_t deltaTime, const glm::mat4 &view, const glm::mat4 &proj, bool &sceneOpen);
    void project();

  public:
#define SHADOW_FVF (D3DFVF_XYZ | D3DFVF_TEXTUREFORMAT2 | D3DFVF_TEX1)

    struct SHADOW_VERTEX
    {
        glm::vec3 pos;
        float tu, tv;
    };

    Shadow();
    ~Shadow() override;
    bool Init() override;
    void Realize(uint32_t Delta_Time);
    uint64_t ProcessMessage(MESSAGE &message) override;

    void ProcessStage(Stage stage, uint32_t delta) override
    {
        switch (stage)
        {
            // case Stage::execute:
            //    Execute(delta); break;
        case Stage::realize:
            Realize(delta);
            break;
        case Stage::lost_render:
            LostRender();
            break;
        case Stage::restore_render:
            RestoreRender();
            break;
        }
    }

    void LostRender();
    void RestoreRender();
};
