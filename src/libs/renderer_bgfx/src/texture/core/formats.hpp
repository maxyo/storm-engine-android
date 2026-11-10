// DXT (BC1-3) -> ETC2 transcode: formats, block decoding and ETC2 encoding.
// Pure CPU code without bgfx; used by the shim (src/shim/objects.cpp) and the
// worker threads of the transcode service.
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
    return size_t(blocks_x(w)) * blocks_x(h) * dxt_block_bytes(format);
}
inline size_t etc_level_size(EtcFormat format, uint32_t w, uint32_t h)
{
    return size_t(blocks_x(w)) * blocks_x(h) * etc_block_bytes(format);
}
// Size of all levels and faces of the D3D payload.
size_t dxt_payload_size(const Layout &layout);

// Decodes one 4x4 block to 64 bytes RGBA8 (R first, rows of 4 texels).
// BC1 blocks use the 3-colour + transparent mode when color0 <= color1.
void decode_bc1_block(const uint8_t *block, uint8_t *rgba, bool four_colour_only = false);
void decode_bc2_block(const uint8_t *block, uint8_t *rgba);
void decode_bc3_block(const uint8_t *block, uint8_t *rgba);
// True if a BC1 block contains a transparent texel.
bool bc1_block_has_transparency(const uint8_t *block);
bool bc1_has_transparency(const uint8_t *data, size_t size);

// Decodes one level into a block-aligned image of blocks_x(w)*4 x blocks_x(h)*4 texels.
void decode_level(DxtFormat format, const uint8_t *src, uint32_t width, uint32_t height, uint8_t *rgba);

// ETC2 format chosen for a DXT payload (punch-through BC1 needs alpha: etcpak
// has no RGB8A1 encoder, so such textures go to RGBA8/EAC).
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
