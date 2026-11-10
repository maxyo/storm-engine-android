// Texture files other than the engine's own .tx: DirectDraw Surface (.dds, parsed by bimg) and
// the Caribbean Legend particle atlases (.txx). Both are normalised into the D3D-shaped layout the
// renderer's texture loader uploads through the shim, so BC1-3 data reaches the same DXT -> ETC2
// transcode and disk cache as .tx textures (see src/texture/core). Pure CPU code, no bgfx calls.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace storm::render
{

enum class TextureFileFormat : uint8_t
{
    Dxt1,  // BC1 blocks, uploaded as D3DFMT_DXT1
    Dxt3,  // BC2 blocks, D3DFMT_DXT3
    Dxt5,  // BC3 blocks, D3DFMT_DXT5
    Bgra8, // 32-bit B,G,R,A bytes, D3DFMT_A8R8G8B8 (decoded on the CPU from any other format)
};

struct TextureFile
{
    struct Level
    {
        size_t offset = 0, size = 0;
        uint32_t width = 0, height = 0;
    };

    TextureFileFormat format = TextureFileFormat::Dxt1;
    uint32_t width = 0, height = 0;
    uint32_t levels = 0; // mip levels per face, level 0 is the full size
    uint32_t faces = 1;  // 1, or 6 for a cube map in D3D face order (+X, -X, +Y, -Y, +Z, -Z)
    bool srgb = false;   // the file declares sRGB data; the D3D9-shaped pipeline samples it as stored
    bool decoded = false; // the payload was decoded on the CPU because it has no DXT1/3/5 equivalent
    std::string source;   // human-readable source format for logs, e.g. "DDS BC3 sRGB"
    std::vector<uint8_t> storage;
    std::vector<Level> subresources; // face-major: [face * levels + level]

    const Level &level(uint32_t face, uint32_t level) const { return subresources[face * levels + level]; }
    std::span<const uint8_t> data(uint32_t face, uint32_t level) const
    {
        const Level &l = this->level(face, level);
        return {storage.data() + l.offset, l.size};
    }
};

enum class TextureContainer : uint8_t
{
    Tx,  // the engine's 24-byte TX_FILE_HEADER (no magic)
    Dds, // "DDS "
    Txx, // "TXX 1.0 "
};

// Detects the container from the first bytes of a file (at least 8 are needed for TXX).
TextureContainer DetectTextureContainer(std::span<const uint8_t> head);

// Parse a whole file. On success return an empty string and fill `out` (which takes the bytes);
// otherwise return the reason the file was rejected.
std::string ParseDdsTexture(std::vector<uint8_t> bytes, TextureFile &out);
std::string ParseTxxTexture(std::vector<uint8_t> bytes, TextureFile &out);

// Texture name -> file path rule (relative to the game root, backslash separated):
//  * the name gets the "resource\textures\" prefix unless it already has it;
//  * the primary path is the name itself when it ends in .tx, .dds or .txx, otherwise name + ".tx"
//    (the original engine rule: "foo.tga" -> "foo.tga.tx");
//  * alternates, tried only when no texture path has the primary file, are the stem (the name
//    without .tx and then without .tga/.dds/.txx) with ".tga.tx", ".dds" and ".txx", in this order,
//    skipping the primary. Caribbean Legend models name a texture "x.tga" that only exists as
//    "x.dds", "x.dds" that only exists as "x.tga.tx", and particle atlases "x.txx" / "xnm".
struct TextureFileCandidates
{
    std::string primary;
    std::vector<std::string> alternates;
};
TextureFileCandidates GetTextureFileCandidates(std::string_view name);

} // namespace storm::render
