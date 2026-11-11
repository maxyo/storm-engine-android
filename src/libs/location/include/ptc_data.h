//============================================================================================
//    Spirenkov Maxim, 2002
//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
//    PtcData
//--------------------------------------------------------------------------------------------
//
//============================================================================================

#pragma once

#include "storm_glm.h"
#include "ptc.h"

#define PTCDATA_MAXSTEPS 32

class VRenderer;

class PtcData
{
  public:
    struct Triangle
    {
        int32_t index;
        glm::vec3 v[3];
        glm::vec3 n;
    };

    struct DbgVertex
    {
        float x, y, z;
        uint32_t c;
    };

    // --------------------------------------------------------------------------------------------
    // Construction, destruction
    // --------------------------------------------------------------------------------------------
  public:
    PtcData();
    virtual ~PtcData();

    // load the patch
    bool Load(const char *path);
    // Determine the current position
    int32_t FindNode(const glm::vec3 &pos, float &y);
    // Moves "pos" to "to", returns a new node
    int32_t Move(int32_t curNode, const glm::vec3 &to, glm::vec3 &pos, int32_t depth = 0);
    // Get normal to a node
    void GetNodeNormal(int32_t curNode, glm::vec3 &n) const;
    // Find the direction of the path
    bool FindPathDir(int32_t curNode, const glm::vec3 &cur, int32_t toNode, const glm::vec3 &to, int32_t &node, glm::vec3 &toPos);
    // Find patch intersection
    float Trace(const glm::vec3 &s, const glm::vec3 &d) const;
    // Find the force pushing away from the edges
    void FindForce(int32_t curNode, glm::vec3 &force) const;
    void FindForce(int32_t curNode, const glm::vec3 &pos, float dist, glm::vec3 &force) const;
    // Get node material
    const char *GetMaterial(int32_t curNode);

    // Get triangles intersecting a given square
    // Triangle * GetTriangles(float x, float z, float sx, float sz, int32_t & num);

    // Last move results
    bool isSlide;     // Slided
    glm::vec3 slideDir; // Sliding direction
    bool isBearing;   // Fell on a bearing

    glm::vec3 stepPos[PTCDATA_MAXSTEPS];
    int32_t numSteps;

    glm::vec3 middle;

    // Debug rendering
    void DebugDraw(VRenderer *rs, float dltTime);

    // --------------------------------------------------------------------------------------------
    // Encapsulation
    // --------------------------------------------------------------------------------------------
  private:
    // Protection function
    void SFLB_PotectionLoad();

    // Calculate the height of a point on the plane of a triangle
    float FindHeight(int32_t trgID, float x, float z);
    // Find the direction of the path
    bool FindPathDir(int32_t step, int32_t curNode, const glm::vec3 &cur, int32_t toNode, const glm::vec3 &to, int32_t &node,
                     glm::vec3 &pos);
    // Find a point on an edge
    glm::vec3 FindEdgePoint(const glm::vec3 &vs, const glm::vec3 &ve, const glm::vec3 &cur, const glm::vec3 &to);
    // Check the intersection of a triangle with a line segment
    float Trace(PtcTriangle &trg, const glm::vec3 &s, const glm::vec3 &d) const;
    // Add triangle to buffer
    // void AddClTriangle(int32_t i);

    // private:
  public:
    // Data block
    char *data;

    // Geometry
    PtcTriangle *triangle; // Geometry triangles
    int32_t numTriangles;     // Number of geometry triangles

    PtcVertex *vertex; // Vertices of geometry
    int32_t numVerteces;  // Number of geometry vertices

    PtcNormal *normal; // Geometry triangle normals
    int32_t numNormals;   // Number of geometry normals

    PtcMaterials *materials; // Materials

    // Collision data
    glm::vec3 min, max; // What to fit into

    PtcMap *map;  // Quick search map
    int32_t l, w;    // Quick Search Map Size
    float ls, ws; // Size of one square

    uint16_t *indeces; // Indexes
    int32_t numIndeces;   // Number of indexes

    // Pathfinding data
    uint8_t *table; // Direction table
    int32_t lineSize;  // Line size

    // Triangles after collision
    Triangle *ctriangle;
    int32_t numClTriangles;
    int32_t maxClTriangles;

    // Debug information
    DbgVertex *dbgTriangles;
    DbgVertex *dbgEdges;
};
