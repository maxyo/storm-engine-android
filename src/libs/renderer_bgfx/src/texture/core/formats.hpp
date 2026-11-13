// The engine's texture formats as bimg formats, decoding (bimg) and ETC2/EAC encoding (bimg_encode) for the
// DXT (BC1-3) -> ETC2 transcode and derived maps. CPU code without bgfx; used by the shim (src/shim/objects.cpp),
// the effects and the worker threads of the transcode service.
#pragma once

#include <bimg/bimg.h>

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

// What the transcode workers make of a DXT texture (service.hpp): its mip chain rebuilt from the top level
// (mips.hpp), encoded for the GPU's tier: ETC2, or the DXT format it came in on GPUs with S3TC.
enum class GpuFormat : uint8_t
{
    Etc2Rgb8 = 1,  // ETC2 RGB8
    Etc2Rgba8 = 2, // ETC2 RGBA8 (EAC alpha)
    Bc1 = 3,       // DXT1 (transparent texels, if any: punch-through, cut at kCutoutReference)
    Bc2 = 4,       // DXT3
    Bc3 = 5,       // DXT5
};
inline bool is_etc2(GpuFormat format) { return format == GpuFormat::Etc2Rgb8 || format == GpuFormat::Etc2Rgba8; }

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
inline uint32_t gpu_block_bytes(GpuFormat format) { return format == GpuFormat::Etc2Rgb8 || format == GpuFormat::Bc1 ? 8 : 16; }
inline size_t dxt_level_size(DxtFormat format, uint32_t w, uint32_t h)
{
    return size_t(blocks_x(w)) * blocks_x(h) * dxt_block_bytes(format);
}
inline size_t gpu_level_size(GpuFormat format, uint32_t w, uint32_t h)
{
    return size_t(blocks_x(w)) * blocks_x(h) * gpu_block_bytes(format);
}
// Size of all levels and faces of the D3D payload.
size_t dxt_payload_size(const Layout &layout);

// True if a BC1 block contains a transparent texel.
bool bc1_block_has_transparency(const uint8_t *block);
bool bc1_has_transparency(const uint8_t *data, size_t size);

// Decodes one level with bimg into a block-aligned RGBA8 image (R first) of
// blocks_x(w)*4 x blocks_x(h)*4 texels. BC1 blocks use the 3-colour + transparent
// mode when color0 <= color1; the colour part of BC2/BC3 is always 4-colour.
void decode_level(DxtFormat format, const uint8_t *src, uint32_t width, uint32_t height, uint8_t *rgba);

// The format for a DXT payload on the ETC2 tier (`bc` false: punch-through BC1 needs alpha and bimg has no RGB8A1
// encoder, so such textures go to RGBA8/EAC) or the BC tier (`bc` true: the DXT format it came in).
GpuFormat choose_gpu_format(DxtFormat format, const uint8_t *payload, size_t size, bool bc);

enum class EacFormat : uint8_t
{
    R11 = 1,  // one EAC block per 4x4 texels (8 bytes) from 1-byte texels
    Rg11 = 2, // two EAC blocks per 4x4 texels (R, then G: 16 bytes) from 2-byte texels
};
inline uint32_t eac_channels(EacFormat format) { return format == EacFormat::R11 ? 1 : 2; }
inline uint32_t eac_block_bytes(EacFormat format) { return format == EacFormat::R11 ? 8 : 16; }
// One level of width x height texels (eac_channels() bytes each) as EAC; edge texels are repeated up to the block
// size. `out`: blocks_x(width) * blocks_x(height) * eac_block_bytes() bytes.
void encode_eac(EacFormat format, const uint8_t *texels, uint32_t width, uint32_t height, uint8_t *out);
// A whole mip chain (levels halving from width x height, top first, tightly packed) as EAC levels.
std::vector<uint8_t> encode_eac_chain(EacFormat format, const uint8_t *texels, uint32_t width, uint32_t height,
                                      uint32_t levels);

// The colour formats of the engine's textures by their D3DFORMAT code (the game's .tx files use the same numbers,
// TX_FORMAT), as the bimg format that holds the same bytes. `opaque`: an X format, whose unused bits read as 1
// (alpha 1); bimg has no such formats, so those bits are set before use (set_opaque). Unknown for any other code.
struct D3dFormat
{
    bimg::TextureFormat::Enum format = bimg::TextureFormat::Unknown;
    bool opaque = false;
};
D3dFormat d3d_format(uint32_t d3d);
// Sets the unused bits of `count` texels of an opaque D3D format to 1, in place.
void set_opaque(bimg::TextureFormat::Enum format, uint8_t *texels, size_t count);
// Size of one level of a d3d_format() code (block formats: whole 4x4 blocks); 0 for an unknown code.
size_t d3d_level_bytes(uint32_t d3d, uint32_t width, uint32_t height);
// One level as RGBA8 (R first), width x height texels, from `data` of d3d_level_bytes(). False for an unknown code.
bool decode_rgba8(uint32_t d3d, const uint8_t *data, uint32_t width, uint32_t height, uint8_t *rgba);

// The formats a .tx file may hold (its header's TX_FORMAT), the subset of d3d_format() the game's files use.
enum class TxFormat : uint32_t
{
    A8R8G8B8 = 21,
    X8R8G8B8 = 22,
    R5G6B5 = 23,
    A1R5G5B5 = 25,
    A4R4G4B4 = 26,
    Dxt1 = 0x31545844, // 'DXT1'
    Dxt3 = 0x33545844,
    Dxt5 = 0x35545844,
};
bool tx_format_known(uint32_t format);

// Encodes a block-aligned RGBA8 image (R first) of aligned_width x aligned_height texels (bimg_encode: etcpak for
// ETC2, libsquish for BC; BC1 makes texels with alpha below 128 transparent).
void encode_gpu(GpuFormat format, const uint8_t *rgba, uint32_t aligned_width, uint32_t aligned_height, uint8_t *out);

} // namespace storm::texture
