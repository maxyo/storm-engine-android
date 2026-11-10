#pragma once

#include "storm_glm.h"
#include "entity.h"
#include "plane.h"

struct TRIANGLE
{
    glm::vec3 vrt[3];
};

using ADD_POLYGON_FUNC = bool (*)(const glm::vec3 *v, int32_t nv);

class COLLISION_OBJECT : public Entity
{
  public:
    bool Init() override
    {
        return true;
    }

    ~COLLISION_OBJECT() override{};
    glm::mat4 mtx;

    virtual float Trace(const glm::vec3 &src, const glm::vec3 &dst) = 0;
    virtual bool Clip(const PLANE *planes, int32_t nplanes, const glm::vec3 &center, float radius,
                      ADD_POLYGON_FUNC addpoly) = 0;

    virtual const char *GetCollideMaterialName() = 0;
    virtual bool GetCollideTriangle(TRIANGLE &triangle) = 0;
};
