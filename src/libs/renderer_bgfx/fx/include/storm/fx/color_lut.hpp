// Procedural colour grading: a small set of grading controls baked into a 3D
// look-up table on the CPU. No third-party LUT data is used anywhere.
#pragma once

#include "storm/fx/settings.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace storm::fx
{

// All controls act on display-referred, gamma-encoded RGB in 0..1. The default
// value of every field is the identity.
struct ColorGrade
{
    float exposure = 0.0f;    // stops
    float temperature = 0.0f; // -1 cool .. +1 warm
    float tint = 0.0f;        // -1 green .. +1 magenta
    float contrast = 1.0f;    // S-curve exponent around mid grey; keeps black and white fixed
    float saturation = 1.0f;
    float vibrance = 0.0f;    // extra saturation for weakly saturated colours only
    std::array<float, 3> shadow_tint{0.0f, 0.0f, 0.0f};    // added in the shadows
    std::array<float, 3> highlight_tint{0.0f, 0.0f, 0.0f}; // added in the highlights
    std::array<float, 3> lift{0.0f, 0.0f, 0.0f};
    std::array<float, 3> gamma{1.0f, 1.0f, 1.0f};
    std::array<float, 3> gain{1.0f, 1.0f, 1.0f};

    static ColorGrade Preset(GradePreset preset);
    // Component-wise interpolation, e.g. for weather transitions or grade_strength.
    static ColorGrade Lerp(const ColorGrade &a, const ColorGrade &b, float t);

    bool IsIdentity() const;
    std::array<float, 3> Apply(std::array<float, 3> rgb) const;
};

inline constexpr int kDefaultLutSize = 32;

// size^3 RGBA8 texels, red fastest, then green, then blue (the layout of a
// bgfx 3D texture with red on U, green on V, blue on W). Alpha is 255.
std::vector<uint8_t> BuildLutRgba8(const ColorGrade &grade, int size = kDefaultLutSize);

// CPU reference of the shader's trilinear LUT fetch (tests and tools).
std::array<float, 3> SampleLut(const std::vector<uint8_t> &lut, int size, std::array<float, 3> rgb);

// ---- tone curve (mirrors shaders/include/fx_tonemap.sh) ----
// Scene values are display-linear (gamma 2.0 decoded). `ExpandLdr` gives the
// 0..1 legacy image some highlight headroom, `AcesFit` is the filmic curve.
float ExpandLdr(float x);
std::array<float, 3> AcesFit(std::array<float, 3> linear_rgb);
// Exposure that makes `AcesFit(ExpandLdr(grey) * k)` return `grey` again for
// 18 % grey, so enabling the tone curve does not change the overall brightness.
float TonemapExposureBias();
// Whole LDR -> LDR transfer of the tone-map pass for a neutral value (0..1, gamma encoded).
float TonemapTransfer(float encoded, float exposure = 1.0f, float strength = 1.0f);

} // namespace storm::fx
