//============================================================================================
// Spirenkov Maxim, 2003
//============================================================================================
// Vector
//============================================================================================

#pragma once

#pragma pack(push)
#pragma pack()
#include <math.h>
#include <stdlib.h>
#include "../glm_backend.h"
#include <glm/common.hpp>
#include <glm/exponential.hpp>
#include <glm/vector_relational.hpp>

namespace storm::math
{
// GLM 1.0.3 implements unary minus as 0 - v, which yields +0 for +0 where the
// legacy code produced -0. Negate the components instead.
inline GlmVec2 negated(const GlmVec2 &value)
{
    return {-value.x, -value.y};
}
inline GlmVec3 negated(const GlmVec3 &value)
{
    return {-value.x, -value.y, -value.z};
}
inline GlmVec4 negated(const GlmVec4 &value)
{
    return {-value.x, -value.y, -value.z, -value.w};
}

// glm::dot sums four products pairwise, (x + y) + (z + w). The legacy code sums
// them left to right, which rounds differently.
template <typename T, glm::qualifier Q>
inline T sequentialDot(const glm::vec<4, T, Q> &a, const glm::vec<4, T, Q> &b)
{
    const auto products = a * b;
    return products.x + products.y + products.z + products.w;
}
} // namespace storm::math
#pragma pack(pop)

#pragma pack(push, 1)

// Class for representing a vector in 3D space
class Vector
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
        };

        // Array representation
        float v[3];
    };

    // -----------------------------------------------------------
    // Constructors
    // -----------------------------------------------------------
  public:
    // Value conversions keep the packed x/y/z representation unchanged.
    storm::math::GlmVec3 glmValue() const { return {x, y, z}; }
    static Vector fromGlm(const storm::math::GlmVec3 &value) { return {value.x, value.y, value.z}; }
    // Projection to the XZ plane used by the 2D helpers.
    storm::math::GlmVec2 glmXZ() const { return {x, z}; }

    // Empty constructor
    Vector();
    // Fill with number
    Vector(float f);
    // Fill with number
    Vector(double d);
    // Fill all components
    Vector(float x, float y, float z);
    // Fill all components
    Vector(const float f[3]);
    // Fill all components
    Vector(const double d[3]);
    // Copy constructor
    Vector(const Vector &v);

    // -----------------------------------------------------------
    // Operators
    // -----------------------------------------------------------
  public:
    // Find the square of the length of a vector
    float operator~() const;
    // Return normalized vector
    Vector operator!() const;

    // Get negative vector
    Vector operator-() const;

    // Assign
    Vector &operator=(float f);
    // Assign
    Vector &operator=(double d);
    // Assign
    Vector &operator=(const Vector &v);
    // per Component addition with assignment
    Vector &operator+=(float f);
    // per Component addition with assignment
    Vector &operator+=(double d);
    // per Component addition with assignment
    Vector &operator+=(const Vector &v);
    // per Component Subtraction with Assignment
    Vector &operator-=(float f);
    // per Component Subtraction with Assignment
    Vector &operator-=(double d);
    // per Component Subtraction with Assignment
    Vector &operator-=(const Vector &v);
    // per Component multiplication with assignment
    Vector &operator*=(float f);
    // per Component multiplication with assignment
    Vector &operator*=(double d);
    // per Component multiplication with assignment
    Vector &operator*=(const Vector &v);
    // per Component division with assignment
    Vector &operator/=(float f);
    // per Component division with assignment
    Vector &operator/=(double d);
    // per Component division with assignment
    Vector &operator/=(const Vector &v);

    // Scalar multiplication, the result is copied to all components
    Vector &operator|=(const Vector &v);
    // Vector multiplication
    Vector &operator^=(const Vector &v);

    // -----------------------------------------------------------
    // Transformation
    // -----------------------------------------------------------
  public:
    // Normalize the vector, and return it to its former length
    float Normalize();
    // Project to XZ Plane and Normalize
    float Normalize2D();

    // Limit the length of the vector, and return the current length
    float ClampLength(float clampValue);

    // Limit X component to range
    Vector &ClampX(float min, float max);
    // Constrain Y component to range
    Vector &ClampY(float min, float max);
    // Constrain Z component to range
    Vector &ClampZ(float min, float max);
    // Limit components to a range
    Vector &Clamp(float min, float max);

    // Keep vector minimal components
    Vector &Min(const Vector &v);
    // Keep vector maximum components
    Vector &Max(const Vector &v);

    // -----------------------------------------------------------
    // Utilities
    // -----------------------------------------------------------
  public:
    // Set new values
    Vector &Set(float x, float y, float z);
    // Set new values
    Vector &Set(const Vector &v);

    // Get the angle between vectors
    float GetAngle(const Vector &v) const;
    // Get the sign angle between vectors in the XZ plane
    float GetAngle2D(const Vector &v) const;
    // Get the angle of rotation of a vector around the Y axis
    float GetAY(float defAngle = 0.0f) const;

    // Get sine between 2D vectors in XZ plane
    float Sin2D(const Vector &v) const;
    // Get cosine between 2D vectors in XZ plane
    float Cos2D(const Vector &v) const;
    // Get the cross product of vectors in the XZ plane
    float Cross2D(const Vector &v) const;

    // Get a vector in the XZ plane
    Vector Get2D() const;
    // Get a vector in the XZ plane
    Vector Get2D(float y) const;
    // Generate vector in XZ plane by angle
    Vector &Make2D(float ay);

    // Get vector length
    float GetLength() const;
    // Get vector length in 2D
    float GetLength2D() const;
    // Get the squared length of a vector in 2D
    float GetLength2D2() const;

    // Rotate a vector in the XZ plane by an angle
    Vector &Rotate(float angle);
    // Rotate the vector in the XZ plane by an angle specified by cos, sin
    Vector &Rotate(float vcos, float vsin);
    // Rotate the vector clockwise in the XZ plane by an angle PI/2
    Vector &Rotate_PI2_CW();
    // Rotate the vector counterclockwise in the XZ plane by an angle PI/2
    Vector &Rotate_PI2_CCW();

    // Calculate linearly interpolated value
    Vector &Lerp(const Vector &v1, const Vector &v2, float kBlend);

    // Calculate the reflected vector
    Vector &Reflection(const Vector &normal);

    // Fill with a unit vector with a random direction
    Vector &Rand();
    // Fill with unit vector with random direction in XZ
    Vector &Rand2D();
    // Fill with random values in a given ABB
    Vector &Rand(const Vector &min, const Vector &max);
    // Fill with random values in a given sphere
    Vector &Rand(const Vector &pos, float radius);

    // Move the current vector to the specified point by the specified step
    bool MoveByStep(const Vector &to, float step);
    // Move the current vector to a given point by a given step in 2D
    bool MoveByStep2D(const Vector &to, float step);

    // The point is in the box
    bool InBox(const Vector &min, const Vector &max) const;
    // The point is in the sphere
    bool InSphere(const Vector &pos, float rad) const;
};

// ===========================================================
// Constructors
// ===========================================================

// Empty constructor
inline Vector::Vector() = default;

// Fill with number
inline Vector::Vector(float f)
{
    x = f;
    y = f;
    z = f;
}

// Fill with number
inline Vector::Vector(double d)
{
    x = static_cast<float>(d);
    y = static_cast<float>(d);
    z = static_cast<float>(d);
}

// Fill all components
inline Vector::Vector(float x, float y, float z)
{
    this->x = x;
    this->y = y;
    this->z = z;
}

// Fill all components
inline Vector::Vector(const float f[3])
{
    x = f[0];
    y = f[1];
    z = f[2];
}

// Fill all components
inline Vector::Vector(const double d[3])
{
    x = static_cast<float>(d[0]);
    y = static_cast<float>(d[1]);
    z = static_cast<float>(d[2]);
}

// Copy constructor
inline Vector::Vector(const Vector &v)
{
    x = v.x;
    y = v.y;
    z = v.z;
}

// ===========================================================
// Operators
// ===========================================================

// Find the square of the length of a vector
inline float Vector::operator~() const
{
    return glm::dot(glmValue(), glmValue());
}

// Return normalized vector
inline Vector Vector::operator!() const
{
    auto v(*this);
    auto length = v.Normalize();
    // assert(fabsf(length) > 1e-20);
    return v;
}

inline Vector Vector::operator-() const
{
    return fromGlm(storm::math::negated(glmValue()));
}

// Assign
inline Vector &Vector::operator=(float f)
{
    x = f;
    y = f;
    z = f;
    return *this;
}

// Assign
inline Vector &Vector::operator=(double d)
{
    x = static_cast<float>(d);
    y = static_cast<float>(d);
    z = static_cast<float>(d);
    return *this;
}

// Assign
inline Vector &Vector::operator=(const Vector &v)
{
    x = v.x;
    y = v.y;
    z = v.z;
    return *this;
}

// per Component addition with assignment
inline Vector &Vector::operator+=(float f)
{
    *this = fromGlm(glmValue() + f);
    return *this;
}

// per Component addition with assignment
inline Vector &Vector::operator+=(double d)
{
    *this = fromGlm(glmValue() + static_cast<float>(d));
    return *this;
}

// per Component addition with assignment
inline Vector &Vector::operator+=(const Vector &v)
{
    *this = fromGlm(glmValue() + v.glmValue());
    return *this;
}

// per Component Subtraction with Assignment
inline Vector &Vector::operator-=(float f)
{
    *this = fromGlm(glmValue() - f);
    return *this;
}

// per Component Subtraction with Assignment
inline Vector &Vector::operator-=(double d)
{
    *this = fromGlm(glmValue() - static_cast<float>(d));
    return *this;
}

// per Component Subtraction with Assignment
inline Vector &Vector::operator-=(const Vector &v)
{
    *this = fromGlm(glmValue() - v.glmValue());
    return *this;
}

// per Component multiplication with assignment
inline Vector &Vector::operator*=(float f)
{
    *this = fromGlm(glmValue() * f);
    return *this;
}

// per Component multiplication with assignment
inline Vector &Vector::operator*=(double d)
{
    *this = fromGlm(glmValue() * static_cast<float>(d));
    return *this;
}

// per Component multiplication with assignment
inline Vector &Vector::operator*=(const Vector &v)
{
    *this = fromGlm(glmValue() * v.glmValue());
    return *this;
}

// per Component division with assignment
inline Vector &Vector::operator/=(float f)
{
    *this = fromGlm(storm::math::GlmVec3(storm::math::GlmDVec3(glmValue()) * (1.0 / f)));
    return *this;
}

// per Component division with assignment
inline Vector &Vector::operator/=(double d)
{
    *this = fromGlm(storm::math::GlmVec3(storm::math::GlmDVec3(glmValue()) * (1.0 / d)));
    return *this;
}

// per Component division with assignment
inline Vector &Vector::operator/=(const Vector &v)
{
    *this = fromGlm(glmValue() / v.glmValue());
    return *this;
}

// Scalar multiplication, the result is copied to all components
inline Vector &Vector::operator|=(const Vector &v)
{
    x = y = z = glm::dot(v.glmValue(), glmValue());
    return *this;
}

// Vector multiplication
inline Vector &Vector::operator^=(const Vector &v)
{
    *this = fromGlm(glm::cross(glmValue(), v.glmValue()));
    return *this;
}

/*!\relates Vector
Addition
*/
inline Vector operator+(const Vector &v, float f)
{
    auto tv(v);
    tv += f;
    return tv;
}

/*!\relates Vector
Addition
*/
inline Vector operator+(float f, const Vector &v)
{
    auto tv(v);
    tv += f;
    return tv;
}

/*!\relates Vector
Addition
*/
inline Vector operator+(const Vector &v, double d)
{
    auto tv(v);
    tv += d;
    return tv;
}

/*!\relates Vector
Addition
*/
inline Vector operator+(double d, const Vector &v)
{
    auto tv(v);
    tv += d;
    return tv;
}

/*!\relates Vector
Addition
*/
inline Vector operator+(const Vector &v1, const Vector &v2)
{
    auto tv(v1);
    tv += v2;
    return tv;
}

/*!\relates Vector
Subtract
*/
inline Vector operator-(const Vector &v, float f)
{
    auto tv(v);
    tv -= f;
    return tv;
}

/*!\relates Vector
Subtract
*/
inline Vector operator-(float f, const Vector &v)
{
    auto tv(v);
    tv -= f;
    return tv;
}

/*!\relates Vector
Subtract
*/
inline Vector operator-(const Vector &v, double d)
{
    auto tv(v);
    tv -= d;
    return tv;
}

/*!\relates Vector
Subtract
*/
inline Vector operator-(double d, const Vector &v)
{
    auto tv(v);
    tv -= d;
    return tv;
}

/*!\relates Vector
Subtract
*/
inline Vector operator-(const Vector &v1, const Vector &v2)
{
    auto tv(v1);
    tv -= v2;
    return tv;
}

/*!\relates Vector
Multiply
*/
inline Vector operator*(const Vector &v, float f)
{
    auto tv(v);
    tv *= f;
    return tv;
}

/*!\relates Vector
Multiply
*/
inline Vector operator*(float f, const Vector &v)
{
    auto tv(v);
    tv *= f;
    return tv;
}

/*!\relates Vector
Multiply
*/
inline Vector operator*(const Vector &v, double d)
{
    auto tv(v);
    tv *= d;
    return tv;
}

/*!\relates Vector
Multiply
*/
inline Vector operator*(double d, const Vector &v)
{
    auto tv(v);
    tv *= d;
    return tv;
}

/*!\relates Vector
Multiply
*/
inline Vector operator*(const Vector &v1, const Vector &v2)
{
    auto tv(v1);
    tv *= v2;
    return tv;
}

/*!\relates Vector
Divide
*/
inline Vector operator/(const Vector &v, float f)
{
    auto tv(v);
    tv /= f;
    return tv;
}

/*!\relates Vector
Divide
*/
inline Vector operator/(float f, const Vector &v)
{
    Vector tv(f);
    tv /= v;
    return tv;
}

/*!\relates Vector
Divide
*/
inline Vector operator/(const Vector &v, double d)
{
    auto tv(v);
    tv /= d;
    return tv;
}

/*!\relates Vector
Divide
*/
inline Vector operator/(double d, const Vector &v)
{
    Vector tv(d);
    tv /= v;
    return tv;
}

/*!\relates Vector
Divide
*/
inline Vector operator/(const Vector &v1, const Vector &v2)
{
    auto tv(v1);
    tv /= v2;
    return tv;
}

/*!\relates Vector
Scalar multiplication
*/
inline float operator|(const Vector &v1, const Vector &v2)
{
    return glm::dot(v1.glmValue(), v2.glmValue());
}

/*!\relates Vector
Vector multiplication
*/
inline Vector operator^(const Vector &v1, const Vector &v2)
{
    return Vector::fromGlm(glm::cross(v1.glmValue(), v2.glmValue()));
}

/*!\relates Vector
Compare component by component
*/
inline bool operator>(const Vector &v1, const Vector &v2)
{
    return glm::all(glm::greaterThan(v1.glmValue(), v2.glmValue()));
}

/*!\relates Vector
Compare component by component
*/
inline bool operator>=(const Vector &v1, const Vector &v2)
{
    return glm::all(glm::greaterThanEqual(v1.glmValue(), v2.glmValue()));
}

/*!\relates Vector
Compare component by component
*/
inline bool operator<(const Vector &v1, const Vector &v2)
{
    return glm::all(glm::lessThan(v1.glmValue(), v2.glmValue()));
}

/*!\relates Vector
Compare component by component
*/
inline bool operator<=(const Vector &v1, const Vector &v2)
{
    return glm::all(glm::lessThanEqual(v1.glmValue(), v2.glmValue()));
}

/*!\relates Vector
Compare component by component
*/
inline bool operator==(const Vector &v1, const Vector &v2)
{
    return glm::all(glm::lessThan(glm::abs(v1.glmValue() - v2.glmValue()), storm::math::GlmVec3(1e-34f)));
}

/*!\relates Vector
Compare component by component
*/
inline bool operator!=(const Vector &v1, const Vector &v2)
{
    return !(v1 == v2);
}

// ===========================================================
// Transformation
// ===========================================================

// Normalize the vector, and return it to its former length
inline float Vector::Normalize()
{
    const auto value = storm::math::GlmDVec3(glmValue());
    const double length = glm::length(value);
    if (length > 0.0) *this = fromGlm(storm::math::GlmVec3(value * (1.0 / length)));
    return static_cast<float>(length);
}

// Project to XZ Plane and Normalize
inline float Vector::Normalize2D()
{
    y = 0.0f;
    const auto value = storm::math::GlmDVec2(static_cast<double>(x), static_cast<double>(z));
    const double length = glm::length(value);
    if (length > 0.0)
    {
        const auto normalized = value * (1.0 / length);
        x = static_cast<float>(normalized.x);
        z = static_cast<float>(normalized.y);
    }
    return static_cast<float>(length);
}

// Limit the length of the vector, and return the current length
inline float Vector::ClampLength(float clampValue)
{
    if (clampValue <= 0.0f)
    {
        x = y = z = 0.0f;
        return 0.0f;
    }
    // The squared length is deliberately accumulated in float, as before.
    const double len = sqrt(glm::dot(glmValue(), glmValue()));
    if (len > clampValue)
    {
        const auto k = clampValue / len;
        *this = fromGlm(storm::math::GlmVec3(storm::math::GlmDVec3(glmValue()) * k));
        return clampValue;
    }
    return static_cast<float>(len);
}

// Limit X component to range
inline Vector &Vector::ClampX(float min, float max)
{
    x = glm::clamp(x, min, max);
    return *this;
}

// Constrain Y component to range
inline Vector &Vector::ClampY(float min, float max)
{
    y = glm::clamp(y, min, max);
    return *this;
}

// Constrain Z component to range
inline Vector &Vector::ClampZ(float min, float max)
{
    z = glm::clamp(z, min, max);
    return *this;
}

// Limit components to a range
inline Vector &Vector::Clamp(float min, float max)
{
    *this = fromGlm(glm::clamp(glmValue(), min, max));
    return *this;
}

// Keep vector minimal components
inline Vector &Vector::Min(const Vector &v)
{
    *this = fromGlm(glm::min(glmValue(), v.glmValue()));
    return *this;
}

// Keep vector maximum components
inline Vector &Vector::Max(const Vector &v)
{
    *this = fromGlm(glm::max(glmValue(), v.glmValue()));
    return *this;
}

// ===========================================================
// Utilities
// ===========================================================

// Set new values
inline Vector &Vector::Set(float x, float y, float z)
{
    this->x = x;
    this->y = y;
    this->z = z;
    return *this;
}

// Set new values
inline Vector &Vector::Set(const Vector &v)
{
    x = v.x;
    y = v.y;
    z = v.z;
    return *this;
}

// Get the angle between vectors
inline float Vector::GetAngle(const Vector &v) const
{
    // Historical behaviour: the squared length of this vector is used twice
    // (the length of v never enters the normalization).
    const auto self = storm::math::GlmDVec3(glmValue());
    auto len = glm::dot(self, self);
    len *= glm::dot(self, self);
    if (len <= 0.0)
        return 0.0f;
    const auto cs = glm::clamp(glm::dot(glmValue(), v.glmValue()) / sqrt(len), -1.0, 1.0);
    return static_cast<float>(acos(cs));
}

// Get the sign angle between vectors in the XZ plane
inline float Vector::GetAngle2D(const Vector &v) const
{
    const auto self = storm::math::GlmDVec2(glmXZ());
    const auto other = storm::math::GlmDVec2(v.glmXZ());
    auto len = glm::dot(self, self);
    len *= glm::dot(other, other);
    if (len <= 0.0)
        return 0.0f;
    len = acos(glm::clamp(glm::dot(glmXZ(), v.glmXZ()) / sqrt(len), -1.0, 1.0));
    if (Cross2D(v) < 0)
        len = -len;
    return static_cast<float>(len);
}

// Get the angle of rotation of a vector around the Y axis
inline float Vector::GetAY(float defAngle) const
{
    const auto self = storm::math::GlmDVec2(glmXZ());
    auto len = glm::dot(self, self);
    if (len > 0.00000000001)
        len = acos(glm::clamp(z / sqrt(len), -1.0, 1.0));
    else
        return defAngle;
    if (x < 0)
        len = -len;
    return static_cast<float>(len);
}

// Get sine between 2D vectors in XZ plane
inline float Vector::Sin2D(const Vector &v) const
{
    const auto self = storm::math::GlmDVec2(glmXZ());
    const auto other = storm::math::GlmDVec2(v.glmXZ());
    auto len = glm::dot(self, self);
    len *= glm::dot(other, other);
    if (len <= 0.0)
        return 0.0f;
    len = Cross2D(v) / sqrt(len);
    return static_cast<float>(len);
}

// Get cosine between 2D vectors in XZ plane
inline float Vector::Cos2D(const Vector &v) const
{
    const auto self = storm::math::GlmDVec2(glmXZ());
    const auto other = storm::math::GlmDVec2(v.glmXZ());
    auto len = glm::dot(self, self);
    len *= glm::dot(other, other);
    if (len <= 0.0)
        return 1.0f;
    len = glm::dot(glmXZ(), v.glmXZ()) / sqrt(len);
    return static_cast<float>(len);
}

// Get the cross product of vectors in the XZ plane
inline float Vector::Cross2D(const Vector &v) const
{
    return z * v.x - x * v.z;
}

// Get a vector in the XZ plane
inline Vector Vector::Get2D() const
{
    return Vector(x, 0.0f, z);
}

// Get a vector in the XZ plane
inline Vector Vector::Get2D(float y) const
{
    return Vector(x, y, z);
}

// Generate vector in XZ plane by angle
inline Vector &Vector::Make2D(float ay)
{
    x = sinf(ay);
    y = 0.0f;
    z = cosf(ay);
    return *this;
}

// Get vector length
inline float Vector::GetLength() const
{
    return static_cast<float>(glm::length(storm::math::GlmDVec3(glmValue())));
}

// Get vector length in 2D
inline float Vector::GetLength2D() const
{
    return static_cast<float>(glm::length(storm::math::GlmDVec2(static_cast<double>(x), static_cast<double>(z))));
}

// Get the squared length of a vector in 2D
inline float Vector::GetLength2D2() const
{
    const auto value = storm::math::GlmVec2(x, z);
    return glm::dot(value, value);
}

// Rotate a vector in the XZ plane by an angle
inline Vector &Vector::Rotate(float angle)
{
    return Rotate(cosf(angle), sinf(angle));
}

// Rotate the vector in the XZ plane by an angle specified by cos, sin
inline Vector &Vector::Rotate(float vcos, float vsin)
{
    // x' = x * cos + z * sin, z' = z * cos - x * sin
    const auto rotated = glmXZ() * vcos + storm::math::GlmVec2(z, -x) * vsin;
    x = rotated.x;
    z = rotated.y;
    return *this;
}

// Rotate the vector clockwise in the XZ plane by an angle PI / 2
inline Vector &Vector::Rotate_PI2_CW()
{
    const auto t = x;
    x = z;
    z = -t;
    return *this;
}

// Rotate the vector counterclockwise in the XZ plane by an angle PI / 2
inline Vector &Vector::Rotate_PI2_CCW()
{
    const auto t = x;
    x = -z;
    z = t;
    return *this;
}

// Calculate linearly interpolated value
inline Vector &Vector::Lerp(const Vector &v1, const Vector &v2, float kBlend)
{
    *this = fromGlm(v1.glmValue() + (v2.glmValue() - v1.glmValue()) * kBlend);
    return *this;
}

// Calculate the reflected vector
inline Vector &Vector::Reflection(const Vector &normal)
{
    // Not glm::reflect: it evaluates (N * dot) * 2, which rounds differently
    // from the legacy N * (-2 * dot) when a product is subnormal or overflows.
    const auto k = -2.0f * glm::dot(glmValue(), normal.glmValue());
    *this = fromGlm(glmValue() + normal.glmValue() * k);
    return *this;
}

// Fill with a unit vector with a random direction
inline Vector &Vector::Rand()
{
    Make2D(rand() * ((2.0f * 3.141592654f) / RAND_MAX));
    y = z;
    z = 0.0f;
    return Rotate(rand() * ((2.0f * 3.141592654f) / RAND_MAX));
}

// Fill with unit vector with random direction in XZ
inline Vector &Vector::Rand2D()
{
    return Make2D(rand() * ((2.0f * 3.141592654f) / RAND_MAX));
}

// Fill with random values in a given ABB
inline Vector &Vector::Rand(const Vector &min, const Vector &max)
{
    // rand() is consumed in x, y, z order.
    const auto rx = static_cast<float>(rand());
    const auto ry = static_cast<float>(rand());
    const auto rz = static_cast<float>(rand());
    *this = fromGlm(min.glmValue() +
                    storm::math::GlmVec3(rx, ry, rz) * ((max.glmValue() - min.glmValue()) * (1.0f / RAND_MAX)));
    return *this;
}

// Fill with random values in a given sphere
inline Vector &Vector::Rand(const Vector &pos, float radius)
{
    Rand();
    *this *= rand() * (radius * (1.0f / RAND_MAX));
    return *this;
}

// Move the current vector to the specified point by the specified step
inline bool Vector::MoveByStep(const Vector &to, float step)
{
    if (step <= 0.0f)
        return false;
    // Distance to point
    const auto target = to.glmValue();
    const auto delta = target - glmValue();
    auto dist = glm::dot(delta, delta);
    if (dist < 1e-20f)
    {
        *this = fromGlm(target);
        return false;
    }
    dist = sqrtf(dist);
    // Moving to a point
    if (step >= dist)
    {
        *this = fromGlm(target);
        return true;
    }
    step /= dist;
    *this = fromGlm(glmValue() + delta * step);
    return true;
}

// Move the current vector to a given point by a given step in 2D
inline bool Vector::MoveByStep2D(const Vector &to, float step)
{
    if (step <= 0.0f)
        return false;
    // Distance to point
    const auto target = to.glmXZ();
    const auto delta = target - glmXZ();
    auto dist = glm::dot(delta, delta);
    if (dist < 1e-20f)
    {
        x = target.x;
        z = target.y;
        return false;
    }
    dist = sqrtf(dist);
    // Moving to a point
    if (step >= dist)
    {
        x = target.x;
        z = target.y;
        return true;
    }
    step /= dist;
    const auto moved = glmXZ() + delta * step;
    x = moved.x;
    z = moved.y;
    return true;
}

// The point is in the box
inline bool Vector::InBox(const Vector &min, const Vector &max) const
{
    if (y < min.y || y > max.y)
        return false;
    if (x < min.x || x > max.x)
        return false;
    if (z < min.z || z > max.z)
        return false;
    return true;
}

// The point is in the sphere
inline bool Vector::InSphere(const Vector &pos, float rad) const
{
    // Float squares accumulated in double, as the legacy code did.
    const auto delta = glmValue() - pos.glmValue();
    const auto squares = storm::math::GlmDVec3(delta * delta);
    const double d = squares.x + squares.y + squares.z;
    return d < static_cast<double>(rad) * static_cast<double>(rad);
}

#pragma pack(pop)
