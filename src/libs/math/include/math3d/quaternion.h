//============================================================================================
// Spirenkov Maxim, 2003
//============================================================================================
// Quaternion
//============================================================================================

#pragma once

#include "matrix.h"
#include "vector.h"

//============================================================================================

#pragma pack(push, 1)

//============================================================================================

// Quaternion representation class
class Quaternion
{
  public:
    union {
        struct
        {
            // X vector component
            float x;
            // Y vector component
            float y;
            // Z Vector component
            float z;
            // Scalar component
            float w;
        };

        // Array representation
        float q[4];
    };

    // -----------------------------------------------------------
    // Constructors
    // -----------------------------------------------------------
  public:
    // Keep serialized x/y/z/w order independent of GLM quaternion data macros.
    storm::math::GlmVec4 glmComponents() const { return {x, y, z, w}; }
    storm::math::GlmQuat glmValue() const
    {
        storm::math::GlmQuat value;
        value.x = x; value.y = y; value.z = z; value.w = w;
        return value;
    }
    Quaternion &setGlm(const storm::math::GlmVec4 &value)
    {
        x = value.x; y = value.y; z = value.z; w = value.w;
        return *this;
    }

    // Set identity quaternion
    Quaternion();
    // With rotation angles
    Quaternion(float ax, float ay, float az);
    // With rotation angles
    Quaternion(const Vector &ang);
    // From components
    Quaternion(float qx, float qy, float qz, float qw);
    // From the matrix
    Quaternion(const Matrix &mtx);

    // -----------------------------------------------------------
    // Operators
    // -----------------------------------------------------------
  public:
    // Negation
    Quaternion &operator-();
    // Equate
    Quaternion &operator=(const Quaternion &q);
    // Addition
    Quaternion &operator+=(const Quaternion &q);
    // Addition
    Quaternion &operator+=(float f);
    // Subtract
    Quaternion &operator-=(const Quaternion &q);
    // Subtract
    Quaternion &operator-=(float f);
    // Multiply
    Quaternion &operator*=(float f);
    // Multiply
    Quaternion &operator*=(const Quaternion &q);
    // Divide
    Quaternion &operator/=(float f);

    // -----------------------------------------------------------
    // Filling
    // -----------------------------------------------------------
  public:
    // Set identity quaternion
    void SetIdentity();
    // Set from angles of rotation
    void Set(float ax, float ay, float az);
    // Set from angles of rotation
    void Set(const Vector &ang);
    // Construct from components
    void Set(float qx, float qy, float qz, float qw);
    // Construct from matrix
    void Set(const Matrix &mtx);

    // -----------------------------------------------------------
    // Transformation
    // -----------------------------------------------------------
  public:
    // Normalize
    Quaternion &Normalize();
    // Get coplanar quaternion
    Quaternion &Conjugate();
    // Invert quaternion
    Quaternion &Inverse();

    // Spherical linear interpolation
    Quaternion &SLerp(const Quaternion &q1, const Quaternion &q2, float kBlend);

    // -----------------------------------------------------------
    // Utilities
    // -----------------------------------------------------------
  public:
    // Get a matrix from a quaternion
    void GetMatrix(Matrix &mtx) const;
    // Get the length of the quaternion
    float GetLength() const;
    // Get the square of the length of a quaternion
    float GetLengthSqr() const;
};

// ===========================================================
// Constructors
// ===========================================================

// Set identity quaternion
inline Quaternion::Quaternion()
{
    x = 0.0f;
    y = 0.0f;
    z = 0.0f;
    w = 1.0f;
}

// From roration angles
inline Quaternion::Quaternion(float ax, float ay, float az)
{
    Set(ax, ay, az);
}

// From rotation angles
inline Quaternion::Quaternion(const Vector &ang)
{
    Set(ang);
}

// From components
inline Quaternion::Quaternion(float qx, float qy, float qz, float qw)
{
    Set(qx, qy, qz, qw);
}

// From a matrix
inline Quaternion::Quaternion(const Matrix &mtx)
{
    Set(mtx);
}

// ===========================================================
// Operators
// ===========================================================

// Negation
inline Quaternion &Quaternion::operator-()
{
    return setGlm(-glmComponents());
}

// Equate
inline Quaternion &Quaternion::operator=(const Quaternion &q)
{
    x = q.x;
    y = q.y;
    z = q.z;
    w = q.w;
    return *this;
}

// Addition
inline Quaternion &Quaternion::operator+=(const Quaternion &q)
{
    return setGlm(glmComponents() + q.glmComponents());
}

// Addition
inline Quaternion &Quaternion::operator+=(float f)
{
    return setGlm(glmComponents() + f);
}

// Subtract
inline Quaternion &Quaternion::operator-=(const Quaternion &q)
{
    return setGlm(glmComponents() - q.glmComponents());
}

// Subtract
inline Quaternion &Quaternion::operator-=(float f)
{
    return setGlm(glmComponents() - f);
}

// Multiply
inline Quaternion &Quaternion::operator*=(float f)
{
    return setGlm(glmComponents() * f);
}

// Multiply
inline Quaternion &Quaternion::operator*=(const Quaternion &q)
{
    const auto left = glmValue();
    const auto right = q.glmValue();
    auto product = left * right;
    // Preserve the historical non-Hamilton y term. In particular, legacy
    // identity*(0,0,1,0) is (0,-2,1,0). Correcting that behavior is a separate
    // compatibility change, not part of replacing the arithmetic backend.
    product.y += 2.0f * (left.x - left.w) * right.z;
    return setGlm({product.x, product.y, product.z, product.w});
}

// Divide
inline Quaternion &Quaternion::operator/=(float f)
{
    return setGlm(storm::math::GlmVec4(storm::math::GlmDVec4(glmComponents()) * (1.0 / f)));
}

/*!\relates Quaternion
Addition
*/
inline Quaternion operator+(float f, const Quaternion &q)
{
    auto tq(q);
    tq += f;
    return tq;
}

/*!\relates Quaternion
Addition
*/
inline Quaternion operator+(const Quaternion &q, float f)
{
    auto tq(q);
    tq += f;
    return tq;
}

/*!\relates Quaternion
Addition
*/
inline Quaternion operator+(const Quaternion &q1, const Quaternion &q2)
{
    auto tq(q1);
    tq += q2;
    return tq;
}

/*!\relates Quaternion
Subtract
*/
inline Quaternion operator-(float f, const Quaternion &q)
{
    auto tq(q);
    tq -= f;
    return tq;
}

/*!\relates Quaternion
Subtract
*/
inline Quaternion operator-(const Quaternion &q, float f)
{
    auto tq(q);
    tq -= f;
    return tq;
}

/*!\relates Quaternion
Subtract
*/
inline Quaternion operator-(const Quaternion &q1, const Quaternion &q2)
{
    auto tq(q1);
    tq -= q2;
    return tq;
}

/*!\relates Quaternion
Multiply
*/
inline Quaternion operator*(float f, const Quaternion &q)
{
    auto tq(q);
    tq *= f;
    return tq;
}

/*!\relates Quaternion
Multiply
*/
inline Quaternion operator*(const Quaternion &q, float f)
{
    auto tq(q);
    tq *= f;
    return tq;
}

/*!\relates Quaternion
Multiply
*/
inline Quaternion operator*(const Quaternion &q1, const Quaternion &q2)
{
    auto tq(q1);
    tq *= q2;
    return tq;
}

/*!\relates Quaternion
Divide
*/
inline Quaternion operator/(const Quaternion &q, float f)
{
    auto tq(q);
    tq /= f;
    return tq;
}

// ===========================================================
// Filling
// ===========================================================

// Set identity quaternion
inline void Quaternion::SetIdentity()
{
    x = 0.0f;
    y = 0.0f;
    z = 0.0f;
    w = 1.0f;
}

// Construct from angles of rotation
inline void Quaternion::Set(float ax, float ay, float az)
{
    Set(Matrix(ax, ay, az));
}

// Construct from angles of rotation
inline void Quaternion::Set(const Vector &ang)
{
    Set(ang.x, ang.y, ang.z);
}

// Construct from component
inline void Quaternion::Set(float qx, float qy, float qz, float qw)
{
    x = qx;
    y = qy;
    z = qz;
    w = qw;
}

// Construct from matrix
inline void Quaternion::Set(const Matrix &mtx)
{
    const auto tr = mtx.m[0][0] + mtx.m[1][1] + mtx.m[2][2];
    if (tr > 0.0f)
    {
        double s = sqrt(tr + 1.0f);
        w = static_cast<float>(s * 0.5);
        s = 0.5 / s;
        x = static_cast<float>((mtx.m[1][2] - mtx.m[2][1]) * s);
        y = static_cast<float>((mtx.m[2][0] - mtx.m[0][2]) * s);
        z = static_cast<float>((mtx.m[0][1] - mtx.m[1][0]) * s);
    }
    else
    {
        int32_t i, j, k;
        if (mtx.m[1][1] > mtx.m[0][0])
        {
            if (mtx.m[2][2] > mtx.m[1][1])
            {
                i = 2;
                j = 0;
                k = 1;
            }
            else
            {
                i = 1;
                j = 2;
                k = 0;
            }
        }
        else if (mtx.m[2][2] > mtx.m[0][0])
        {
            i = 2;
            j = 0;
            k = 1;
        }
        else
        {
            i = 0;
            j = 1;
            k = 2;
        }
        auto s = sqrt((mtx.m[i][i] - (mtx.m[j][j] + mtx.m[k][k])) + 1.0);
        q[i] = static_cast<float>(s * 0.5);
        if (s != 0.0)
            s = 0.5 / s;
        q[j] = static_cast<float>((mtx.m[i][j] + mtx.m[j][i]) * s);
        q[k] = static_cast<float>((mtx.m[i][k] + mtx.m[k][i]) * s);
        q[3] = static_cast<float>((mtx.m[j][k] - mtx.m[k][j]) * s);
    }
}

// ===========================================================
// Transformation
// ===========================================================

// Normalize
inline Quaternion &Quaternion::Normalize()
{
    const auto value = storm::math::GlmDVec4(glmComponents());
    const double squared = glm::dot(value, value);
    if (squared <= 0.0) return *this;
    return setGlm(storm::math::GlmVec4(value * (1.0 / sqrt(squared))));
}

// Get coplanar quaternion
inline Quaternion &Quaternion::Conjugate()
{
    const auto result = glm::conjugate(glmValue());
    return setGlm({result.x, result.y, result.z, result.w});
}

// Invert quaternion
inline Quaternion &Quaternion::Inverse()
{
    Conjugate();
    Normalize();
    return *this;
}

// Spherical linear interpolation
inline Quaternion &Quaternion::SLerp(const Quaternion &q1, const Quaternion &q2, float kBlend)
{
    // The angle between the quaternions
    double cosomega = q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w;
    double k = 1.0f;
    // Nearest direction
    if (cosomega < 0.0f)
    {
        cosomega = -cosomega;
        k = -1.0f;
    }
    if (cosomega > 1.0)
        cosomega = 1.0;
    // Interpolation coefficients
    double k0, k1;
    if (1.0 - cosomega > 0.1)
    {
        // slerp
        const auto omega = acos(cosomega);
        const auto sinomega = 1.0 / sin(omega);
        k0 = sin(omega * (1.0 - kBlend)) * sinomega;
        k1 = sin(omega * kBlend) * sinomega;
    }
    else
    {
        // lerp
        k0 = 1.0 - kBlend;
        k1 = kBlend;
    }
    k0 *= k;
    // Interpolate
    // Retain the 0.9 threshold and first-endpoint sign policy above. GLM's
    // generic slerp uses different policies, so only blend algebra is delegated.
    const auto result = storm::math::GlmDVec4(q1.glmComponents()) * k0 +
                        storm::math::GlmDVec4(q2.glmComponents()) * k1;
    return setGlm(storm::math::GlmVec4(result));
}

// ===========================================================
// Utilities
// ===========================================================

// Get a matrix from a quaternion
inline void Quaternion::GetMatrix(Matrix &mtx) const
{
    storm::math::storeGlmMatrix(mtx.matrix, glm::mat4_cast(glmValue()));
}

// Get the length of the quaternion
inline float Quaternion::GetLength() const
{
    return glm::length(glmComponents());
}

// Get the square of the length of a quaternion
inline float Quaternion::GetLengthSqr() const
{
    return glm::dot(glmComponents(), glmComponents());
}

#pragma pack(pop)
