#include "formats.hpp"

#include <bimg/bimg.h>
#include <bimg/encode.h>
#include <bx/allocator.h>
#include <bx/error.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace storm::texture
{
namespace
{
uint16_t read16(const uint8_t *p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t read32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
bx::DefaultAllocator allocator; // stateless (malloc); shared by the transcode workers
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

GpuFormat choose_gpu_format(DxtFormat format, const uint8_t *payload, size_t size, bool bc)
{
    if (bc)
        return format == DxtFormat::Bc1 ? GpuFormat::Bc1 : format == DxtFormat::Bc2 ? GpuFormat::Bc2 : GpuFormat::Bc3;
    if (format != DxtFormat::Bc1)
        return GpuFormat::Etc2Rgba8;
    return bc1_has_transparency(payload, size) ? GpuFormat::Etc2Rgba8 : GpuFormat::Etc2Rgb8;
}

D3dFormat d3d_format(uint32_t d3d)
{
    switch (d3d)
    {
    case 21: // A8R8G8B8: B, G, R, A in memory
        return {bimg::TextureFormat::BGRA8, false};
    case 22: // X8R8G8B8
        return {bimg::TextureFormat::BGRA8, true};
    case 23: // R5G6B5: R in the top bits, as bimg's B5G6R5
        return {bimg::TextureFormat::B5G6R5, false};
    case 25: // A1R5G5B5
        return {bimg::TextureFormat::BGR5A1, false};
    case 26: // A4R4G4B4
        return {bimg::TextureFormat::BGRA4, false};
    case 32: // A8B8G8R8: R first (the video frames)
        return {bimg::TextureFormat::RGBA8, false};
    case 116: // A32B32G32R32F (the bone palettes)
        return {bimg::TextureFormat::RGBA32F, false};
    case 0x31545844: // 'DXT1'
        return {bimg::TextureFormat::BC1, false};
    case 0x32545844: // 'DXT2', 'DXT3'
    case 0x33545844:
        return {bimg::TextureFormat::BC2, false};
    case 0x34545844: // 'DXT4', 'DXT5'
    case 0x35545844:
        return {bimg::TextureFormat::BC3, false};
    }
    return {};
}

void set_opaque(bimg::TextureFormat::Enum format, uint8_t *texels, size_t count)
{
    if (format == bimg::TextureFormat::BGRA8 || format == bimg::TextureFormat::RGBA8)
        for (size_t i = 0; i < count; ++i)
            texels[i * 4 + 3] = 255;
}

size_t d3d_level_bytes(uint32_t d3d, uint32_t width, uint32_t height)
{
    const bimg::TextureFormat::Enum format = d3d_format(d3d).format;
    if (format == bimg::TextureFormat::Unknown)
        return 0;
    const bimg::ImageBlockInfo &block = bimg::getBlockInfo(format);
    if (bimg::isCompressed(format))
        return size_t(blocks_x(width)) * blocks_x(height) * block.blockSize;
    return size_t(width) * height * block.bitsPerPixel / 8;
}

bool tx_format_known(uint32_t format)
{
    switch (static_cast<TxFormat>(format))
    {
    case TxFormat::A8R8G8B8:
    case TxFormat::X8R8G8B8:
    case TxFormat::R5G6B5:
    case TxFormat::A1R5G5B5:
    case TxFormat::A4R4G4B4:
    case TxFormat::Dxt1:
    case TxFormat::Dxt3:
    case TxFormat::Dxt5:
        return true;
    }
    return false;
}

bool decode_rgba8(uint32_t d3d, const uint8_t *data, uint32_t width, uint32_t height, uint8_t *rgba)
{
    const D3dFormat format = d3d_format(d3d);
    if (format.format == bimg::TextureFormat::Unknown || format.format == bimg::TextureFormat::RGBA32F)
        return false;
    if (bimg::isCompressed(format.format))
    {
        // the decoder writes whole 4x4 blocks; keep the visible width x height part
        const uint32_t aligned_width = blocks_x(width) * 4, aligned_height = blocks_x(height) * 4;
        std::vector<uint8_t> blocks(size_t(aligned_width) * aligned_height * 4);
        bimg::imageDecodeToRgba8(&allocator, blocks.data(), data, aligned_width, aligned_height, aligned_width * 4,
                                 format.format);
        for (uint32_t y = 0; y < height; ++y)
            memcpy(rgba + size_t(y) * width * 4, blocks.data() + size_t(y) * aligned_width * 4, size_t(width) * 4);
    }
    else
        bimg::imageDecodeToRgba8(&allocator, rgba, data, width, height, width * 4, format.format);
    if (format.opaque)
        set_opaque(bimg::TextureFormat::RGBA8, rgba, size_t(width) * height);
    return true;
}

namespace
{
// Encodes RGBA8 with bimg (etcpak inside); an encoder error is a programming error (sizes and formats are ours).
void encode(bimg::TextureFormat::Enum dst, const uint8_t *rgba, uint32_t width, uint32_t height, uint8_t *out,
            bimg::Quality::Enum quality = bimg::Quality::Default)
{
    bx::Error error;
    bimg::imageEncodeFromRgba8(&allocator, out, rgba, width, height, 1, dst, quality, &error);
    if (!error.isOk())
    {
        fprintf(stderr, "bimg encode to format %d failed: %.*s\n", int(dst), int(error.getMessage().getLength()),
                error.getMessage().getCPtr());
        abort();
    }
}
} // namespace

void encode_eac(EacFormat format, const uint8_t *texels, uint32_t width, uint32_t height, uint8_t *out)
{
    // Widened to RGBA8 here: bimg's own R8/RG8 input path converts every texel through floats, ~1.5x the time of
    // the whole encode.
    const uint32_t channels = eac_channels(format);
    const size_t count = size_t(width) * height;
    thread_local std::vector<uint32_t> rgba; // reused: the chains of a texture encode level after level
    rgba.resize(count);
    if (channels == 1)
        for (size_t i = 0; i < count; ++i)
            rgba[i] = texels[i];
    else
        for (size_t i = 0; i < count; ++i)
            rgba[i] = uint32_t(texels[i * 2]) | uint32_t(texels[i * 2 + 1]) << 8; // R, G first in memory (little-endian)
    encode(format == EacFormat::R11 ? bimg::TextureFormat::EACR11 : bimg::TextureFormat::EACRG11,
           reinterpret_cast<const uint8_t *>(rgba.data()), width, height, out);
}

std::vector<uint8_t> encode_eac_chain(EacFormat format, const uint8_t *texels, uint32_t width, uint32_t height,
                                      uint32_t levels)
{
    size_t total = 0;
    for (uint32_t level = 0, w = width, h = height; level < levels; ++level, w = std::max(w / 2, 1u), h = std::max(h / 2, 1u))
        total += size_t(blocks_x(w)) * blocks_x(h) * eac_block_bytes(format);
    std::vector<uint8_t> out(total);
    size_t in = 0, at = 0;
    for (uint32_t level = 0, w = width, h = height; level < levels; ++level, w = std::max(w / 2, 1u), h = std::max(h / 2, 1u))
    {
        encode_eac(format, texels + in, w, h, out.data() + at);
        in += size_t(w) * h * eac_channels(format);
        at += size_t(blocks_x(w)) * blocks_x(h) * eac_block_bytes(format);
    }
    return out;
}

void encode_gpu(GpuFormat format, const uint8_t *rgba, uint32_t aligned_width, uint32_t aligned_height, uint8_t *out)
{
    static constexpr bimg::TextureFormat::Enum kFormats[] = {bimg::TextureFormat::Unknown, bimg::TextureFormat::ETC2,
                                                             bimg::TextureFormat::ETC2A,   bimg::TextureFormat::BC1,
                                                             bimg::TextureFormat::BC2,     bimg::TextureFormat::BC3};
    // libsquish's range fit for BC: its cluster fit takes seconds per large texture on a phone core, and only the mips
    // are encoded (the game's top level stays as it is).
    encode(kFormats[static_cast<int>(format)], rgba, aligned_width, aligned_height, out,
           is_etc2(format) ? bimg::Quality::Default : bimg::Quality::Fastest);
}

} // namespace storm::texture
