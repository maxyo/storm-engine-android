//============================================================================================
// Spirenkov Maxim, 2003
//============================================================================================
// Color
//============================================================================================

#pragma once

#pragma pack(push, 1)

#include <stdint.h>

#include "../math3d.h"
#include "vector.h"
#include "vector4.h"

// Color representation class (float)
class Color
{
  public:
    // Red
    float r;
    // Green
    float g;
    // Blue
    float b;
    // Transparency
    float a;

    // -----------------------------------------------------------
    // Constructors
    // -----------------------------------------------------------
  public:
    // Value conversions keep the packed r/g/b/a representation unchanged.
    storm::math::GlmVec3 glmRgb() const { return {r, g, b}; }
    storm::math::GlmVec4 glmRgba() const { return {r, g, b, a}; }
    Color &setRgb(const storm::math::GlmVec3 &value)
    {
        r = value.x; g = value.y; b = value.z;
        return *this;
    }
    Color &setRgba(const storm::math::GlmVec4 &value)
    {
        r = value.x; g = value.y; b = value.z; a = value.w;
        return *this;
    }

    // Empty constructor
    Color();
    // Fill with rgb number
    Color(float rgb, float a = 1.0f);
    // Fill all components
    Color(float r, float g, float b, float a = 1.0f);
    // Fill all components
    Color(const float f[3], float a = 1.0f);
    // Fill all components
    Color(const Vector &v, float a = 1.0f);
    // Fill all components
    Color(const Vector4 &v);
    // Unpack
    Color(uint32_t c);
    // Copy constructor
    Color(const Color &c);

    // -----------------------------------------------------------
    // Operators
    // -----------------------------------------------------------
  public:
    // Get rgb intensity
    float operator~() const;
    // Return color with constrained components 0..1
    Color operator!() const;

    // Get negative color
    Color operator-() const;

    // Assign rgb
    Color &operator=(float f);
    // Unpack and assign
    Color &operator=(uint32_t c);
    // Assign rgb
    Color &operator=(const Vector &v);
    // Assign
    Color &operator=(const Vector4 &v);
    // Assign
    Color &operator=(const Color &c);
    // per component addition with rgb assignment
    Color &operator+=(float f);
    // Unpack and add
    Color &operator+=(uint32_t c);
    // per component addition with rgb assignment
    Color &operator+=(const Vector &v);
    // per component addition with assignment
    Color &operator+=(const Vector4 &v);
    // per component addition with assignment
    Color &operator+=(const Color &c);
    // per component subtraction with rgb assignment
    Color &operator-=(float f);
    // Unpack and subtract
    Color &operator-=(uint32_t c);
    // per component subtraction with rgb assignment
    Color &operator-=(const Vector &v);
    // per component subtraction with assignment
    Color &operator-=(const Vector4 &v);
    // per component subtraction with assignment
    Color &operator-=(const Color &c);
    // per component multiplication with rgb assignment
    Color &operator*=(float f);
    // Unpack and multiply
    Color &operator*=(uint32_t c);
    // per component multiplication with rgb assignment
    Color &operator*=(const Vector &v);
    // per component multiplication with assignment
    Color &operator*=(const Vector4 &v);
    // per component multiplication with assignment
    Color &operator*=(const Color &c);
    // per component division with rgb assignment
    Color &operator/=(float f);
    // Unpack and divide
    Color &operator/=(uint32_t c);
    // per component division with rgb assignment
    Color &operator/=(const Vector &v);
    // per component division with assignment
    Color &operator/=(const Vector4 &v);
    // per component division with assignment
    Color &operator/=(const Color &c);

    // Scalar rgb multiplication, the result is copied to all components
    Color &operator|=(const Color &c);

    // Get packed color as int32_t
    operator uint32_t() const;

    // -----------------------------------------------------------
    // Transformation
    // -----------------------------------------------------------
  public:
    // Limit to range 0..1
    void Clamp();
    // Limit to range
    void Clamp(float min, float max);

    // Save minimal components
    void Min(const Color &c);
    // Save maximum components
    void Max(const Color &c);

    // Brightness -1..1
    void Brightness(float br);
    // Gamma -1..1
    void Gamma(float gm);
    // Contrast -1..1
    void Contrast(float cn);
    // Change brightness, gamma and contrast
    void Processed(float br, float gm, float cn);

    // -----------------------------------------------------------
    // Utilities
    // -----------------------------------------------------------
  public:
    // Get intensity
    float GetIntensity() const;
    // Normalize rgb
    Color &Normalize();

    // Get color distance between colors
    float GetDistance(const Color &c) const;
    // Get the color distance between colors squared
    float GetDistanceSqr(const Color &c) const;

    // Calculate linearly interpolated value
    Color &Lerp(const Color &c1, const Color &c2, float kBlend);
    // Calculate linearly interpolated value
    Color &LerpA(const Color &c1, const Color &c2, float kBlend);
    // Multiply color by number
    Color &MulColor(float k);
    // Multiply alpha by number
    Color &MulAlpha(float k);

    // Swap r, b
    Color &SwapRB();

    // Get packed color as uint32_t
    uint32_t GetDword() const;

    // Converting A8R8G8B8 to R5G6B5
    static unsigned short Make565(uint32_t color);
    // Converting A8R8G8B8 to X1R5G5B5
    static unsigned short Make555(uint32_t color);
    // Converting A8R8G8B8 to A1R5G5B5
    static unsigned short Make1555(uint32_t color);
    // Converting A8R8G8B8 to A1R5G5B5
    static unsigned short Make4444(uint32_t color);
};

// ===========================================================
// Constructors
// ===========================================================

// Empty constructor
inline Color::Color()
{
    r = 0.0f;
    g = 0.0f;
    b = 0.0f;
    a = 1.0f;
}

// Fill with rgb number
inline Color::Color(float rgb, float a)
{
    r = rgb;
    g = rgb;
    b = rgb;
    this->a = a;
}

// Fill all components
inline Color::Color(float r, float g, float b, float a)
{
    this->r = r;
    this->g = g;
    this->b = b;
    this->a = a;
}

// Fill all components
inline Color::Color(const float f[3], float a)
{
    r = f[0];
    g = f[1];
    b = f[2];
    this->a = a;
}

// Fill all components
inline Color::Color(const Vector &v, float a)
{
    r = v.x;
    g = v.y;
    b = v.z;
    this->a = a;
}

// Fill all components
inline Color::Color(const Vector4 &v)
{
    r = v.x;
    g = v.y;
    b = v.z;
    a = v.w;
}

// Unpack
inline Color::Color(uint32_t c)
{
    r = static_cast<unsigned char>(c >> 16) * (1.0f / 255.0f);
    g = static_cast<unsigned char>(c >> 8) * (1.0f / 255.0f);
    b = static_cast<unsigned char>(c >> 0) * (1.0f / 255.0f);
    a = static_cast<unsigned char>(c >> 24) * (1.0f / 255.0f);
}

// Copy constructor
inline Color::Color(const Color &c)
{
    r = c.r;
    g = c.g;
    b = c.b;
    a = c.a;
}

// ===========================================================
// Operators
// ===========================================================

// Get rgb intensity
inline float Color::operator~() const
{
    return GetIntensity();
}

// Return color with constrained components 0..1
inline Color Color::operator!() const
{
    auto c(*this);
    c.Clamp();
    return c;
}

// Get negative color
inline Color Color::operator-() const
{
    auto c(*this);
    return c.setRgba(storm::math::negated(glmRgba()));
}

// Assign rgb
inline Color &Color::operator=(float f)
{
    r = f;
    g = f;
    b = f;
    a = 1.0f;
    return *this;
}

// Assign rgb
inline Color &Color::operator=(const Vector &v)
{
    r = v.x;
    g = v.y;
    b = v.z;
    a = 1.0f;
    return *this;
}

// Assign
inline Color &Color::operator=(const Vector4 &v)
{
    r = v.x;
    g = v.y;
    b = v.z;
    a = v.w;
    return *this;
}

// Unpack and assign
inline Color &Color::operator=(uint32_t c)
{
    r = static_cast<unsigned char>(c >> 16) * (1.0f / 255.0f);
    g = static_cast<unsigned char>(c >> 8) * (1.0f / 255.0f);
    b = static_cast<unsigned char>(c >> 0) * (1.0f / 255.0f);
    a = static_cast<unsigned char>(c >> 24) * (1.0f / 255.0f);
    return *this;
}

// Assign
inline Color &Color::operator=(const Color &c)
{
    r = c.r;
    g = c.g;
    b = c.b;
    a = c.a;
    return *this;
}

// per component addition with rgb assignment
inline Color &Color::operator+=(float f)
{
    return setRgb(glmRgb() + f);
}

// Unpack and add
inline Color &Color::operator+=(uint32_t c)
{
    const Color clr(c);
    *this += clr;
    return *this;
}

// per component addition with rgb assignment
inline Color &Color::operator+=(const Vector &v)
{
    return setRgb(glmRgb() + v.glmValue());
}

// per component addition with assignment
inline Color &Color::operator+=(const Vector4 &v)
{
    return setRgba(glmRgba() + v.glmValue());
}

// per component addition with assignment
inline Color &Color::operator+=(const Color &c)
{
    return setRgba(glmRgba() + c.glmRgba());
}

// per component subtraction with rgb assignment
inline Color &Color::operator-=(float f)
{
    return setRgb(glmRgb() - f);
}

// Unpack and subtract
inline Color &Color::operator-=(uint32_t c)
{
    const Color clr(c);
    *this = clr;
    return *this;
}

// per component subtraction with rgb assignment
inline Color &Color::operator-=(const Vector &v)
{
    return setRgb(glmRgb() - v.glmValue());
}

// per component subtraction with assignment
inline Color &Color::operator-=(const Vector4 &v)
{
    return setRgba(glmRgba() - v.glmValue());
}

// per component subtraction with assignment
inline Color &Color::operator-=(const Color &c)
{
    return setRgba(glmRgba() - c.glmRgba());
}

// per component multiplication with rgb assignment
inline Color &Color::operator*=(float f)
{
    return setRgb(glmRgb() * f);
}

// Unpack and multiply
inline Color &Color::operator*=(uint32_t c)
{
    const Color clr(c);
    *this *= clr;
    return *this;
}

// per component multiplication with rgb assignment
inline Color &Color::operator*=(const Vector &v)
{
    return setRgb(glmRgb() * v.glmValue());
}

// per component multiplication with assignment
inline Color &Color::operator*=(const Vector4 &v)
{
    return setRgba(glmRgba() * v.glmValue());
}

// per component multiplication with assignment
inline Color &Color::operator*=(const Color &c)
{
    return setRgba(glmRgba() * c.glmRgba());
}

// per component division with rgb assignment
inline Color &Color::operator/=(float f)
{
    // Single-precision reciprocal, unlike Vector::operator/=.
    f = 1.0f / f;
    return setRgb(glmRgb() * f);
}

// Unpack and divide
inline Color &Color::operator/=(uint32_t c)
{
    const Color clr(c);
    *this /= clr;
    return *this;
}

// per component division with rgb assignment
inline Color &Color::operator/=(const Vector &v)
{
    return setRgb(glmRgb() / v.glmValue());
}

// per component division with assignment
inline Color &Color::operator/=(const Vector4 &v)
{
    return setRgba(glmRgba() / v.glmValue());
}

// per component division with assignment
inline Color &Color::operator/=(const Color &c)
{
    return setRgba(glmRgba() / c.glmRgba());
}

// Scalar rgb multiplication, the result is copied to all components
inline Color &Color::operator|=(const Color &c)
{
    r = g = b = a = glm::dot(glmRgb(), c.glmRgb());
    return *this;
}

// Get packed color as int32_t
inline Color::operator uint32_t() const
{
    auto c(*this);
    c.Clamp();
    return static_cast<int32_t>(c.GetDword());
}

/*!\relates Color
per component addition with rgb assignment
*/
inline Color operator+(const Color &c, float f)
{
    auto clr(c);
    clr += f;
    return clr;
}

/*!\relates Color
per component addition with rgb assignment
*/
inline Color operator+(float f, const Color &c)
{
    auto clr(c);
    clr += f;
    return clr;
}

/*!\relates Color
Unpack and add
*/
inline Color operator+(const Color &c, uint32_t cl)
{
    auto clr(c);
    clr += cl;
    return clr;
}

/*!\relates Color
Unpack and add
*/
inline Color operator+(uint32_t cl, const Color &c)
{
    auto clr(c);
    clr += cl;
    return clr;
}

/*!\relates Color
per component addition with rgb assignment
*/
inline Color operator+(const Color &c, const Vector &v)
{
    auto clr(c);
    clr += v;
    return clr;
}

/*!\relates Color
per component addition with rgb assignment
*/
inline Color operator+(const Vector &v, const Color &c)
{
    auto clr(c);
    clr += v;
    return clr;
}

/*!\relates Color
per component addition with assignment
*/
inline Color operator+(const Color &c, const Vector4 &v)
{
    auto clr(c);
    clr += v;
    return clr;
}

/*!\relates Color
per component addition with assignment
*/
inline Color operator+(const Vector4 &v, const Color &c)
{
    auto clr(c);
    clr += v;
    return clr;
}

/*!\relates Color
per component addition with assignment
*/
inline Color operator+(const Color &c1, const Color &c2)
{
    auto clr(c1);
    clr += c2;
    return clr;
}

/*!\relates Color
per component subtraction with rgb assignment
*/
inline Color operator-(const Color &c, float f)
{
    auto clr(c);
    clr -= f;
    return clr;
}

/*!\relates Color
per component subtraction with rgb assignment
*/
inline Color operator-(float f, const Color &c)
{
    Color clr(f);
    clr -= f;
    return clr;
}

/*!\relates Color
Unpack and subtract
*/
inline Color operator-(const Color &c, uint32_t cl)
{
    auto clr(c);
    clr -= cl;
    return clr;
}

/*!\relates Color
Unpack and subtract
*/
inline Color operator-(uint32_t cl, const Color &c)
{
    Color clr(cl);
    clr -= c;
    return clr;
}

/*!\relates Color
per component subtraction with rgb assignment
*/
inline Color operator-(const Color &c, const Vector &v)
{
    auto clr(c);
    clr -= v;
    return clr;
}

/*!\relates Color
per component subtraction with rgb assignment
*/
inline Color operator-(const Vector &v, const Color &c)
{
    Color clr(v);
    clr -= c;
    return clr;
}

/*!\relates Color
per component subtraction with assignment
*/
inline Color operator-(const Color &c, const Vector4 &v)
{
    auto clr(c);
    clr -= v;
    return clr;
}

/*!\relates Color
per component subtraction with assignment
*/
inline Color operator-(const Vector4 &v, const Color &c)
{
    Color clr(v);
    clr -= c;
    return clr;
}

/*!\relates Color
per component subtraction with assignment
*/
inline Color operator-(const Color &c1, const Color &c2)
{
    auto c(c1);
    c -= c2;
    return c;
}

/*!\relates Color
per component multiplication with rgb assignment
*/
inline Color operator*(const Color &c, float f)
{
    auto clr(c);
    clr *= f;
    return clr;
}

/*!\relates Color
per component multiplication with rgb assignment
*/
inline Color operator*(float f, const Color &c)
{
    auto clr(c);
    clr *= f;
    return clr;
}

/*!\relates Color
Unpack and multiply
*/
inline Color operator*(const Color &c, uint32_t cl)
{
    auto clr(c);
    clr *= cl;
    return clr;
}

/*!\relates Color
Unpack and multiply
*/
inline Color operator*(uint32_t cl, const Color &c)
{
    auto clr(c);
    clr *= cl;
    return clr;
}

/*!\relates Color
per component multiplication with rgb assignment
*/
inline Color operator*(const Color &c, const Vector &v)
{
    auto clr(c);
    clr *= v;
    return clr;
}

/*!\relates Color
per component multiplication with rgb assignment
*/
inline Color operator*(const Vector &v, const Color &c)
{
    auto clr(c);
    clr *= v;
    return clr;
}

/*!\relates Color
per component multiplication with assignment
*/
inline Color operator*(const Color &c, const Vector4 &v)
{
    auto clr(c);
    clr *= v;
    return clr;
}

/*!\relates Color
per component multiplication with assignment
*/
inline Color operator*(const Vector4 &v, const Color &c)
{
    auto clr(c);
    clr *= v;
    return clr;
}

/*!\relates Color
per component multiplication with assignment
*/
inline Color operator*(const Color &c1, const Color &c2)
{
    auto clr(c1);
    clr *= c2;
    return clr;
}

/*!\relates Color
per component division with rgb assignment
*/
inline Color operator/(const Color &c, float f)
{
    auto clr(c);
    clr /= f;
    return clr;
}

/*!\relates Color
per component division with rgb assignment
*/
inline Color operator/(float f, const Color &c)
{
    Color clr(f);
    clr /= c;
    return clr;
}

/*!\relates Color
Unpack and divide
*/
inline Color operator/(const Color &c, uint32_t cl)
{
    auto clr(c);
    clr /= cl;
    return clr;
}

/*!\relates Color
Unpack and divide
*/
inline Color operator/(uint32_t cl, const Color &c)
{
    Color clr(cl);
    clr /= c;
    return clr;
}

/*!\relates Color
per component division with rgb assignment
*/
inline Color operator/(const Color &c, const Vector &v)
{
    auto clr(c);
    clr /= v;
    return clr;
}

/*!\relates Color
per component division with rgb assignment
*/
inline Color operator/(const Vector &v, const Color &c)
{
    Color clr(v);
    clr /= c;
    return clr;
}

/*!\relates Color
per component division with assignment
*/
inline Color operator/(const Color &c, const Vector4 &v)
{
    auto clr(c);
    clr /= v;
    return clr;
}

/*!\relates Color
per component division with assignment
*/
inline Color operator/(const Vector4 &v, const Color &c)
{
    Color clr(v);
    clr /= c;
    return clr;
}

/*!\relates Color
per component division with assignment
*/
inline Color operator/(const Color &c1, const Color &c2)
{
    auto clr(c1);
    clr /= c2;
    return clr;
}

/*!\relates Color
Scalar rgb multiplication, the result is copied to all components
*/
inline Color operator|(const Color &c1, const Color &c2)
{
    auto clr(c1);
    clr |= c2;
    return clr;
}

// ===========================================================
// Transformation
// ===========================================================

// Limit to range 0..1
inline void Color::Clamp()
{
    setRgba(glm::clamp(glmRgba(), 0.0f, 1.0f));
}

// Limit to range
inline void Color::Clamp(float min, float max)
{
    setRgba(glm::clamp(glmRgba(), min, max));
}

// Save minimal components
inline void Color::Min(const Color &c)
{
    setRgba(glm::min(glmRgba(), c.glmRgba()));
}

// Save maximum components
inline void Color::Max(const Color &c)
{
    setRgba(glm::max(glmRgba(), c.glmRgba()));
}

// Brightness -1..1
inline void Color::Brightness(float br)
{
    br = glm::clamp(br, -1.0f, 1.0f);
    *this += br;
    Clamp();
}

// Gamma -1..1
inline void Color::Gamma(float gm)
{
    Clamp();
    if (gm > 0.0f)
    {
        gm = glm::min(gm, 1.0f);
        gm = 1.0f + gm * 9.0f;
        setRgb(glm::pow(glmRgb(), storm::math::GlmVec3(gm)));
    }
    else if (gm < 0.0f)
    {
        gm = glm::max(gm, -1.0f);
        gm = 1.0f + gm * 0.9f;
        setRgb(glm::pow(glmRgb(), storm::math::GlmVec3(gm * 10.0f)));
    }
}

// Contrast -1..1
inline void Color::Contrast(float cn)
{
    cn = glm::clamp(cn, -1.0f, 1.0f);
    cn *= 5.0f;
    setRgb((glmRgb() - 0.5f) * cn + 0.5f);
    Clamp();
}

// Change brightness, gamma and contrast
inline void Color::Processed(float br, float gm, float cn)
{
    Contrast(cn);
    Gamma(gm);
    Brightness(br);
}

// ===========================================================
// Utilities
// ===========================================================

// Get intensity
inline float Color::GetIntensity() const
{
    return glm::dot(glmRgb(), storm::math::GlmVec3(0.299f, 0.587f, 0.114f));
}

// Normalize rgb
inline Color &Color::Normalize()
{
    auto min = glm::min(glm::min(r, g), b);
    const auto max = glm::max(glm::max(r, g), b);
    min = glm::min(min, 0.0f);
    auto dlt = max - min;
    setRgb(glmRgb() - min);
    if (dlt > 1.0f)
    {
        dlt = 1.0f / dlt;
        setRgb(glmRgb() * dlt);
    }
    Clamp();
    return *this;
}

// Get color distance between colors
inline float Color::GetDistance(const Color &c) const
{
    return sqrtf(GetDistanceSqr(c));
}

// Get the color distance between colors squared
inline float Color::GetDistanceSqr(const Color &c) const
{
    const auto weighted = (glmRgb() - c.glmRgb()) * storm::math::GlmVec3(0.299f, 0.587f, 0.114f);
    return glm::dot(weighted, weighted);
}

// Calculate linearly interpolated value
inline Color &Color::Lerp(const Color &c1, const Color &c2, float kBlend)
{
    // Not glm::mix: it evaluates a * (1 - k) + b * k, which rounds differently.
    return setRgb(c1.glmRgb() + (c2.glmRgb() - c1.glmRgb()) * kBlend);
}

// Calculate linearly interpolated value
inline Color &Color::LerpA(const Color &c1, const Color &c2, float kBlend)
{
    return setRgba(c1.glmRgba() + (c2.glmRgba() - c1.glmRgba()) * kBlend);
}

// Multiply color by number
inline Color &Color::MulColor(float k)
{
    return setRgb(glmRgb() * k);
}

// Multiply alpha by number
inline Color &Color::MulAlpha(float k)
{
    a *= k;
    return *this;
}

// Swap r, b
inline Color &Color::SwapRB()
{
    const auto t = r;
    r = b;
    b = t;
    return *this;
}

// Get packed color as uint32_t
inline uint32_t Color::GetDword() const
{
    uint32_t t = (static_cast<uint8_t>(fftoi(r * 255.0f)) << 16) +
                 (static_cast<uint8_t>(fftoi(g * 255.0f)) << 8) +
                 (static_cast<uint8_t>(fftoi(b * 255.0f)) << 0) +
                 (static_cast<uint8_t>(fftoi(a * 255.0f)) << 24);

    return t;
/*
    DColor color;
    const auto k = 255.0f;

    color.r = static_cast<uint8_t>(fftoi(r * k));
    color.g = static_cast<uint8_t>(fftoi(g * k));
    color.b = static_cast<uint8_t>(fftoi(b * k));
    color.a = static_cast<uint8_t>(fftoi(a * k));

    return color.c;
*/
}

// Converting A8R8G8B8 to R5G6B5
inline unsigned short Color::Make565(uint32_t color)
{
    //   11111000 11111100 11111000
    //           11111 111111 11111
    const auto b = (color >> 3) & 0x1f;
    const auto g = (color >> 5) & 0x7e0;
    const auto r = (color >> 8) & 0xf800;
    return static_cast<unsigned short>(r | g | b);
}

// Converting A8R8G8B8 to X1R5G5B5
inline unsigned short Color::Make555(uint32_t color)
{
    //   11111000 11111000 11111000
    //           011111 11111 11111
    const auto b = (color >> 3) & 0x1f;
    const auto g = (color >> 6) & 0x3e0;
    const auto r = (color >> 9) & 0x7c00;
    return static_cast<unsigned short>(r | g | b);
}

// Converting A8R8G8B8 to A1R5G5B5
inline unsigned short Color::Make1555(uint32_t color)
{
    //   11111000 11111000 11111000
    //          1 11111 11111 11111
    const auto b = (color >> 3) & 0x1f;
    const auto g = (color >> 6) & 0x3e0;
    const auto r = (color >> 9) & 0x7c00;
    const auto a = (color >> 16) & 0x8000;
    return static_cast<unsigned short>(r | g | b | a);
}

// Converting A8R8G8B8 to A1R5G5B5
inline unsigned short Color::Make4444(uint32_t color)
{
    //  11110000 11111000 11111000 11111000
    //                  1111 1111 1111 1111
    const auto b = (color >> 4) & 0xf;
    const auto g = (color >> 8) & 0xf0;
    const auto r = (color >> 12) & 0xf00;
    const auto a = (color >> 16) & 0xf000;
    return static_cast<unsigned short>(r | g | b | a);
}

#pragma pack(pop)
