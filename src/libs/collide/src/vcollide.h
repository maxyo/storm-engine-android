#pragma once

#include "collide.h"


class LCOLL : public LOCAL_COLLIDE
{
    layer_index_t layerIndex_;
    PLANE plane[6];
    glm::vec3 boxCenter;
    float boxRadius;
    COLLIDE *col;

  public:
    LCOLL(layer_index_t idx);
    ~LCOLL() override;
    int32_t SetBox(const glm::vec3 &boxSize, const glm::mat4 &transform, bool testOnly = false) override;
    const glm::vec3 *GetFace(int32_t &numVertices) override;
    float Trace(const glm::vec3 &src, const glm::vec3 &dst) override;
};

class COLL : public COLLIDE
{
  public:
    COLL() = default;
    ~COLL() override = default;
    LOCAL_COLLIDE *CreateLocalCollide(layer_index_t idx) override;
    float Trace(entid_t entity, const glm::vec3 &src, const glm::vec3 &dst) override;
    float Trace(entity_container_cref entities, const glm::vec3 &src, const glm::vec3 &dst, const entid_t *exclude_list,
                int32_t exclude_num) override;
    bool Clip(entity_container_cref entities, const PLANE *planes, int32_t nplanes, const glm::vec3 &center, float radius,
              ADD_POLYGON_FUNC addpoly, const entid_t *exclude_list, int32_t exclude_num) override;
    entid_t GetObjectID() override;
};


// API_SERVICE_START("collide service")
//    DECLARE_MAIN_SERVICE(COLL)
// API_SERVICE_END(COLL)
