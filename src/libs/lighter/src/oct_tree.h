//============================================================================================
//    Spirenkov Maxim aka Sp-Max Shaman, 2001
//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
//    OctTree
//--------------------------------------------------------------------------------------------
//
//============================================================================================

#pragma once

#include "l_geometry.h"

class OctTree
{
    struct OTNode
    {
        OTNode(const glm::vec3 &_min, const glm::vec3 &_max);
        ~OTNode();
        OTNode *node[8]; // Children
        glm::vec3 min;     // Minimum value
        glm::vec3 max;     // Maximum value
        Vertex **vrt;    // Vertex indices
        int32_t num;        // Number of vertices
    };

    // --------------------------------------------------------------------------------------------
    // Construction, destruction
    // --------------------------------------------------------------------------------------------
  public:
    OctTree();
    virtual ~OctTree();

    // Initialize tree
    void Init(LGeometry *g);
    // Find vertices in a given radius
    void FindVerts(const glm::vec3 &pos, float r);

    std::vector<OctFndVerts> verts;
    int32_t numVerts;
    int32_t maxVerts;

    // --------------------------------------------------------------------------------------------
    // Encapsulation
    // --------------------------------------------------------------------------------------------
  private:
    // Adding vertices
    bool AddVertex(OTNode *node, Vertex *v);
    // Optimizing the tree
    void Optimize(OTNode *node);
    // Search
    void FindVerts(OTNode *node);

    int32_t Check(OTNode *node, Vertex *v, int32_t num);

  private:
    Vertex *vrt;
    int32_t numVrt;
    OTNode *root;
    glm::vec3 vertsPos, vertsPosMin, vertsPosMax;
    float vertsR;
};
