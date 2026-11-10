//============================================================================================
// Spirenkov Maxim, 2003
//============================================================================================
// Vector4
//============================================================================================

#pragma once

#include "vector.h"

#pragma pack(push, 1)

// Class for representing a vector with a weighting coefficient in 3D space
struct Vector4
{
  public:
    union {
        struct
        {
            // The X component
            float x;
            // The Y component
            float y;
            // The Z component
            float z;
            // Weight component
            float w;
        };

        struct
        {
            // Array representation
            float v4[4];
        };
    };

    // -----------------------------------------------------------
    // Constructors
    // -----------------------------------------------------------
  public:
    // Value conversions keep the packed x/y/z/w representation unchanged.
    storm::math::GlmVec4 glmValue() const { return {x, y, z, w}; }
    static Vector4 fromGlm(const storm::math::GlmVec4 &value) { return {value.x, value.y, value.z, value.w}; }

    // Empty constructor
    Vector4();
    // Fill with number
    Vector4(float f);
    // Fill with number
    Vector4(double d);
    // Fill 3 components, 1
    Vector4(float x, float y, float z);
    // Fill all components
    Vector4(float x, float y, float z, float w);
    // Fill 3 components, 1
    Vector4(const float f[3]);
    // Fill 3 components, 1
    Vector4(const double d[3]);
    // Fill 3 components, 1
    Vector4(const Vector &v);
    // Copy constructor
    Vector4(const Vector4 &v);

    // -----------------------------------------------------------
    // Operators
    // -----------------------------------------------------------
  public:
    // Find the square of the length of a vector
    float operator~() const;

    // Get negative vector
    Vector4 operator-() const;

    // Assign
    Vector4 &operator=(float f);
    // Assign
    Vector4 &operator=(double d);
    // Assign
    Vector4 &operator=(const Vector &v);
    // Assign
    Vector4 &operator=(const Vector4 &v);

    // -----------------------------------------------------------
    // Transformation
    // -----------------------------------------------------------
  public:
    // Normalize w component
    void Normalize();
};

// ===========================================================
// Constructors
// ===========================================================

// Empty constructor
inline Vector4::Vector4()
{
}

// Fill with number
inline Vector4::Vector4(float f)
{
    x = f;
    y = f;
    z = f;
    w = f;
}

// Fill with number
inline Vector4::Vector4(double d)
{
    w = static_cast<float>(d);
}

// Fill 3 components, 1
inline Vector4::Vector4(float x, float y, float z)
{
    this->x = x;
    this->y = y;
    this->z = z;
    w = 1.0f;
}

// Fill all components
inline Vector4::Vector4(float x, float y, float z, float w)
{
    this->x = x;
    this->y = y;
    this->z = z;
    this->w = w;
}

// Fill 3 components, 1
inline Vector4::Vector4(const float f[3])
{
    x = f[0];
    y = f[1];
    z = f[2];
    w = 1.0f;
}

// Fill 3 components, 1
inline Vector4::Vector4(const double d[3])
{
    x = static_cast<float>(d[0]);
    y = static_cast<float>(d[1]);
    z = static_cast<float>(d[2]);
    w = 1.0f;
}

// Fill 3 components, 1
inline Vector4::Vector4(const Vector &vc)
{
    x = vc.x;
    y = vc.y;
    z = vc.z;
    w = 1.0f;
}

// Copy constructor
inline Vector4::Vector4(const Vector4 &v)
{
    x = v.x;
    y = v.y;
    z = v.z;
    w = v.w;
}

// ===========================================================
// Operators
// ===========================================================

// Find the square of the length of a vector
inline float Vector4::operator~() const
{
    return storm::math::sequentialDot(glmValue(), glmValue());
}

// Get negative vector
inline Vector4 Vector4::operator-() const
{
    return fromGlm(storm::math::negated(glmValue()));
}

// Assign
inline Vector4 &Vector4::operator=(float f)
{
    x = f;
    y = f;
    z = f;
    w = 1.0f;
    return *this;
}

// Assign
inline Vector4 &Vector4::operator=(double d)
{
    x = static_cast<float>(d);
    y = static_cast<float>(d);
    z = static_cast<float>(d);
    w = 1.0f;
    return *this;
}

// Assign
inline Vector4 &Vector4::operator=(const Vector &v)
{
    x = v.x;
    y = v.y;
    z = v.z;
    w = 1.0f;
    return *this;
}

// Assign
inline Vector4 &Vector4::operator=(const Vector4 &v)
{
    x = v.x;
    y = v.y;
    z = v.z;
    w = v.w;
    return *this;
}

// ===========================================================
// Transformation
// ===========================================================

// Normalize w component
inline void Vector4::Normalize()
{
    const auto k = 1.0 / w;
    const auto scaled = storm::math::GlmVec3(storm::math::GlmDVec3(x, y, z) * k);
    x = scaled.x;
    y = scaled.y;
    z = scaled.z;
    w = 1.0f;
}

#pragma pack(pop)
