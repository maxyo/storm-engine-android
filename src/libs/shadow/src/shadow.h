#pragma once

#include "collide.h"
#include "renderer.h"
#include "model.h"
#include "vma.hpp"

#include <vector>

class Shadow : public Entity
{
    VRenderer *rs;
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
        glm::mat4 proj; // the light's projection of the TEXTURE_SIZE map
        int32_t shade = 0;
        int32_t tile = 0; // this frame's tile in the shadow atlases
    };
    Prepared prepared_;
    // What prepare() works out before it knows whether the shadow needs its rays this frame.
    struct Setup
    {
        glm::vec3 objPos{0.0f}, ObjPos{0.0f}, headPos{0.0f}, lightPos{0.0f}, dir{0.0f}, camPos{0.0f};
        float radius = 0.0f; // of the character's model
    };
    Setup setup_;
    bool pending_ = false;     // setup() passed the cheap view tests: finish() follows
    bool rayGranted_ = false;  // this frame's budget of ray casts reaches this shadow
    float camDist2_ = 0.0f;
    // The occlusion rays of the last frames (setup, finish): a shadow is due for new ones when the character, the
    // light or the model size moved, or the rays are kRayRefresh frames old (moving occluders), but only the nearest
    // few shadows are granted a cast each frame; the others keep what they have. headRay < 0: not cast yet;
    // minVal < 0: the body rays were not cast (the shadow was out of view).
    struct Rays
    {
        glm::vec3 ObjPos{0.0f}, dir{0.0f};
        float radius = -1.0f;
        float headRay = -1.0f, minVal = -1.0f;
        uint32_t age = 0;
    };
    Rays rays_;
    // The location's triangles around the character, clipped to the projector's volume widened by kFanMargin, and
    // what they were cut for: while the character, the light and the head ray keep the projector's volume inside
    // that margin, project() clips these few polygons exactly instead of walking the location's trees. The location's
    // tree takes the polygons whose plane is closer to the sphere's centre than its radius, so project() keeps that rule
    // for the sphere of the frame.
    struct Fan
    {
        bool valid = false;
        uint32_t age = 0;
        glm::vec3 ObjPos{0.0f}, dir{0.0f}, cen{0.0f};
        float perspective = 0.0f, sphereRadius = 0.0f;
        std::vector<glm::vec3> verts; // the polygons one after another
        std::vector<uint8_t> counts;  // their vertex counts
        std::vector<glm::vec4> planes; // the plane of each polygon: normal, offset
    };
    Fan fan_;
    uint32_t lastEpoch_ = UINT32_MAX;
    static std::vector<Shadow *> instances_;
    static uint32_t epoch_, preparedEpoch_;
    static float fogDensity_; // EWhr_GetFogDensity of this frame (prepareAll)
    void prepareAll(uint32_t deltaTime);
    void setup(const glm::mat4 &view, const glm::mat4 &proj);
    bool raysDue() const;
    void finish(uint32_t deltaTime);
    bool fanFits() const;
    void rebuildFan();
    void renderModel();
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
