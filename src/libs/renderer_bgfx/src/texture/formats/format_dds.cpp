// DirectDraw Surface textures through bimg (legacy FourCC and DX10 headers, cube maps, sRGB).
// BC1-3 stay compressed (D3D DXT1/3/5, transcoded to ETC2 by the shim like .tx textures); every
// other format bimg can read is decoded to BGRA8 here, which the caller logs.
#include "storm/render/texture_file.hpp"

#include <bimg/bimg.h>
#include <bx/allocator.h>
#include <bx/error.h>

#include <algorithm>
#include <cstring>

namespace storm::render
{
namespace
{
bx::DefaultAllocator allocator;

uint32_t level_size(uint32_t size, uint32_t level) { return std::max<uint32_t>(1, size >> level); }
} // namespace

std::string ParseDdsTexture(std::vector<uint8_t> bytes, TextureFile &out)
{
    if (DetectTextureContainer(bytes) != TextureContainer::Dds)
        return "no DDS magic";
    bimg::ImageContainer image{};
    bx::Error err;
    if (!bimg::imageParse(image, bytes.data(), uint32_t(bytes.size()), &err) || !err.isOk())
        return "bimg cannot parse the DDS header: " + std::string(err.getMessage().getCPtr(), err.getMessage().getLength());
    if (image.m_depth > 1)
        return "volume textures are not supported";
    if (image.m_numLayers > 1)
        return "texture arrays are not supported";
    if (image.m_numMips == 0 || image.m_width == 0 || image.m_height == 0)
        return "empty DDS (no size or no mip levels)";
    if (image.m_cubeMap && image.m_width != image.m_height)
        return "cube map faces are not square";

    const char *format_name = bimg::getName(image.m_format);
    TextureFile file;
    file.width = image.m_width;
    file.height = image.m_height;
    file.levels = image.m_numMips;
    file.faces = image.m_cubeMap ? 6 : 1;
    file.srgb = image.m_srgb;
    switch (image.m_format)
    {
    case bimg::TextureFormat::BC1:
        file.format = TextureFileFormat::Dxt1;
        break;
    case bimg::TextureFormat::BC2:
        file.format = TextureFileFormat::Dxt3;
        break;
    case bimg::TextureFormat::BC3:
        file.format = TextureFileFormat::Dxt5;
        break;
    default:
        file.format = TextureFileFormat::Bgra8;
        file.decoded = true;
        break;
    }
    file.source = std::string("DDS ") + format_name + (image.m_srgb ? " sRGB" : "") + (image.m_cubeMap ? " cube" : "");

    std::vector<uint8_t> decoded;
    std::vector<uint8_t> scratch;
    const bimg::ImageBlockInfo &block = bimg::getBlockInfo(image.m_format);
    for (uint32_t face = 0; face < file.faces; ++face)
        for (uint32_t level = 0; level < file.levels; ++level)
        {
            bimg::ImageMip mip;
            if (!bimg::imageGetRawData(image, uint16_t(face), uint8_t(level), bytes.data(), uint32_t(bytes.size()), mip))
                return "face " + std::to_string(face) + " level " + std::to_string(level) + " is outside the file";
            TextureFile::Level sub;
            sub.width = level_size(file.width, level);
            sub.height = level_size(file.height, level);
            if (!file.decoded)
            {
                sub.offset = size_t(mip.m_data - bytes.data());
                sub.size = mip.m_size;
            }
            else
            {
                // bimg decodes whole blocks: decode the block-aligned mip, then keep the real size.
                const uint32_t aligned_w = std::max<uint32_t>(mip.m_width, block.blockWidth);
                const uint32_t aligned_h = std::max<uint32_t>(mip.m_height, block.blockHeight);
                scratch.resize(size_t(aligned_w) * aligned_h * 4);
                bimg::imageDecodeToBgra8(&allocator, scratch.data(), mip.m_data, aligned_w, aligned_h, aligned_w * 4,
                                         mip.m_format);
                sub.offset = decoded.size();
                sub.size = size_t(sub.width) * sub.height * 4;
                decoded.resize(sub.offset + sub.size);
                for (uint32_t row = 0; row < sub.height; ++row)
                    memcpy(decoded.data() + sub.offset + size_t(row) * sub.width * 4,
                           scratch.data() + size_t(row) * aligned_w * 4, size_t(sub.width) * 4);
            }
            file.subresources.push_back(sub);
        }
    file.storage = file.decoded ? std::move(decoded) : std::move(bytes);
    out = std::move(file);
    return {};
}

} // namespace storm::render
