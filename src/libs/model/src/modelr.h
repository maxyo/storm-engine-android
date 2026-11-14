#pragma once

#include <string>

#include "animation.h"
#include "renderer.h"
#include "geometry.h"
#include "model.h"
#include "gpu_skinning.hpp"
#include "skinning_stream_cache.hpp"

class NODER : public NODE
{
    std::string sys_modelName_base;
    std::string sys_modelName_full;
    std::string sys_LightPath;
    std::string sys_TexPath;
    std::string sys_lmPath;
    bool isReleased;

    static int32_t depth, node;
    uintptr_t idGeoGroup; // id of "geometry" string
    char technique[256], name[256];

    // local radius and center of geometry
    float geo_radius;
    glm::vec3 geo_center;

    float max_view_dist;
    float distance_blend;

  public:
    // local radius and center of whole node with children
    float radius;
    glm::vec3 center;

    static VGEOMETRY *gs;
    static VRenderer *rs;

    bool Init(const char *lightPath, const char *pname, const char *oname, const glm::mat4 &m, const glm::mat4 &globm,
              NODER *par, const char *lmPath) override;
    NODER();
    ~NODER() override;
    void Draw();
    float Trace(const glm::vec3 &src, const glm::vec3 &dst) override;
    NODER *GetNode(int32_t n);
    NODER *FindNode(const char *cNodeName);
    float Update(glm::mat4 &mtx, glm::vec3 &cnt);
    const char *GetName() override;
    bool Clip();

    // unlink node to model
    entid_t Unlink2Model() override;

    void SetTechnique(const char *name) override;
    const char *GetTechnique() override;
    
    // replace only this node model without touching anything else
    void SubstituteGeometry(const std::string &new_model) override;

    void ReleaseGeometry();
    void RestoreGeometry();

    void SetMaxViewDist(float fDist);

};

#define MODEL_ANI_MAXBUFFERS 16

class MODELR : public MODEL
{
    std::string LightPath, lmPath;

    struct ANIBUFFER
    {
        FVF_VERTEX *v;
        int32_t num;
        int32_t vb;
    };

    VRenderer *rs;
    VGEOMETRY *GeometyService;
    Animation *ani;
    float aniPos[ANI_MAX_ACTIONS];

    bool bSetupFog;
    bool bFogEnable;
    float fFogDensity;

    storm::model::GpuSkinning gpuSkinning_;
    storm::model::SkinningStreamCache<GEOS::VERTEX0> skinnedStreams_; // CPU skinning: the caustic pass only
    static storm::render::VertexBufferId VBTransform(void*, const void*, const std::shared_ptr<storm::render::VertexSourceLifetime>&, int32_t, int32_t, int32_t, bool);

    ANIBUFFER aniVerts[MODEL_ANI_MAXBUFFERS];
    void AniRender();
    NODE *colideNode;
    void FindPlanes(const glm::mat4 &view, const glm::mat4 &proj);

    unsigned short *idxBuff;

  public:
    NODER *root;

    MODELR();
    ~MODELR() override;
    bool Init() override;
    void Realize(uint32_t Delta_Time);
    uint64_t ProcessMessage(MESSAGE &message) override;
    void LostRender();
    void RestoreRender();

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

    NODE *GetNode(int32_t n) override;
    NODE *FindNode(const char *cNodeName) override;
    void Update() override;
    Animation *GetAnimation() override;

    float Trace(const glm::vec3 &src, const glm::vec3 &dst) override;
    const char *GetCollideMaterialName() override;
    bool GetCollideTriangle(TRIANGLE &triangle) override;
    bool Clip(const PLANE *planes, int32_t nplanes, const glm::vec3 &center, float radius,
              ADD_POLYGON_FUNC addpoly) override;

    NODE *GetCollideNode() override;

  protected:
    bool useBlend;
    uint32_t blendTime, passedTime;
    std::string blendTechnique;
    float alpha1, alpha2;
};
