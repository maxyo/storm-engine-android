#pragma once

#include "geos.h"
#include "dynamic_vertex_stream.hpp"
#include "vertex_source_lifetime.hpp"
#include "service.h"
#include "vma.hpp"

class ANIMATION
{
  public:
    virtual ~ANIMATION(){};
    virtual float SetTime(float time) = 0;
    virtual void SetMatrix() = 0;
};

using VERTEX_TRANSFORM = storm::render::VertexBufferId (*)(void *context, const void *vb, const std::shared_ptr<storm::render::VertexSourceLifetime>& lifetime, int32_t startVrt, int32_t nVerts, int32_t totVerts);

class VGEOMETRY : public SERVICE
{
  public:
    class ANIMATION_VB
    {
      public:
        int32_t nvertices;
        int32_t fvf;
        int32_t stride;
        void *buff;
        std::shared_ptr<storm::render::VertexSourceLifetime> lifetime;
    };

    ~VGEOMETRY() override{};
    virtual GEOS *CreateGeometry(const char *file_name, const char *light_file_name, int32_t flags,
                                 const char *lmPath = nullptr) = 0;
    virtual void DeleteGeometry(GEOS *) = 0;
    virtual ANIMATION *LoadAnimation(const char *anim) = 0;
    virtual void SetTechnique(const char *name) = 0;
    virtual void SetVBConvertFunc(VERTEX_TRANSFORM _transform_func, void *context = nullptr) = 0;
    virtual ANIMATION_VB GetAnimationVBDesc(int32_t avb) = 0;

    virtual const char *GetTexturePath() = 0;
    virtual void SetTexturePath(const char *) = 0;

    virtual void SetCausticMode(bool bSet = false) = 0;
};
