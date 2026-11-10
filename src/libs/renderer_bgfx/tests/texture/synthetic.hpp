// Synthetic test images and a minimal BC1/BC3 encoder (tests and benchmark only).
#pragma once

#include "formats.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace storm::texture::test
{

enum class Pattern
{
    Gradient,
    Noise,
    Mixed, // smooth gradient with noise and hard edges
};

inline std::vector<uint8_t> make_image(Pattern pattern, uint32_t w, uint32_t h, uint32_t seed, bool alpha)
{
    std::vector<uint8_t> rgba(size_t(w) * h * 4);
    std::mt19937 rng(seed);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
        {
            uint8_t *p = &rgba[(size_t(y) * w + x) * 4];
            const uint32_t gx = x * 255 / std::max(1u, w - 1), gy = y * 255 / std::max(1u, h - 1);
            const uint32_t n = rng();
            switch (pattern)
            {
            case Pattern::Gradient:
                p[0] = uint8_t(gx), p[1] = uint8_t(gy), p[2] = uint8_t((gx + gy) / 2);
                break;
            case Pattern::Noise:
                p[0] = uint8_t(n), p[1] = uint8_t(n >> 8), p[2] = uint8_t(n >> 16);
                break;
            case Pattern::Mixed:
            {
                const int jitter = int(n & 15) - 8;
                const bool edge = ((x / 37) + (y / 23)) & 1;
                p[0] = uint8_t(std::clamp(int(gx) + jitter + (edge ? 40 : 0), 0, 255));
                p[1] = uint8_t(std::clamp(int(gy) + jitter, 0, 255));
                p[2] = uint8_t(std::clamp(int(255 - gx) + jitter - (edge ? 40 : 0), 0, 255));
                break;
            }
            }
            p[3] = alpha ? uint8_t((x * 7 + y * 3) & 0xff) : 255;
        }
    return rgba;
}

inline uint16_t to565(const uint8_t *c) { return uint16_t((c[0] >> 3) << 11 | (c[1] >> 2) << 5 | c[2] >> 3); }

// Bounding-box BC1 colour block, 4-colour mode; `punch` marks texels with
// alpha < 128 as transparent (3-colour mode).
inline void encode_bc1_block(const uint8_t *texels, uint8_t *out, bool punch)
{
    uint8_t lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
    bool any_transparent = false;
    for (int i = 0; i < 16; ++i)
    {
        if (punch && texels[i * 4 + 3] < 128)
        {
            any_transparent = true;
            continue;
        }
        for (int c = 0; c < 3; ++c)
            lo[c] = std::min(lo[c], texels[i * 4 + c]), hi[c] = std::max(hi[c], texels[i * 4 + c]);
    }
    uint16_t c0 = to565(hi), c1 = to565(lo);
    if (any_transparent ? c0 > c1 : c0 < c1)
        std::swap(c0, c1);
    if (!any_transparent && c0 == c1)
        c1 = c0 ? uint16_t(c0 - 1) : c0; // keep 4-colour mode (c0 > c1) unless both are 0
    uint8_t block[8] = {uint8_t(c0), uint8_t(c0 >> 8), uint8_t(c1), uint8_t(c1 >> 8)};
    // palette[k] = colour of index k
    uint8_t palette[4][4];
    for (uint32_t k = 0; k < 4; ++k)
    {
        uint8_t decoded[64];
        const uint32_t sel = k * 0x55555555u;
        memcpy(block + 4, &sel, 4);
        decode_bc1_block(block, decoded);
        memcpy(palette[k], decoded, 4);
    }
    uint32_t indices = 0;
    for (int i = 0; i < 16; ++i)
    {
        int best = 0, best_error = 1 << 30;
        const bool transparent = punch && texels[i * 4 + 3] < 128;
        for (int k = 0; k < 4; ++k)
        {
            const uint8_t *c = palette[k];
            if (transparent != (c[3] == 0))
                continue;
            int error = 0;
            for (int ch = 0; ch < 3; ++ch)
                error += (c[ch] - texels[i * 4 + ch]) * (c[ch] - texels[i * 4 + ch]);
            if (error < best_error)
                best_error = error, best = k;
        }
        indices |= uint32_t(best) << (2 * i);
    }
    memcpy(block + 4, &indices, 4);
    memcpy(out, block, 8);
}

inline void encode_bc3_alpha(const uint8_t *texels, uint8_t *out)
{
    uint8_t lo = 255, hi = 0;
    for (int i = 0; i < 16; ++i)
        lo = std::min(lo, texels[i * 4 + 3]), hi = std::max(hi, texels[i * 4 + 3]);
    out[0] = hi;
    out[1] = lo;
    uint64_t bits = 0;
    for (int i = 0; i < 16; ++i)
    {
        const int a = texels[i * 4 + 3];
        int code = 0;
        if (hi > lo)
        {
            const int step = (hi - a) * 7 / (hi - lo); // 0 = hi .. 7 = lo
            code = step == 0 ? 0 : step == 7 ? 1 : step + 1;
        }
        bits |= uint64_t(code) << (3 * i);
    }
    for (int i = 0; i < 6; ++i)
        out[2 + i] = uint8_t(bits >> (8 * i));
}

// Encodes one level of an RGBA image (any size; edges replicated) to BC1 or BC3.
inline std::vector<uint8_t> encode_dxt(DxtFormat format, const uint8_t *rgba, uint32_t w, uint32_t h,
                                       bool punch = false)
{
    std::vector<uint8_t> out(dxt_level_size(format, w, h));
    uint8_t *dst = out.data();
    for (uint32_t by = 0; by < blocks_x(h); ++by)
        for (uint32_t bx = 0; bx < blocks_x(w); ++bx)
        {
            uint8_t texels[64];
            for (uint32_t i = 0; i < 16; ++i)
            {
                const uint32_t x = std::min(bx * 4 + i % 4, w - 1), y = std::min(by * 4 + i / 4, h - 1);
                memcpy(texels + i * 4, rgba + (size_t(y) * w + x) * 4, 4);
            }
            if (format == DxtFormat::Bc3)
            {
                encode_bc3_alpha(texels, dst);
                encode_bc1_block(texels, dst + 8, false);
                dst += 16;
            }
            else
            {
                encode_bc1_block(texels, dst, punch);
                dst += 8;
            }
        }
    return out;
}

// Box-filtered half-size image.
inline std::vector<uint8_t> downsample(const std::vector<uint8_t> &rgba, uint32_t w, uint32_t h)
{
    const uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
    std::vector<uint8_t> out(size_t(nw) * nh * 4);
    for (uint32_t y = 0; y < nh; ++y)
        for (uint32_t x = 0; x < nw; ++x)
            for (int c = 0; c < 4; ++c)
            {
                uint32_t sum = 0;
                for (uint32_t dy = 0; dy < 2; ++dy)
                    for (uint32_t dx = 0; dx < 2; ++dx)
                        sum += rgba[(size_t(std::min(y * 2 + dy, h - 1)) * w + std::min(x * 2 + dx, w - 1)) * 4 + c];
                out[(size_t(y) * nw + x) * 4 + c] = uint8_t(sum / 4);
            }
    return out;
}

// D3D payload (face-major, all levels) of a synthetic texture.
inline std::vector<uint8_t> make_payload(const Layout &layout, Pattern pattern, uint32_t seed, bool punch = false)
{
    std::vector<uint8_t> payload;
    for (uint32_t face = 0; face < layout.faces; ++face)
    {
        std::vector<uint8_t> image =
            make_image(pattern, layout.width, layout.height, seed + face, layout.format != DxtFormat::Bc1 || punch);
        uint32_t w = layout.width, h = layout.height;
        for (uint32_t level = 0; level < layout.levels; ++level)
        {
            const auto level_data = encode_dxt(layout.format, image.data(), w, h, punch);
            payload.insert(payload.end(), level_data.begin(), level_data.end());
            image = downsample(image, w, h);
            w = std::max(1u, w / 2), h = std::max(1u, h / 2);
        }
    }
    return payload;
}

} // namespace storm::texture::test
