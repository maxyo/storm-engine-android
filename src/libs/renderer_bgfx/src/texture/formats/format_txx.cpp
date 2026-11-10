// Caribbean Legend particle atlases (Resource/Textures/Particles*.txx). Layout derived from the
// data files (local spec, no disassembly): "TXX 1.0 " followed by nine int32
//   [1, format, width, height, 1, levels, row_pitch, bits_per_pixel, 0]
// and the tightly packed mip chain from level 0 at byte 44. Format 9 is BC1 (4 bits per pixel),
// format 11 is BC3 (8 bits per pixel); row_pitch is width * bits_per_pixel / 8.
#include "storm/render/texture_file.hpp"

#include <algorithm>
#include <cstring>

namespace storm::render
{
namespace
{
constexpr size_t kHeaderSize = 44;

struct TxxFormat
{
    int32_t id;
    int32_t bits_per_pixel;
    TextureFileFormat format;
    const char *name;
};
constexpr TxxFormat kFormats[] = {
    {9, 4, TextureFileFormat::Dxt1, "BC1"},
    {11, 8, TextureFileFormat::Dxt5, "BC3"},
};
} // namespace

std::string ParseTxxTexture(std::vector<uint8_t> bytes, TextureFile &out)
{
    if (bytes.size() < kHeaderSize || DetectTextureContainer(bytes) != TextureContainer::Txx)
        return "no TXX 1.0 header";
    int32_t field[9];
    memcpy(field, bytes.data() + 8, sizeof(field));
    const auto *format = std::ranges::find_if(kFormats, [&](const TxxFormat &f) { return f.id == field[1]; });
    if (format == std::end(kFormats))
        return "unknown TXX format " + std::to_string(field[1]);
    const int32_t width = field[2], height = field[3], levels = field[5];
    if (field[0] != 1 || field[4] != 1 || field[8] != 0)
        return "unexpected TXX header fields " + std::to_string(field[0]) + "/" + std::to_string(field[4]) + "/" +
               std::to_string(field[8]);
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 || levels <= 0 || levels > 15)
        return "bad TXX size " + std::to_string(width) + "x" + std::to_string(height) + ", " + std::to_string(levels) +
               " levels";
    if (field[7] != format->bits_per_pixel || int64_t(field[6]) * 8 != int64_t(width) * field[7])
        return "TXX pitch " + std::to_string(field[6]) + " / bits per pixel " + std::to_string(field[7]) +
               " do not match format " + format->name;

    TextureFile file;
    file.format = format->format;
    file.width = uint32_t(width);
    file.height = uint32_t(height);
    file.levels = uint32_t(levels);
    file.source = std::string("TXX ") + format->name;
    const size_t block_bytes = format->format == TextureFileFormat::Dxt1 ? 8 : 16;
    size_t offset = kHeaderSize;
    for (uint32_t level = 0; level < file.levels; ++level)
    {
        TextureFile::Level sub;
        sub.width = std::max<uint32_t>(1, file.width >> level);
        sub.height = std::max<uint32_t>(1, file.height >> level);
        sub.offset = offset;
        sub.size = size_t((sub.width + 3) / 4) * ((sub.height + 3) / 4) * block_bytes;
        offset += sub.size;
        file.subresources.push_back(sub);
    }
    if (offset != bytes.size())
        return "TXX mip chain needs " + std::to_string(offset) + " bytes, the file has " + std::to_string(bytes.size());
    file.storage = std::move(bytes);
    out = std::move(file);
    return {};
}

} // namespace storm::render
