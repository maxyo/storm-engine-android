#pragma once

#include "storm_glm.h"

#include "entity.h"
#include "object.h"
#include "service.h"
#include "plane.h"
#include "vma.hpp"

class LOCAL_COLLIDE
{
  public:
    virtual ~LOCAL_COLLIDE(){};

    // clip faces into the box and returns num of faces
    virtual int32_t SetBox(const glm::vec3 &boxSize, const glm::mat4 &transform, bool testOnly = false) = 0;

    // returns pointer to clipped face and fills num of vertices in face
    virtual const glm::vec3 *GetFace(int32_t &numVertices) = 0;

    // trace a ray inside the box
    virtual float Trace(const glm::vec3 &src, const glm::vec3 &dst) = 0;
};

class COLLIDE : public SERVICE
{
  public:
    ~COLLIDE() override = default;

    // creates LocalCollide object which must be deleted after use
    virtual LOCAL_COLLIDE *CreateLocalCollide(layer_index_t idx) = 0;

    virtual float Trace(entid_t entity, const glm::vec3 &src, const glm::vec3 &dst) = 0;

    virtual float Trace(entity_container_cref entities, const glm::vec3 &src, const glm::vec3 &dst,
                        const entid_t *exclude_list, int32_t exclude_num) = 0;

    virtual bool Clip(entity_container_cref entities, const PLANE *planes, int32_t nplanes, const glm::vec3 &center,
                      float radius, ADD_POLYGON_FUNC addpoly, const entid_t *exclude_list, int32_t exclude_num) = 0;

    virtual entid_t GetObjectID() = 0;
};
