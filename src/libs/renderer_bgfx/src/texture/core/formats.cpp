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

EtcFormat choose_etc_format(DxtFormat format, const uint8_t *payload, size_t size)
{
    if (format != DxtFormat::Bc1)
        return EtcFormat::Rgba8;
    return bc1_has_transparency(payload, size) ? EtcFormat::Rgba8 : EtcFormat::Rgb8;
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

size_t tx_level_bytes(uint32_t format, uint32_t width, uint32_t height)
{
    switch (static_cast<TxFormat>(format))
    {
    case TxFormat::A8R8G8B8:
    case TxFormat::X8R8G8B8:
        return size_t(width) * height * 4;
    case TxFormat::R5G6B5:
    case TxFormat::A1R5G5B5:
    case TxFormat::A4R4G4B4:
        return size_t(width) * height * 2;
    case TxFormat::Dxt1:
        return dxt_level_size(DxtFormat::Bc1, width, height);
    case TxFormat::Dxt3:
        return dxt_level_size(DxtFormat::Bc2, width, height);
    case TxFormat::Dxt5:
        return dxt_level_size(DxtFormat::Bc3, width, height);
    }
    return 0;
}

bool decode_tx_level(uint32_t format, const uint8_t *data, uint32_t width, uint32_t height, uint8_t *rgba)
{
    const size_t texels = size_t(width) * height;
    switch (static_cast<TxFormat>(format))
    {
    case TxFormat::Dxt1:
    case TxFormat::Dxt3:
    case TxFormat::Dxt5:
    {
        const DxtFormat dxt = static_cast<TxFormat>(format) == TxFormat::Dxt1   ? DxtFormat::Bc1
                              : static_cast<TxFormat>(format) == TxFormat::Dxt3 ? DxtFormat::Bc2
                                                                                 : DxtFormat::Bc3;
        const uint32_t aligned_width = blocks_x(width) * 4, aligned_height = blocks_x(height) * 4;
        std::vector<uint8_t> blocks(size_t(aligned_width) * aligned_height * 4);
        decode_level(dxt, data, width, height, blocks.data());
        for (uint32_t y = 0; y < height; ++y)
            memcpy(rgba + size_t(y) * width * 4, blocks.data() + size_t(y) * aligned_width * 4, size_t(width) * 4);
        return true;
    }
    case TxFormat::A8R8G8B8:
    case TxFormat::X8R8G8B8:
        for (size_t i = 0; i < texels; ++i)
        {
            const uint8_t *s = data + i * 4;
            uint8_t *d = rgba + i * 4;
            d[0] = s[2], d[1] = s[1], d[2] = s[0];
            d[3] = static_cast<TxFormat>(format) == TxFormat::A8R8G8B8 ? s[3] : 255;
        }
        return true;
    case TxFormat::R5G6B5:
    case TxFormat::A1R5G5B5:
    case TxFormat::A4R4G4B4:
        for (size_t i = 0; i < texels; ++i)
        {
            const uint32_t v = data[i * 2] | (data[i * 2 + 1] << 8);
            uint8_t *d = rgba + i * 4;
            if (static_cast<TxFormat>(format) == TxFormat::R5G6B5)
            {
                const uint32_t r = v >> 11, g = (v >> 5) & 63, b = v & 31;
                d[0] = uint8_t((r << 3) | (r >> 2)), d[1] = uint8_t((g << 2) | (g >> 4)), d[2] = uint8_t((b << 3) | (b >> 2));
                d[3] = 255;
            }
            else if (static_cast<TxFormat>(format) == TxFormat::A1R5G5B5)
            {
                const uint32_t r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
                d[0] = uint8_t((r << 3) | (r >> 2)), d[1] = uint8_t((g << 3) | (g >> 2)), d[2] = uint8_t((b << 3) | (b >> 2));
                d[3] = (v & 0x8000) ? 255 : 0;
            }
            else
            {
                d[0] = uint8_t(((v >> 8) & 15) * 17), d[1] = uint8_t(((v >> 4) & 15) * 17), d[2] = uint8_t((v & 15) * 17);
                d[3] = uint8_t((v >> 12) * 17);
            }
        }
        return true;
    }
    return false;
}

namespace
{
// Encodes RGBA8 with bimg (etcpak inside); an encoder error is a programming error (sizes and formats are ours).
void encode(bimg::TextureFormat::Enum dst, const uint8_t *rgba, uint32_t width, uint32_t height, uint8_t *out)
{
    bx::Error error;
    bimg::imageEncodeFromRgba8(&allocator, out, rgba, width, height, 1, dst, bimg::Quality::Default, &error);
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

void encode_etc(EtcFormat format, const uint8_t *rgba, uint32_t aligned_width, uint32_t aligned_height, uint8_t *out)
{
    encode(format == EtcFormat::Rgb8 ? bimg::TextureFormat::ETC2 : bimg::TextureFormat::ETC2A, rgba, aligned_width,
           aligned_height, out);
}

} // namespace storm::texture
