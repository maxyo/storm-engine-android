#include "mips.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace storm::texture
{
namespace
{
// sRGB <-> linear. To sRGB through a table over linear values: kToSrgbSize steps keep the darkest 8-bit sRGB steps
// (where the curve is steepest, 12.92) apart.
constexpr int kToSrgbSize = 16384;

const std::array<float, 256> &to_linear_table()
{
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i)
        {
            const float c = i / 255.0f;
            t[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return t;
    }();
    return table;
}

const std::vector<uint8_t> &to_srgb_table()
{
    static const std::vector<uint8_t> table = [] {
        std::vector<uint8_t> t(kToSrgbSize + 1);
        for (int i = 0; i <= kToSrgbSize; ++i)
        {
            const float l = float(i) / kToSrgbSize;
            const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
            t[i] = uint8_t(std::clamp(c * 255.0f + 0.5f, 0.0f, 255.0f));
        }
        return t;
    }();
    return table;
}

uint8_t to_srgb(float linear)
{
    return to_srgb_table()[size_t(std::clamp(linear, 0.0f, 1.0f) * kToSrgbSize + 0.5f)];
}

// One level in linear light: alpha-weighted colour (r, g, b times alpha), alpha, and the plain colour average for
// texels whose whole footprint is transparent (they still show through bilinear filtering at cut-out edges).
constexpr int kChannels = 7;

struct Level
{
    uint32_t width = 0, height = 0;
    std::vector<float> texels; // kChannels per texel
};

// Texel (x, y) of the level below, clamped to its edge (odd sizes: the last row/column is taken twice).
template <typename Fetch> Level downsample(uint32_t width, uint32_t height, Fetch &&fetch)
{
    Level out;
    out.width = std::max(width / 2, 1u);
    out.height = std::max(height / 2, 1u);
    out.texels.resize(size_t(out.width) * out.height * kChannels);
    float *o = out.texels.data();
    for (uint32_t y = 0; y < out.height; ++y)
    {
        const uint32_t y0 = std::min(2 * y, height - 1), y1 = std::min(2 * y + 1, height - 1);
        for (uint32_t x = 0; x < out.width; ++x, o += kChannels)
        {
            const uint32_t x0 = std::min(2 * x, width - 1), x1 = std::min(2 * x + 1, width - 1);
            float sum[kChannels] = {};
            fetch(x0, y0, sum);
            fetch(x1, y0, sum);
            fetch(x0, y1, sum);
            fetch(x1, y1, sum);
            for (int c = 0; c < kChannels; ++c)
                o[c] = sum[c] * 0.25f;
        }
    }
    return out;
}

// A cut-out level's alpha: which texels pass the test at kCutoutReference (those whose alpha bin is at least `bin`) and
// the scale of the alpha around it.
constexpr int kBins = 4096;

int alpha_bin(float alpha)
{
    return int(std::clamp(alpha, 0.0f, 1.0f) * (kBins - 1) + 0.5f);
}

struct Cut
{
    int bin = 0;        // texels in this bin and above pass, the others fail (kBins: none passes)
    float scale = 1.0f; // their alpha's scale: the lowest passing bin lands on the reference
};

// The cut that makes `level` pass the test for `coverage` of its texels, as closely as its texels allow.
Cut coverage_cut(const Level &level, float coverage)
{
    std::vector<uint32_t> histogram(kBins, 0);
    const size_t texels = size_t(level.width) * level.height;
    for (size_t i = 0; i < texels; ++i)
        ++histogram[size_t(alpha_bin(level.texels[i * kChannels + 3]))];
    // The alpha t such that the texels at or above it come closest to `coverage`: scaled by ref / t they pass the
    // test. (Texels of one alpha pass or fail together: a level rarely hits the share exactly.)
    const double wanted = coverage * double(texels);
    double above = 0.0;
    int bin = kBins - 1;
    for (; bin > 0; --bin)
    {
        const double with = above + histogram[bin];
        if (with >= wanted)
        {
            if (wanted - above < with - wanted && bin + 1 < kBins)
                ++bin; // stopping above this bin is closer
            break;
        }
        above = with;
    }
    const float t = std::max(float(bin) / (kBins - 1), 1.0f / (kBins - 1));
    return {bin, (kCutoutReference / 255.0f) / t};
}

// `cut`: a cut-out level's (null: its alpha as it is). The cut decides each texel's side of the test: the scaled alpha,
// rounded to 8 bits, must not carry a failing texel over the reference (a 1x1 level of a small disk at 15 % coverage
// would turn opaque: the planets' grey squares in the night sky) nor a passing one under it.
std::vector<uint8_t> to_rgba8(const Level &level, const Cut *cut)
{
    std::vector<uint8_t> out(size_t(level.width) * level.height * 4);
    const float *t = level.texels.data();
    for (size_t i = 0; i < out.size(); i += 4, t += kChannels)
    {
        const float a = t[3];
        const bool weighted = a > 1.0f / 1024.0f;
        for (int c = 0; c < 3; ++c)
            out[i + c] = to_srgb(weighted ? t[c] / a : t[4 + c]);
        int alpha = int(std::clamp(a * (cut ? cut->scale : 1.0f) * 255.0f + 0.5f, 0.0f, 255.0f));
        if (cut)
            alpha = alpha_bin(a) >= cut->bin ? std::max(alpha, int(kCutoutReference)) : std::min(alpha, kCutoutReference - 1);
        out[i + 3] = uint8_t(alpha);
    }
    return out;
}
} // namespace

uint32_t full_mip_levels(uint32_t width, uint32_t height)
{
    uint32_t levels = 1;
    for (uint32_t size = std::max(width, height); size > 1; size >>= 1)
        ++levels;
    return levels;
}

bool is_cutout_alpha(const uint8_t *rgba, size_t texels)
{
    size_t below = 0, between = 0;
    for (size_t i = 0; i < texels; ++i)
    {
        const uint8_t a = rgba[i * 4 + 3];
        below += a < kCutoutReference;
        between += a > 24 && a < 232;
    }
    // Both sides present (at least 1 %), and the soft edge texels are a small part (a blended texture is not).
    return below * 100 >= texels && (texels - below) * 100 >= texels && between * 4 <= texels;
}

std::vector<std::vector<uint8_t>> build_mip_chain(const uint8_t *rgba, uint32_t width, uint32_t height, uint32_t levels,
                                                  bool cutout)
{
    std::vector<std::vector<uint8_t>> chain;
    if (levels <= 1)
        return chain;
    const std::array<float, 256> &linear = to_linear_table();
    float coverage = 0.0f;
    if (cutout)
    {
        size_t passed = 0;
        const size_t texels = size_t(width) * height;
        for (size_t i = 0; i < texels; ++i)
            passed += rgba[i * 4 + 3] >= kCutoutReference;
        coverage = float(double(passed) / double(texels));
    }
    Level level = downsample(width, height, [&](uint32_t x, uint32_t y, float *sum) {
        const uint8_t *p = rgba + (size_t(y) * width + x) * 4;
        const float a = p[3] / 255.0f;
        for (int c = 0; c < 3; ++c)
        {
            sum[c] += linear[p[c]] * a;
            sum[4 + c] += linear[p[c]];
        }
        sum[3] += a;
    });
    for (uint32_t l = 1; l < levels; ++l)
    {
        const Cut cut = cutout ? coverage_cut(level, coverage) : Cut{};
        chain.push_back(to_rgba8(level, cutout ? &cut : nullptr));
        if (l + 1 < levels)
        {
            const Level &below = level;
            level = downsample(below.width, below.height, [&](uint32_t x, uint32_t y, float *sum) {
                const float *t = below.texels.data() + (size_t(y) * below.width + x) * kChannels;
                for (int c = 0; c < kChannels; ++c)
                    sum[c] += t[c];
            });
        }
    }
    return chain;
}

} // namespace storm::texture
