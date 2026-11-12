#include "effects/texture_pixels.hpp"

#include "texture/core/formats.hpp"
#include "texture/core/service.hpp"

#include <algorithm>
#include <cstring>

namespace storm::effects
{

namespace
{
bool dxt_format(D3DFORMAT format, texture::DxtFormat *out)
{
    switch (format)
    {
    case D3DFMT_DXT1:
        *out = texture::DxtFormat::Bc1;
        return true;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
        *out = texture::DxtFormat::Bc2;
        return true;
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:
        *out = texture::DxtFormat::Bc3;
        return true;
    default:
        return false;
    }
}
} // namespace

std::vector<uint8_t> decode_rgba(D3DFORMAT format, const uint8_t *data, uint32_t width, uint32_t height)
{
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
    texture::DxtFormat dxt;
    if (dxt_format(format, &dxt))
    {
        const uint32_t aligned_w = texture::blocks_x(width) * 4, aligned_h = texture::blocks_x(height) * 4;
        std::vector<uint8_t> blocks(static_cast<size_t>(aligned_w) * aligned_h * 4);
        texture::decode_level(dxt, data, width, height, blocks.data());
        for (uint32_t y = 0; y < height; ++y)
            memcpy(rgba.data() + static_cast<size_t>(y) * width * 4, blocks.data() + static_cast<size_t>(y) * aligned_w * 4,
                   static_cast<size_t>(width) * 4);
    }
    else
        shim::convert_to_rgba8(format, data, shim::level_pitch(format, width), width, height, rgba.data());
    return rgba;
}

bool readable_format(D3DFORMAT format)
{
    texture::DxtFormat dxt;
    switch (format)
    {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_R8G8B8:
    case D3DFMT_R5G6B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_X4R4G4B4:
    case D3DFMT_L8:
        return true;
    default:
        return dxt_format(format, &dxt);
    }
}

std::shared_ptr<const fx::AlphaMask> texture_cutout(shim::TexStore &store)
{
    if (store.kind != shim::TexKind::Tex2D || store.subs.empty() || !readable_format(store.format))
        return nullptr;
    switch (store.format) // formats without alpha
    {
    case D3DFMT_X8R8G8B8:
    case D3DFMT_R8G8B8:
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_X4R4G4B4:
    case D3DFMT_L8:
        return nullptr;
    default:
        break;
    }
    uint32_t level = 0;
    while (level + 1 < store.levels && std::max(store.level_width(level), store.level_height(level)) > 128)
        ++level;
    const std::vector<uint8_t> &data = store.sub(0, level).data;
    if (data.empty())
        return nullptr;
    const uint32_t width = store.level_width(level), height = store.level_height(level);
    const std::vector<uint8_t> rgba = decode_rgba(store.format, data.data(), width, height);
    fx::AlphaMask mask;
    if (!fx::MakeAlphaMask(rgba, static_cast<int>(width), static_cast<int>(height), kGameAlphaReference, mask))
        return nullptr;
    return std::make_shared<const fx::AlphaMask>(std::move(mask));
}

} // namespace storm::effects
