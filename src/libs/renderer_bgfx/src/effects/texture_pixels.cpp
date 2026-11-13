#include "effects/texture_pixels.hpp"

#include "texture/core/formats.hpp"

#include <algorithm>
#include <vector>

namespace storm::effects
{

std::shared_ptr<const fx::AlphaMask> texture_cutout(shim::TexStore &store)
{
    if (store.kind != shim::TexKind::Tex2D || store.subs.empty())
        return nullptr;
    const texture::D3dFormat format = texture::d3d_format(static_cast<uint32_t>(store.format));
    // formats without alpha, and the float bone palettes
    if (format.format == bimg::TextureFormat::Unknown || format.opaque || format.format == bimg::TextureFormat::B5G6R5 ||
        format.format == bimg::TextureFormat::RGBA32F)
        return nullptr;
    uint32_t level = 0;
    while (level + 1 < store.levels && std::max(store.level_width(level), store.level_height(level)) > 128)
        ++level;
    const std::vector<uint8_t> &data = store.sub(0, level).data;
    if (data.empty())
        return nullptr;
    const uint32_t width = store.level_width(level), height = store.level_height(level);
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
    texture::decode_rgba8(static_cast<uint32_t>(store.format), data.data(), width, height, rgba.data());
    fx::AlphaMask mask;
    if (!fx::MakeAlphaMask(rgba, static_cast<int>(width), static_cast<int>(height), kGameAlphaReference, mask))
        return nullptr;
    return std::make_shared<const fx::AlphaMask>(std::move(mask));
}

std::shared_ptr<const fx::BounceTexture> texture_albedo(shim::TexStore &store)
{
    if (store.kind != shim::TexKind::Tex2D || store.subs.empty())
        return nullptr;
    const texture::D3dFormat format = texture::d3d_format(static_cast<uint32_t>(store.format));
    if (format.format == bimg::TextureFormat::Unknown || format.format == bimg::TextureFormat::RGBA32F)
        return nullptr;
    uint32_t level = 0;
    while (level + 1 < store.levels && std::max(store.level_width(level), store.level_height(level)) > 16)
        ++level;
    const std::vector<uint8_t> &data = store.sub(0, level).data;
    if (data.empty())
        return nullptr;
    const uint32_t width = store.level_width(level), height = store.level_height(level);
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
    if (!texture::decode_rgba8(static_cast<uint32_t>(store.format), data.data(), width, height, rgba.data()))
        return nullptr;
    auto out = std::make_shared<fx::BounceTexture>();
    out->width = static_cast<int>(width);
    out->height = static_cast<int>(height);
    out->srgb.resize(static_cast<size_t>(width) * height * 3);
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i)
        std::copy_n(&rgba[i * 4], 3, &out->srgb[i * 3]);
    return out;
}

} // namespace storm::effects
