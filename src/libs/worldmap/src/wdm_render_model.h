//============================================================================================
//    Spirenkov Maxim, 2001
//--------------------------------------------------------------------------------------------
//    Sea Dogs II
//--------------------------------------------------------------------------------------------
//    WdmRenderModel
//--------------------------------------------------------------------------------------------
//
//============================================================================================

#pragma once

#include "wdm_objects.h"
#include "wdm_render_object.h"

#include "storm_glm.h"

class GEOS;
class VGEOMETRY;

class WdmRenderModel : public WdmRenderObject
{
    // --------------------------------------------------------------------------------------------
    // Construction, destruction
    // --------------------------------------------------------------------------------------------
  public:
    WdmRenderModel();
    ~WdmRenderModel() override;

    virtual bool Load(const char *modelName);

    void PRender(VRenderer *rs) override;
    void MRender(VRenderer *rs) override;
    void LRender(VRenderer *rs) override;

    void SetTech(const char *t, const char *ta);
    int32_t GetTexture(int32_t stage) const;
    void SetTexture(int32_t stage, int32_t id) const;

    glm::mat4 mtx;
    float alpha;

  public:
    void Render(VRenderer *rs) const;

    GEOS *geo;
    const char *tech;
    const char *techa;

    // Positioned circle
    glm::vec3 center;
    float radius;

    bool drawCircle;
};
