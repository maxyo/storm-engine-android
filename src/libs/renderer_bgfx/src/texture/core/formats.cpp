#include "formats.hpp"

#include <ProcessRGB.hpp>

#include <cstring>

namespace storm::texture
{
namespace
{
uint16_t read16(const uint8_t *p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t read32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

void expand565(uint16_t c, uint8_t *out)
{
    const uint32_t r = c >> 11 & 31, g = c >> 5 & 63, b = c & 31;
    out[0] = uint8_t(r << 3 | r >> 2);
    out[1] = uint8_t(g << 2 | g >> 4);
    out[2] = uint8_t(b << 3 | b >> 2);
    out[3] = 255;
}
} // namespace

size_t dxt_payload_size(const Layout &layout)
{
    size_t total = 0;
    for (uint32_t level = 0; level < layout.levels; ++level)
        total += dxt_level_size(layout.format, layout.level_width(level), layout.level_height(level));
    return total * layout.faces;
}

void decode_bc1_block(const uint8_t *block, uint8_t *rgba, bool four_colour_only)
{
    const uint16_t c0 = read16(block), c1 = read16(block + 2);
    uint8_t palette[4][4];
    expand565(c0, palette[0]);
    expand565(c1, palette[1]);
    if (c0 > c1 || four_colour_only)
    {
        for (int i = 0; i < 3; ++i)
        {
            palette[2][i] = uint8_t((2 * palette[0][i] + palette[1][i] + 1) / 3);
            palette[3][i] = uint8_t((palette[0][i] + 2 * palette[1][i] + 1) / 3);
        }
        palette[2][3] = palette[3][3] = 255;
    }
    else
    {
        for (int i = 0; i < 3; ++i)
            palette[2][i] = uint8_t((palette[0][i] + palette[1][i] + 1) / 2);
        palette[2][3] = 255;
        memset(palette[3], 0, 4);
    }
    const uint32_t indices = read32(block + 4);
    for (int texel = 0; texel < 16; ++texel)
        memcpy(rgba + texel * 4, palette[indices >> (2 * texel) & 3], 4);
}

void decode_bc2_block(const uint8_t *block, uint8_t *rgba)
{
    decode_bc1_block(block + 8, rgba, true);
    for (int texel = 0; texel < 16; ++texel)
    {
        const uint32_t nibble = block[texel / 2] >> (4 * (texel & 1)) & 15;
        rgba[texel * 4 + 3] = uint8_t(nibble * 17);
    }
}

void decode_bc3_block(const uint8_t *block, uint8_t *rgba)
{
    decode_bc1_block(block + 8, rgba, true);
    const uint32_t a0 = block[0], a1 = block[1];
    uint8_t alpha[8] = {uint8_t(a0), uint8_t(a1)};
    if (a0 > a1)
        for (uint32_t i = 1; i < 7; ++i)
            alpha[i + 1] = uint8_t(((7 - i) * a0 + i * a1 + 3) / 7);
    else
    {
        for (uint32_t i = 1; i < 5; ++i)
            alpha[i + 1] = uint8_t(((5 - i) * a0 + i * a1 + 2) / 5);
        alpha[6] = 0;
        alpha[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i)
        bits |= uint64_t(block[2 + i]) << (8 * i);
    for (int texel = 0; texel < 16; ++texel)
        rgba[texel * 4 + 3] = alpha[bits >> (3 * texel) & 7];
}

bool bc1_block_has_transparency(const uint8_t *block)
{
    if (read16(block) > read16(block + 2))
        return false;
    const uint32_t indices = read32(block + 4);
    // a texel uses index 3 when both of its bits are set
    return (indices & indices >> 1 & 0x55555555u) != 0;
}

bool bc1_has_transparency(const uint8_t *data, size_t size)
{
    for (size_t offset = 0; offset + 8 <= size; offset += 8)
        if (bc1_block_has_transparency(data + offset))
            return true;
    return false;
}

void decode_level(DxtFormat format, const uint8_t *src, uint32_t width, uint32_t height, uint8_t *rgba)
{
    const uint32_t bx = blocks_x(width), by = blocks_x(height);
    const size_t row_pitch = size_t(bx) * 16; // bytes per texel row of the aligned image
    uint8_t block[64];
    for (uint32_t y = 0; y < by; ++y)
        for (uint32_t x = 0; x < bx; ++x)
        {
            switch (format)
            {
            case DxtFormat::Bc1:
                decode_bc1_block(src, block);
                break;
            case DxtFormat::Bc2:
                decode_bc2_block(src, block);
                break;
            case DxtFormat::Bc3:
                decode_bc3_block(src, block);
                break;
            }
            src += dxt_block_bytes(format);
            uint8_t *out = rgba + size_t(y) * 4 * row_pitch + size_t(x) * 16;
            for (int row = 0; row < 4; ++row)
                memcpy(out + row * row_pitch, block + row * 16, 16);
        }
}

EtcFormat choose_etc_format(DxtFormat format, const uint8_t *payload, size_t size)
{
    if (format != DxtFormat::Bc1)
        return EtcFormat::Rgba8;
    return bc1_has_transparency(payload, size) ? EtcFormat::Rgba8 : EtcFormat::Rgb8;
}

void encode_level(EtcFormat format, const uint8_t *rgba, uint32_t aligned_width, uint32_t aligned_height,
                  uint8_t *out, EncodeScratch &scratch)
{
    // etcpak reads pixels as uint32 0xAARRGGBB (BGRA bytes in memory, verified by
    // the channel-order unit test); the decoded image is RGBA in memory.
    const size_t texels = size_t(aligned_width) * aligned_height;
    scratch.pixels.resize(texels);
    for (size_t i = 0; i < texels; ++i)
    {
        const uint8_t *p = rgba + i * 4;
        scratch.pixels[i] = uint32_t(p[2]) | uint32_t(p[1]) << 8 | uint32_t(p[0]) << 16 | uint32_t(p[3]) << 24;
    }
    const uint32_t blocks = (aligned_width / 4) * (aligned_height / 4);
    const uint32_t words = format == EtcFormat::Rgb8 ? 1 : 2;
    scratch.blocks.resize(size_t(blocks) * words);
    if (format == EtcFormat::Rgb8)
        CompressEtc2Rgb(scratch.pixels.data(), scratch.blocks.data(), blocks, aligned_width, true);
    else
        CompressEtc2Rgba(scratch.pixels.data(), scratch.blocks.data(), blocks, aligned_width, true);
    memcpy(out, scratch.blocks.data(), scratch.blocks.size() * sizeof(uint64_t));
}

} // namespace storm::texture
