#pragma once

#include "dx9render.h"
#include "storm_glm.h"

class PathTracks
{
#pragma pack(push, 1)
    struct Point
    {
        glm::quat q;
        glm::vec3 p;
        glm::vec3 s;
    };
#pragma pack(pop)

    //--------------------------------------------------------------------------------------------
  public:
    PathTracks();
    virtual ~PathTracks();

    //--------------------------------------------------------------------------------------------
  public:
    // Upload track to PathTracks
    bool Load(const char *fileName);
    // Draw track
    void Draw(VDX9RENDER *render);
    // Get track point
    bool GetPoint(float index, glm::vec3 &cp, glm::quat &cq) const;

    //--------------------------------------------------------------------------------------------
  private:
    Point *point;
    int32_t numPoints;
};
