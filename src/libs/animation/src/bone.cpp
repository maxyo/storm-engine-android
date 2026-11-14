// ============================================================================================
// Spirenkov Maxim aka Sp-Max Shaman, 2001
// --------------------------------------------------------------------------------------------
// Storm engine v2.00
// --------------------------------------------------------------------------------------------
// Bone
// --------------------------------------------------------------------------------------------
// Model bone containing its animation
// ============================================================================================

#include "bone.h"

#include "core.h"
#include "storm_assert.h"
#include "vma.hpp"

#include <cstring>

// ============================================================================================
// Construction, destruction
// ============================================================================================

Bone::Bone()
{
    parent = nullptr;
    ang = nullptr;
    pos = nullptr;
    numFrames = 0;
}

Bone::~Bone()
{
    delete[] ang;
    delete[] pos;
}

// how many frames of animation there will be
void Bone::SetNumFrames(int32_t num, glm::vec3 &sPos, bool isRoot)
{
    delete[] ang;
    delete[] pos;
    ang = nullptr;
    pos = nullptr;
    numFrames = num;
    if (numFrames <= 0)
    {
        numFrames = 0;
        return;
    }
#ifdef ANI_COMPRESS_ENABLE
    ang = new COMP_QUATERNION[num];
    memset(ang, 0, numFrames * sizeof(ang[0]));
#else
    ang = new glm::quat[num];
    memset(ang, 0, numFrames * sizeof(ang[0]));
#endif

    if (isRoot)
    {
        pos = new glm::vec3[num];
        memset(pos, 0, numFrames * sizeof(glm::vec3));
    }
    pos0 = sPos;
}

// Set animation positions
void Bone::SetPositions(const glm::vec3 *pArray, int32_t numPos)
{
    Assert(numPos == numFrames);
    Assert(pArray);
    Assert(pos);
    memcpy(pos, pArray, numFrames * sizeof(glm::vec3));
}

//=====================================================================================

#ifdef ANI_COMPRESS_ENABLE

//-------------------------------------
// Compressed animation
//-------------------------------------

// Set animation angles
void Bone::SetAngles(const glm::quat *aArray, int32_t numAng)
{
    Assert(numAng == numFrames);
    Assert(aArray);
    Assert(ang);
    for (int32_t i = 0; i < numAng; i++)
    {
        auto x = Clamp(aArray[i].x, "Animation is break: qt.x < -1.0f or qt.x > 1.0f !!!");
        auto y = Clamp(aArray[i].y, "Animation is break: qt.y < -1.0f or qt.y > 1.0f !!!");
        auto z = Clamp(aArray[i].z, "Animation is break: qt.z < -1.0f or qt.z > 1.0f !!!");
        auto w = Clamp(aArray[i].w, "Animation is break: qt.w < -1.0f or qt.w > 1.0f !!!");
        x = static_cast<float>(asin(x) / (PI * 0.5)) * 32767.0f;
        y = static_cast<float>(asin(y) / (PI * 0.5)) * 32767.0f;
        z = static_cast<float>(asin(z) / (PI * 0.5)) * 32767.0f;
        w = static_cast<float>(asin(w) / (PI * 0.5)) * 32767.0f;
        ang[i].x = static_cast<short>(static_cast<int32_t>(x));
        ang[i].y = static_cast<short>(static_cast<int32_t>(y));
        ang[i].z = static_cast<short>(static_cast<int32_t>(z));
        ang[i].w = static_cast<short>(static_cast<int32_t>(w));
    }
}

inline void Bone::GetFrame(int32_t f, glm::quat &qt)
{
    qt.x = sinf((ang[f].x * (1.0f / 32767.0f)) * PI * 0.5f);
    qt.y = sinf((ang[f].y * (1.0f / 32767.0f)) * PI * 0.5f);
    qt.z = sinf((ang[f].z * (1.0f / 32767.0f)) * PI * 0.5f);
    qt.w = sinf((ang[f].w * (1.0f / 32767.0f)) * PI * 0.5f);
}

#else

//-------------------------------------
// Uncompressed animation
//-------------------------------------

// Set animation angles
void Bone::SetAngles(const glm::quat *aArray, int32_t numAng)
{
    Assert(numAng == numFrames);
    Assert(aArray);
    Assert(ang);
    memcpy(ang, aArray, numFrames * sizeof(*ang));
}

inline void Bone::GetFrame(int32_t f, glm::quat &qt)
{
    qt = ang[f];
}

#endif

inline float Bone::Clamp(float v, const char *str)
{
    auto isErr = false;
    if (v < -1.0f)
    {
        v = -1.0f;
        isErr = true;
    }
    if (v > 1.0f)
    {
        v = 1.0f;
        isErr = true;
    }
    if (isErr && str)
        core.Trace(str);
    return v;
}

//=====================================================================================

// Initialize start matrix
void Bone::BuildStartMatrix()
{
    if (numFrames == 0 || !ang)
        return;
    glm::quat a;
    GetFrame(0, a);
    auto inmtx = glm::mat4_cast(a);
    storm::math::pos(inmtx) = pos0;
    if (parent)
        start = parent->start * inmtx;
    else
        start = inmtx;
}

// --------------------------------------------------------------------------------------------
// Working with a bone
// --------------------------------------------------------------------------------------------

// Add animation frames
glm::vec3 Bone::BlendPosition(int32_t frame, float kBlend) const
{
    if (frame + 1 >= numFrames)
        return pos[numFrames - 1];
    return pos[frame] + kBlend * (pos[frame + 1] - pos[frame]);
}

void Bone::BlendFrame(int32_t frame, float kBlend, glm::quat &res)
{
    if (numFrames <= 0)
        return;
    const auto f0 = frame;
    const auto f1 = frame + 1;
    if (f0 >= numFrames || f1 >= numFrames)
    {
        GetFrame(numFrames - 1, res);
        return;
    }
    glm::quat q0, q1;
    GetFrame(f0, q0);
#ifdef ANI_COMPRESS_ENABLE
    // Identical packed keys decode identically within this call. Reuse only
    // the decoding; SLerp still applies its original rounding and sign rules.
    if (std::memcmp(&ang[f0], &ang[f1], sizeof(ang[0])) == 0)
        q1 = q0;
    else
        GetFrame(f1, q1);
#else
    GetFrame(f1, q1);
#endif
    if (kBlend < 0.0f)
        kBlend = 0.0f;
    if (kBlend > 1.0f)
        kBlend = 1.0f;
    res = storm::math::slerp(q0, q1, kBlend);
}

// Create a matrix for the resulting position
void Bone::BuildMatrix()
{
    // Turns
    // matrix = a.BuildMatrix();
    matrix = glm::mat4_cast(a);
    storm::math::pos(matrix) = p;
    // Multiply by the parent matrix
    if (parent)
        matrix = parent->matrix * glm::mat4(matrix);
}
