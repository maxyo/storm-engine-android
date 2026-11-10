#include "formats.hpp"

#include <ProcessRGB.hpp>
#include <bimg/bimg.h>
#include <bx/allocator.h>

#include <cstring>

namespace storm::texture
{
namespace
{
uint16_t read16(const uint8_t *p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t read32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
bx::DefaultAllocator allocator; // bimg needs one only for formats that are not BC
} // namespace

size_t dxt_payload_size(const Layout &layout)
{
    size_t total = 0;
    for (uint32_t level = 0; level < layout.levels; ++level)
        total += dxt_level_size(layout.format, layout.level_width(level), layout.level_height(level));
    return total * layout.faces;
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
    const uint32_t aligned_width = blocks_x(width) * 4, aligned_height = blocks_x(height) * 4;
    bimg::TextureFormat::Enum source = bimg::TextureFormat::BC1;
    if (format == DxtFormat::Bc2)
        source = bimg::TextureFormat::BC2;
    else if (format == DxtFormat::Bc3)
        source = bimg::TextureFormat::BC3;
    bimg::imageDecodeToRgba8(&allocator, rgba, src, aligned_width, aligned_height, aligned_width * 4, source);
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
