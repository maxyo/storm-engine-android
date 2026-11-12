#include "storm/fx/color_lut.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{
namespace
{
using Rgb = std::array<float, 3>;

float Saturate(float v)
{
    return std::clamp(v, 0.0f, 1.0f);
}

// Exact at both ends, so a fully blended grade equals its target.
float Mix(float a, float b, float t)
{
    return a * (1.0f - t) + b * t;
}

float Luma(const Rgb &c)
{
    return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
}

// Smooth contrast that keeps 0, 0.5 and 1 fixed (no clipping for k > 1).
float SCurve(float x, float k)
{
    x = Saturate(x);
    if (k == 1.0f)
        return x;
    const float a = std::pow(x, k);
    const float b = std::pow(1.0f - x, k);
    return a / std::max(a + b, 1e-12f);
}
} // namespace

ColorGrade ColorGrade::Preset(GradePreset preset)
{
    ColorGrade g;
    switch (preset)
    {
    case GradePreset::Caribbean:
        // Late-afternoon tropics: warm light, teal shadows, rich but not neon colours.
        g.temperature = 0.14f;
        g.tint = 0.01f;
        g.contrast = 1.10f;
        g.saturation = 1.14f;
        g.vibrance = 0.30f;
        g.shadow_tint = {-0.018f, 0.004f, 0.022f};
        g.highlight_tint = {0.022f, 0.008f, -0.024f};
        g.gain = {1.02f, 1.0f, 0.985f};
        break;
    case GradePreset::Overcast:
        // Grey Atlantic day: cool, flat, washed out.
        g.temperature = -0.12f;
        g.contrast = 0.93f;
        g.saturation = 0.70f;
        g.shadow_tint = {-0.006f, 0.0f, 0.012f};
        g.highlight_tint = {-0.010f, 0.0f, 0.008f};
        g.lift = {0.012f, 0.014f, 0.018f};
        g.gain = {0.955f, 0.965f, 0.975f};
        break;
    default:
        break;
    }
    return g;
}

ColorGrade ColorGrade::ForMood(Mood mood)
{
    ColorGrade g;
    switch (mood)
    {
    // The moods shift hue and warmth only, over the player's grade: contrast, saturation and exposure stay those
    // of the game (restrained look; the brightness of the original is kept).
    case Mood::TropicalNoon: // the player's grade as it is
        break;
    case Mood::Sunset:
        g.temperature = 0.12f;
        g.tint = 0.02f;
        g.shadow_tint = {0.004f, -0.004f, 0.012f};
        g.highlight_tint = {0.024f, 0.008f, -0.024f};
        break;
    case Mood::Night:
        g.temperature = -0.14f;
        g.lift = {0.002f, 0.004f, 0.010f};
        g.highlight_tint = {0.012f, 0.006f, -0.010f}; // lamps and windows stay warm
        break;
    case Mood::Storm:
        g.temperature = -0.08f;
        g.tint = -0.03f;
        break;
    case Mood::Interior:
        g.temperature = 0.08f;
        g.highlight_tint = {0.016f, 0.006f, -0.012f};
        break;
    }
    return g;
}

ColorGrade SceneGrade(const SkyState &sky, float overcast, float interior)
{
    const float night = Saturate(sky.night);
    // Golden hour: the sun within about 17 degrees of the horizon.
    const float low_sun = (1.0f - Saturate((sky.sun_dir[1] - 0.05f) / 0.25f)) * (1.0f - night);
    ColorGrade g = ColorGrade::ForMood(ColorGrade::Mood::TropicalNoon);
    g = ColorGrade::Lerp(g, ColorGrade::ForMood(ColorGrade::Mood::Sunset), low_sun);
    g = ColorGrade::Lerp(g, ColorGrade::ForMood(ColorGrade::Mood::Night), night);
    g = ColorGrade::Lerp(g, ColorGrade::ForMood(ColorGrade::Mood::Storm), Saturate(overcast) * (1.0f - 0.5f * night));
    return ColorGrade::Lerp(g, ColorGrade::ForMood(ColorGrade::Mood::Interior), Saturate(interior));
}

ColorGrade ColorGrade::Lerp(const ColorGrade &a, const ColorGrade &b, float t)
{
    ColorGrade g;
    g.exposure = Mix(a.exposure, b.exposure, t);
    g.temperature = Mix(a.temperature, b.temperature, t);
    g.tint = Mix(a.tint, b.tint, t);
    g.contrast = Mix(a.contrast, b.contrast, t);
    g.saturation = Mix(a.saturation, b.saturation, t);
    g.vibrance = Mix(a.vibrance, b.vibrance, t);
    for (int i = 0; i < 3; ++i)
    {
        g.shadow_tint[i] = Mix(a.shadow_tint[i], b.shadow_tint[i], t);
        g.highlight_tint[i] = Mix(a.highlight_tint[i], b.highlight_tint[i], t);
        g.lift[i] = Mix(a.lift[i], b.lift[i], t);
        g.gamma[i] = Mix(a.gamma[i], b.gamma[i], t);
        g.gain[i] = Mix(a.gain[i], b.gain[i], t);
    }
    return g;
}

bool ColorGrade::IsIdentity() const
{
    const ColorGrade identity;
    return exposure == identity.exposure && temperature == identity.temperature && tint == identity.tint &&
           contrast == identity.contrast && saturation == identity.saturation && vibrance == identity.vibrance &&
           shadow_tint == identity.shadow_tint && highlight_tint == identity.highlight_tint && lift == identity.lift &&
           gamma == identity.gamma && gain == identity.gain;
}

std::array<float, 3> ColorGrade::Apply(std::array<float, 3> c) const
{
    if (IsIdentity())
        return c;

    // White balance and exposure act on light, i.e. in linear space.
    if (exposure != 0.0f || temperature != 0.0f || tint != 0.0f)
    {
        const float k = std::exp2(exposure);
        const Rgb wb{k * (1.0f + 0.30f * temperature + 0.10f * tint), k * (1.0f - 0.10f * tint),
                     k * (1.0f - 0.30f * temperature + 0.10f * tint)};
        // Keep luminance: a warmer image must not also become brighter.
        const float norm = 1.0f / Luma(Rgb{wb[0] / k, wb[1] / k, wb[2] / k});
        for (int i = 0; i < 3; ++i)
        {
            const float linear = std::pow(Saturate(c[i]), 2.2f) * wb[i] * norm;
            c[i] = std::pow(std::max(linear, 0.0f), 1.0f / 2.2f);
        }
        // Compress instead of clipping so bright colours keep their hue.
        const float peak = std::max({c[0], c[1], c[2]});
        if (peak > 1.0f)
        {
            const float luma = std::min(Luma(c), 1.0f);
            const float t = (1.0f - luma) / std::max(peak - luma, 1e-6f);
            for (float &v : c)
                v = luma + (v - luma) * t;
        }
    }

    for (float &v : c)
        v = SCurve(v, contrast);

    const float luma = Luma(c);
    const float chroma = std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]});
    const float sat = saturation * (1.0f + vibrance * (1.0f - Saturate(chroma * 2.0f)));
    for (float &v : c)
        v = luma + (v - luma) * sat;

    const float shadow = (1.0f - luma) * (1.0f - luma);
    const float highlight = luma * luma;
    for (int i = 0; i < 3; ++i)
    {
        float v = c[i] + shadow_tint[i] * shadow + highlight_tint[i] * highlight;
        v = v * gain[i] + lift[i] * (1.0f - v);
        v = std::pow(Saturate(v), 1.0f / std::max(gamma[i], 1e-3f));
        c[i] = Saturate(v);
    }
    return c;
}

float GradeDifference(const ColorGrade &a, const ColorGrade &b)
{
    float worst = 0.0f;
    for (int i = 0; i < 27; ++i)
    {
        const Rgb probe{0.1f + 0.4f * (i % 3), 0.1f + 0.4f * (i / 3 % 3), 0.1f + 0.4f * (i / 9)};
        const Rgb x = a.Apply(probe), y = b.Apply(probe);
        for (int c = 0; c < 3; ++c)
            worst = std::max(worst, std::fabs(x[c] - y[c]));
    }
    return worst;
}

void BuildLutSlices(const ColorGrade &grade, int size, const ColorGrade &scene, int first_blue, int blue_count, uint8_t *lut)
{
    const float step = 1.0f / static_cast<float>(size - 1);
    size_t o = static_cast<size_t>(first_blue) * size * size * 4;
    for (int b = first_blue; b < first_blue + blue_count; ++b)
        for (int g = 0; g < size; ++g)
            for (int r = 0; r < size; ++r)
            {
                const Rgb out = grade.Apply(scene.Apply(Rgb{r * step, g * step, b * step}));
                for (int i = 0; i < 3; ++i)
                    lut[o++] = static_cast<uint8_t>(std::lround(Saturate(out[i]) * 255.0f));
                lut[o++] = 255;
            }
}

std::vector<uint8_t> BuildLutRgba8(const ColorGrade &grade, int size, const ColorGrade &scene)
{
    size = std::clamp(size, 2, 64);
    std::vector<uint8_t> lut(static_cast<size_t>(size) * size * size * 4);
    BuildLutSlices(grade, size, scene, 0, size, lut.data());
    return lut;
}

std::array<float, 3> SampleLut(const std::vector<uint8_t> &lut, int size, std::array<float, 3> rgb)
{
    int i0[3];
    float f[3];
    for (int i = 0; i < 3; ++i)
    {
        const float p = Saturate(rgb[i]) * static_cast<float>(size - 1);
        i0[i] = std::min(static_cast<int>(p), size - 2);
        f[i] = p - static_cast<float>(i0[i]);
    }
    Rgb out{0.0f, 0.0f, 0.0f};
    for (int corner = 0; corner < 8; ++corner)
    {
        const int dr = corner & 1, dg = (corner >> 1) & 1, db = (corner >> 2) & 1;
        const float w = (dr ? f[0] : 1.0f - f[0]) * (dg ? f[1] : 1.0f - f[1]) * (db ? f[2] : 1.0f - f[2]);
        const size_t texel = (static_cast<size_t>(i0[2] + db) * size + (i0[1] + dg)) * size + (i0[0] + dr);
        for (int i = 0; i < 3; ++i)
            out[i] += w * static_cast<float>(lut[texel * 4 + i]) / 255.0f;
    }
    return out;
}

// ---- tone curve ----

float ExpandLdr(float x)
{
    // Inverse of an extended Reinhard shoulder: 1.0 maps to kLdrWhite. Must match fx_tonemap.sh.
    constexpr float kLdrWhite = 3.0f;
    constexpr float k = 1.0f - 1.0f / kLdrWhite;
    return x / (1.0f - k * std::min(x, 1.0f));
}

// AgX on a grey value: Blender's log2 range around middle grey and the sigmoid polynomial of Benjamin Wrensch's
// "Minimal AgX", as in three.js (MIT, Copyright (c) 2010-2023 three.js authors; see ../THIRD_PARTY.md); mirrored in
// shaders/include/fx_tonemap.sh.
float AgxCurve(float y)
{
    const float x = Saturate((std::log2(std::max(y, 1e-10f)) + 12.47393f) / 16.5f);
    const float x2 = x * x, x4 = x2 * x2;
    const float s = 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x - 0.00232f;
    return std::pow(std::max(s, 0.0f), 2.2f);
}

float TonemapExposureBias()
{
    static const float bias = [] {
        constexpr float grey = 0.18f;
        const float expanded = ExpandLdr(grey);
        float lo = 0.25f, hi = 8.0f;
        for (int i = 0; i < 40; ++i)
        {
            const float mid = 0.5f * (lo + hi);
            const float v = expanded * mid;
            (AgxCurve(v) < grey ? lo : hi) = mid;
        }
        return 0.5f * (lo + hi);
    }();
    return bias;
}

float TonemapTransfer(float encoded, float exposure, float strength)
{
    const float linear = Saturate(encoded) * Saturate(encoded);
    const float v = ExpandLdr(linear) * exposure * TonemapExposureBias();
    const float plain = std::sqrt(Saturate(linear * exposure));
    constexpr float kShadowEnd = 0.12f; // FX_TONE_SHADOW_END
    return Mix(plain, std::sqrt(Saturate(AgxCurve(v))), strength * Saturate(linear / kShadowEnd));
}

} // namespace storm::fx
