#pragma once

#include <memory>
#include <vector>

#include "storm_glm.h"
#include "geos.h"
#include "object.h"

class Animation;
class NODER;

class NODE
{
  public:
    glm::mat4 loc_mtx;
    glm::mat4 glob_mtx;

    enum FLAGS
    {
        VISIBLE = (1 << 0),
        VISIBLE_TREE = (1 << 1),
        CLIP_ENABLE = (1 << 2),
        CLIP_ENABLE_TREE = (1 << 3),
        TRACE_ENABLE = (1 << 4),
        TRACE_ENABLE_TREE = (1 << 5)
    };

    int32_t flags;

    GEOS *geo;
    GEOS::MATERIAL_FUNC geoMaterialFunc;
    // children, owned: a node frees its subtree. Unlink2Model hands a child to a model of its own.
    std::vector<std::unique_ptr<NODE>> next;
    // the node this one hangs from; nullptr for a model's root
    NODE *parent;

    virtual ~NODE(){};
    // name of this node - locator's name
    virtual const char *GetName() = 0;

    // unlink node to model: the node and its subtree become the root of a new model entity
    virtual entid_t Unlink2Model() = 0;

    virtual void SetTechnique(const char *name) = 0;
    virtual const char *GetTechnique() = 0;
    virtual bool Init(const char *lightPath, const char *pname, const char *oname, const glm::mat4 &m,
                      const glm::mat4 &globm, NODER *par, const char *lmPath) = 0;

    virtual float Trace(const glm::vec3 &src, const glm::vec3 &dst) = 0;

    virtual void SubstituteGeometry(const std::string& new_model) = 0;
};

class VRenderer;

class MODEL : public COLLISION_OBJECT
{
  public:
    class RenderTuner
    {
        friend MODEL;

      public:
        RenderTuner(){};

        virtual ~RenderTuner(){};

        virtual void Set(MODEL *model, VRenderer *rs){};

        virtual void Restore(MODEL *model, VRenderer *rs){};
    };

    void SetRenderTuner(RenderTuner *rt)
    {
        renderTuner = rt;
    };

    RenderTuner *GetRenderTuner(RenderTuner *rt) const
    {
        return renderTuner;
    };

  protected:
    RenderTuner *renderTuner;

  public:
  public:
    MODEL()
    {
        renderTuner = nullptr;
    };

    ~MODEL() override
    {
        renderTuner = nullptr;
    };

    virtual NODE *GetNode(int32_t n) = 0;
    virtual NODE *FindNode(const char *cNodeName) = 0;
    virtual void Update() = 0;
    virtual Animation *GetAnimation() = 0;

    float Trace(const glm::vec3 &src, const glm::vec3 &dst) override = 0;
    bool Clip(const PLANE *planes, int32_t nplanes, const glm::vec3 &center, float radius,
              ADD_POLYGON_FUNC addpoly) override = 0;

    const char *GetCollideMaterialName() override = 0;
    bool GetCollideTriangle(TRIANGLE &triangle) override = 0;

    virtual NODE *GetCollideNode() = 0;
};
