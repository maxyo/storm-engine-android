#pragma once

#pragma pack(push)
#pragma pack()
#include <math.h>
#include <cstddef>
#include <type_traits>
#include "glm_backend.h"
#pragma pack(pop)

struct CVECTOR
{
  public:
    union {
        struct
        {
            float x, y, z;
        };

        float v[3];
    };

    CVECTOR() = default;

    CVECTOR(float a)
    {
        x = a;
        y = a;
        z = a;
    };

    CVECTOR(double a)
    {
        x = static_cast<float>(a);
        y = static_cast<float>(a);
        z = static_cast<float>(a);
    };

    CVECTOR(float _x, float _y, float _z)
    {
        x = _x;
        y = _y;
        z = _z;
    };

    CVECTOR(double _x, double _y, double _z)
    {
        x = static_cast<float>(_x);
        y = static_cast<float>(_y);
        z = static_cast<float>(_z);
    };

    CVECTOR(float a[3])
    {
        x = a[0];
        y = a[1];
        z = a[2];
    };

    CVECTOR(double a[3])
    {
        x = static_cast<float>(a[0]);
        y = static_cast<float>(a[1]);
        z = static_cast<float>(a[2]);
    };
    // CVECTOR(const DVECTOR &d) : x((float)d.x), y((float)d.y), z((float)d.z){};

    // Explicit value conversion preserves this legacy type's public binary layout.
    storm::math::GlmVec3 glmValue() const { return {x, y, z}; }
    static CVECTOR fromGlm(const storm::math::GlmVec3 &value) { return {value.x, value.y, value.z}; }

    CVECTOR &operator+=(const CVECTOR &v)
    {
        *this = fromGlm(glmValue() + v.glmValue());
        return *this;
    };

    CVECTOR &operator-=(const CVECTOR &v)
    {
        *this = fromGlm(glmValue() - v.glmValue());
        return *this;
    };

    CVECTOR &operator*=(const CVECTOR &v)
    {
        *this = fromGlm(glmValue() * v.glmValue());
        return *this;
    };

    CVECTOR &operator/=(const CVECTOR &v)
    {
        *this = fromGlm(glmValue() / v.glmValue());
        return *this;
    };

    // square magnitude
    friend float operator~(const CVECTOR &v)
    {
        return glm::dot(v.glmValue(), v.glmValue());
    };

    float GetLength() const
    {
        return static_cast<float>(glm::length(storm::math::GlmDVec3(glmValue())));
    }

    float GetLength2D() const
    {
        return static_cast<float>(glm::length(storm::math::GlmDVec2(static_cast<double>(x), static_cast<double>(z))));
    }

    // normalize
    friend CVECTOR operator!(const CVECTOR &v)
    {
        // Preserve legacy float squared-length, then double reciprocal/multiply.
        const double squared = glm::dot(v.glmValue(), v.glmValue());
        if (squared == 0.0) return CVECTOR(0.0f);
        return fromGlm(storm::math::GlmVec3(storm::math::GlmDVec3(v.glmValue()) * (1.0 / sqrt(squared))));
    };

    // none
    friend CVECTOR operator+(const CVECTOR &v)
    {
        return v;
    };
    // add
    friend CVECTOR operator+(const CVECTOR &v1, const CVECTOR &v2)
    {
        return fromGlm(v1.glmValue() + v2.glmValue());
    };
    // negative
    friend CVECTOR operator-(const CVECTOR &v)
    {
        return fromGlm(-v.glmValue());
    };
    // sub
    friend CVECTOR operator-(const CVECTOR &v1, const CVECTOR &v2)
    {
        return fromGlm(v1.glmValue() - v2.glmValue());
    };
    // multiply each element by each
    friend CVECTOR operator*(const CVECTOR &v1, const CVECTOR &v2)
    {
        return fromGlm(v1.glmValue() * v2.glmValue());
    };
    // multiply each element by float
    friend CVECTOR operator*(const CVECTOR &v1, float f)
    {
        return fromGlm(v1.glmValue() * f);
    };
    // divide each element by each
    friend CVECTOR operator/(const CVECTOR &v1, const CVECTOR &v2)
    {
        return fromGlm(v1.glmValue() / v2.glmValue());
    };
    // divide each element by float
    friend CVECTOR operator/(const CVECTOR &v1, float f)
    {
        return fromGlm(storm::math::GlmVec3(storm::math::GlmDVec3(v1.glmValue()) * (1.0 / f)));
    };
    // cross product
    friend CVECTOR operator^(const CVECTOR &v1, const CVECTOR &v2)
    {
        return fromGlm(glm::cross(v1.glmValue(), v2.glmValue()));
    }

    // dot product
    friend float operator|(const CVECTOR &v1, const CVECTOR &v2)
    {
        return glm::dot(v1.glmValue(), v2.glmValue());
    }
};

struct DVECTOR
{
  public:
    double x;
    double y;
    double z;

    DVECTOR(){};

    DVECTOR(double a)
    {
        x = a;
        y = a;
        z = a;
    };

    DVECTOR(double _x, double _y, double _z)
    {
        x = _x;
        y = _y;
        z = _z;
    };

    DVECTOR(double a[3])
    {
        x = a[0];
        y = a[1];
        z = a[2];
    };

    DVECTOR(const CVECTOR &d)
    {
        x = d.x;
        y = d.y;
        z = d.z;
    };

    // Explicit value conversion preserves this legacy type's public binary layout.
    storm::math::GlmDVec3 glmValue() const { return {x, y, z}; }
    static DVECTOR fromGlm(const storm::math::GlmDVec3 &value) { return {value.x, value.y, value.z}; }

    DVECTOR &operator+=(const DVECTOR &v)
    {
        *this = fromGlm(glmValue() + v.glmValue());
        return *this;
    };

    DVECTOR &operator-=(const DVECTOR &v)
    {
        *this = fromGlm(glmValue() - v.glmValue());
        return *this;
    };

    DVECTOR &operator*=(const DVECTOR &v)
    {
        *this = fromGlm(glmValue() * v.glmValue());
        return *this;
    };

    DVECTOR &operator/=(const DVECTOR &v)
    {
        *this = fromGlm(glmValue() / v.glmValue());
        return *this;
    };

    // square magnitude
    friend double operator~(const DVECTOR &v)
    {
        return glm::dot(v.glmValue(), v.glmValue());
    };

    // normalize
    friend DVECTOR operator!(const DVECTOR &v)
    {
        // The legacy double vector has no zero-vector guard (NaN is preserved).
        return fromGlm(glm::normalize(v.glmValue()));
    };

    // none
    friend DVECTOR operator+(const DVECTOR &v)
    {
        return v;
    };
    // add
    friend DVECTOR operator+(const DVECTOR &v1, const DVECTOR &v2)
    {
        return fromGlm(v1.glmValue() + v2.glmValue());
    };
    // negative
    friend DVECTOR operator-(const DVECTOR &v)
    {
        return fromGlm(-v.glmValue());
    };
    // sub
    friend DVECTOR operator-(const DVECTOR &v1, const DVECTOR &v2)
    {
        return fromGlm(v1.glmValue() - v2.glmValue());
    };
    // multiply each element by each
    friend DVECTOR operator*(const DVECTOR &v1, const DVECTOR &v2)
    {
        return fromGlm(v1.glmValue() * v2.glmValue());
    };
    // multiply each element by double
    friend DVECTOR operator*(const DVECTOR &v1, double f)
    {
        return fromGlm(v1.glmValue() * f);
    };
    // divide each element by each
    friend DVECTOR operator/(const DVECTOR &v1, const DVECTOR &v2)
    {
        return fromGlm(v1.glmValue() / v2.glmValue());
    };
    // divide each element by double
    friend DVECTOR operator/(const DVECTOR &v1, double f)
    {
        return fromGlm(v1.glmValue() * (1.0 / f));
    };
    // cross product
    friend DVECTOR operator^(const DVECTOR &v1, const DVECTOR &v2)
    {
        return fromGlm(glm::cross(v1.glmValue(), v2.glmValue()));
    }

    // dot product
    friend double operator|(const DVECTOR &v1, const DVECTOR &v2)
    {
        return glm::dot(v1.glmValue(), v2.glmValue());
    }
};

// Raw animation vertices and SaveVector persist these exact representations.
// Legacy callers can lower alignment with pragma pack; retain that ABI context.
static_assert(std::is_standard_layout_v<CVECTOR> && std::is_trivially_copyable_v<CVECTOR>);
static_assert(sizeof(CVECTOR) == 12 && alignof(CVECTOR) <= alignof(float));
static_assert(offsetof(CVECTOR, x) == 0 && offsetof(CVECTOR, y) == 4 && offsetof(CVECTOR, z) == 8);
static_assert(std::is_standard_layout_v<DVECTOR> && std::is_trivially_copyable_v<DVECTOR>);
static_assert(sizeof(DVECTOR) == 24 && alignof(DVECTOR) <= alignof(double));
