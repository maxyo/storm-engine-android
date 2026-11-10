// ============================================================================================
// Spirenkov Maxim aka Sp-Max Shaman, 2001
// --------------------------------------------------------------------------------------------
// Storm engine v2.00
// --------------------------------------------------------------------------------------------
// Bone
// --------------------------------------------------------------------------------------------
// Model bone containing its animation
// ============================================================================================

#pragma once

//============================================================================================

#include "math_inlines.h"
#include "storm_glm.h"
//============================================================================================

#define ANI_COMPRESS_ENABLE

//============================================================================================

class Bone
{
#pragma pack(push, 1)

    struct COMP_QUATERNION
    {
        short x, y, z;
        short w;
    };

#pragma pack(pop)

    // --------------------------------------------------------------------------------------------
    // Construction, destruction
    // --------------------------------------------------------------------------------------------
  public:
    Bone();
    virtual ~Bone();
    // Set parent
    void SetParent(Bone *parentBone);
    // how many frames of animation there will be
    void SetNumFrames(int32_t num, glm::vec3 &sPos, bool isRoot = false);
    // Set animation positions
    void SetPositions(const glm::vec3 *pArray, int32_t numPos);
    // Set animation angles
    void SetAngles(const glm::quat *aArray, int32_t numAng);
    // Initialize start matrix
    void BuildStartMatrix();

    // --------------------------------------------------------------------------------------------
    // Working with a bone
    // --------------------------------------------------------------------------------------------
  public:
    // Add animation frames
    void BlendFrame(int32_t frame, float kBlend, glm::quat &res);
    // void BlendFrame(float frame);
    // Create a matrix for the resulting position
    void BuildMatrix();
    // Create matrix for frame 0
    // void BuildMatrixZero();
    // Get position matrix
    glm::mat4 &GetMatrix();
    // Get the starting position matrix
    glm::mat4 &StartMatrix();

    // --------------------------------------------------------------------------------------------
    // Encapsulation
    // --------------------------------------------------------------------------------------------
  private:
    void GetFrame(int32_t f, glm::quat &qt);
    float Clamp(float v, const char *str);
    // Linear position interpolation
    float LerpPos(float a, float b, float k);
    // Linear Angle Interpolation
    float LerpAng(float a, float b, float k);

  public:
    Bone *parent; // Parent bone

#ifdef ANI_COMPRESS_ENABLE
    COMP_QUATERNION *ang; // Compressed angles of animation frames
#else
    glm::quat *ang; // Animation frame angles
#endif

    glm::vec3 *pos;   // Animation frame positions
    int32_t numFrames; // Number of animation frames
    glm::vec3 pos0;   // Bone position if there are no animation frame positions

    glm::vec3 p;        // Bone position in local coordinates
    glm::quat a;        // Bone rotation angles in local coordinates
    glm::mat4 matrix;   // Bone position matrix
    glm::mat4 start;    // Frame 0 matrix
};

//============================================================================================
// inline
//============================================================================================

// Set parent
inline void Bone::SetParent(Bone *parentBone)
{
    parent = parentBone;
}

// Get position matrix
inline glm::mat4 &Bone::GetMatrix()
{
    return matrix;
}

// Get the starting position matrix
inline glm::mat4 &Bone::StartMatrix()
{
    return start;
}

// Linear position interpolation
inline float Bone::LerpPos(float a, float b, float k)
{
    return a + k * (b - a);
}

// Linear Angle Interpolation
inline float Bone::LerpAng(float a, float b, float k)
{
    float delta = b - a;
    if (delta < -PI)
        delta = 2 * PI + delta;
    if (delta > PI)
        delta = -2 * PI + delta;
    return a + k * delta;
}
