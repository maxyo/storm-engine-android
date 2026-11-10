// DXT (BC1-3) or RGBA8 -> ETC2 transcode: formats, block decoding (bimg) and ETC2 encoding.
// Pure CPU code without bgfx; used by the shim (src/shim/objects.cpp), the Effekseer texture
// loader (decoded PNG/TGA, effekseer/src/textures.cpp) and the worker threads of the service.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace storm::texture
{

enum class DxtFormat : uint8_t
{
    Bc1 = 1, // DXT1 (with optional 1-bit punch-through alpha)
    Bc2 = 2, // DXT2/DXT3
    Bc3 = 3, // DXT4/DXT5
    Rgba8 = 4, // uncompressed RGBA8 (R first), e.g. a decoded PNG with its mip chain
};

enum class EtcFormat : uint8_t
{
    Rgb8 = 1,  // ETC2 RGB8
    Rgba8 = 2, // ETC2 RGBA8 (EAC alpha)
};

// Shape of a D3D texture: `faces` is 1 or 6; levels start at the full size.
struct Layout
{
    DxtFormat format = DxtFormat::Bc1;
    uint32_t width = 0, height = 0, levels = 1, faces = 1;
    uint32_t level_width(uint32_t level) const { return width >> level ? width >> level : 1; }
    uint32_t level_height(uint32_t level) const { return height >> level ? height >> level : 1; }
};

inline uint32_t blocks_x(uint32_t width) { return (width + 3) / 4; }
inline uint32_t dxt_block_bytes(DxtFormat format) { return format == DxtFormat::Bc1 ? 8 : 16; }
inline uint32_t etc_block_bytes(EtcFormat format) { return format == EtcFormat::Rgb8 ? 8 : 16; }
inline size_t dxt_level_size(DxtFormat format, uint32_t w, uint32_t h)
{
    if (format == DxtFormat::Rgba8)
        return size_t(w) * h * 4;
    return size_t(blocks_x(w)) * blocks_x(h) * dxt_block_bytes(format);
}
// Levels of a full mip chain down to 1x1.
inline uint32_t full_mip_count(uint32_t w, uint32_t h)
{
    uint32_t levels = 1;
    for (uint32_t size = w > h ? w : h; size > 1; size >>= 1)
        ++levels;
    return levels;
}
inline size_t etc_level_size(EtcFormat format, uint32_t w, uint32_t h)
{
    return size_t(blocks_x(w)) * blocks_x(h) * etc_block_bytes(format);
}
// Size of all levels and faces of the D3D payload.
size_t dxt_payload_size(const Layout &layout);

// True if a BC1 block contains a transparent texel.
bool bc1_block_has_transparency(const uint8_t *block);
bool bc1_has_transparency(const uint8_t *data, size_t size);

// Decodes one level with bimg into a block-aligned RGBA8 image (R first) of
// blocks_x(w)*4 x blocks_x(h)*4 texels (Rgba8 levels are copied, edges repeated). BC1 blocks use the 3-colour + transparent
// mode when color0 <= color1; the colour part of BC2/BC3 is always 4-colour.
void decode_level(DxtFormat format, const uint8_t *src, uint32_t width, uint32_t height, uint8_t *rgba);

// Builds `levels` RGBA8 levels (level-major, R first) from the top level with a 2x2 box filter;
// the result is the payload of an Rgba8 layout with one face.
std::vector<uint8_t> build_rgba_mips(const uint8_t *rgba, uint32_t width, uint32_t height, uint32_t levels);

// ETC2 format chosen for a DXT payload (punch-through BC1 needs alpha: etcpak
// has no RGB8A1 encoder, so such textures go to RGBA8/EAC); Rgba8 sources get alpha unless
// every texel is opaque.
EtcFormat choose_etc_format(DxtFormat format, const uint8_t *payload, size_t size);

struct EncodeScratch
{
    std::vector<uint32_t> pixels;
    std::vector<uint64_t> blocks;
};
// Encodes a block-aligned RGBA8 image (R first). `scratch` is reused between calls.
void encode_level(EtcFormat format, const uint8_t *rgba, uint32_t aligned_width, uint32_t aligned_height,
                  uint8_t *out, EncodeScratch &scratch);

} // namespace storm::texture
