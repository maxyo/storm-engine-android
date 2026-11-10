#pragma once

// ANIMATION_HEADER
// int32_t parentIndex[nJoints]
// glm::vec3 jointPos[nJoints]

// Frames:
// glm::vec3 rootPos[nFrames]
// glm::vec3 jointAngle[nJoints][nFrames]

#pragma pack(push, 1)

namespace ANFILE
{
struct HEADER
{
    int32_t nFrames;
    int32_t nJoints;
    float framesPerSec;
};
}; // namespace ANFILE

#pragma pack(pop)
