#include "effects/normal_maps.hpp"

#include "shim/device.hpp"
#include "storm/fx/lighting.hpp"
#include "texture/core/cache.hpp"
#include "texture/core/service.hpp"

#include <algorithm>
#include <cstring>

namespace storm::effects
{

namespace
{
constexpr size_t kMaxMapBytes = 64u << 20;

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

// One level as RGBA8 (R first), `width` x `height` texels. The format is DXT or uncompressed (checked by the caller).
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

// The full mip chain, as fx::DeriveNormalMap produces it.
uint32_t mip_count(uint32_t width, uint32_t height)
{
    uint32_t count = 1;
    for (uint32_t size = std::max(width, height); size > 1; size >>= 1)
        ++count;
    return count;
}
} // namespace

NormalMapEntry::~NormalMapEntry()
{
    if (bgfx::isValid(handle) && shim::Context::instance().initialised())
        bgfx::destroy(handle);
}

void NormalMaps::on_texture_loaded(shim::TexStore &store, const char *path)
{
    if (!path || store.kind != shim::TexKind::Tex2D || store.subs.empty())
        return;
    if (!fx::NameSuggestsHeightMap(fx::NormalizeTexturePath(path)))
        return; // HeightMapReason::NameNoMatch; no manifest (RESOURCE/fx_height_maps.txt) is read
    texture::DxtFormat dxt;
    if (!dxt_format(store.format, &dxt) && shim::format_info(store.format).bytes_per_pixel != 4)
    {
        if (!format_reported_)
            shim::logf("ERROR: effects: height texture %s has D3D format %u, which the normal-map derivation does not "
                       "read: no relief for it (and textures like it)",
                       path, static_cast<unsigned>(store.format));
        format_reported_ = true;
        return;
    }
    // Classify from a small level (64 px is plenty), derive from the top one.
    uint32_t level = 0;
    while (level + 1 < store.levels && std::max(store.level_width(level), store.level_height(level)) > 64)
        ++level;
    const std::vector<uint8_t> &small = store.sub(0, level).data;
    const std::vector<uint8_t> &top = store.sub(0, 0).data;
    if (small.empty() || top.empty())
    {
        shim::logf("ERROR: effects: height texture %s has no level data when it is reported: no relief for it", path);
        return;
    }
    const std::vector<uint8_t> rgba = decode_rgba(store.format, small.data(), store.level_width(level),
                                                  store.level_height(level));
    if (!classifier_.Classify(path, rgba).is_height_map)
        return;
    auto entry = std::make_shared<NormalMapEntry>();
    entry->format = store.format;
    entry->width = store.width;
    entry->height = store.height;
    entry->source = top;
    store.normal_map = entry;
    if (enabled_)
        derive(entry);
}

void NormalMaps::derive(const std::shared_ptr<NormalMapEntry> &entry)
{
    entry->state = NormalMapEntry::Pending;
    texture::Service &service = texture::Service::instance();
    service.post([entry, data = std::move(entry->source), directory = service.cache_directory(), options = options_] {
        const uint32_t width = entry->width, height = entry->height;
        const uint64_t key = texture::hash64(data.data(), data.size(),
                                             fx::NormalMapCacheKey(options) ^ (uint64_t{width} << 32 | height));
        const uint32_t mips = mip_count(width, height);
        const texture::DerivedShape shape{.magic = {'S', 'T', 'X', 'C', 'N', 'R', 'G', '8'},
                                          .version = fx::kNormalMapVersion, .width = width, .height = height,
                                          .levels = mips};
        if (!directory.empty())
            if (std::optional<texture::Blob> cached = texture::read_derived_file(directory, key, shape, kMaxMapBytes))
            {
                entry->map.width = static_cast<int>(width);
                entry->map.height = static_cast<int>(height);
                entry->map.mip_count = static_cast<int>(mips);
                entry->map.rg.assign(cached->data.get(), cached->data.get() + cached->size);
                entry->state = NormalMapEntry::Derived;
                return;
            }
        const std::vector<uint8_t> rgba = decode_rgba(entry->format, data.data(), width, height);
        std::vector<uint8_t> heights(static_cast<size_t>(width) * height);
        for (size_t i = 0; i < heights.size(); ++i)
            heights[i] = rgba[i * 4];
        entry->map = fx::DeriveNormalMap(heights, static_cast<int>(width), static_cast<int>(height), options);
        if (entry->map.rg.empty() || entry->map.mip_count != static_cast<int>(mips))
        {
            entry->state = NormalMapEntry::Failed;
            return;
        }
        if (!directory.empty())
            texture::write_derived_file(directory, key, shape, entry->map.rg.data(), entry->map.rg.size());
        entry->state = NormalMapEntry::Derived;
    });
    entry->source.clear();
}

bgfx::TextureHandle NormalMaps::handle(shim::TexStore &store, fx::Lighting &lighting)
{
    const std::shared_ptr<NormalMapEntry> &entry = store.normal_map;
    if (!entry)
        return BGFX_INVALID_HANDLE;
    switch (entry->state.load())
    {
    case NormalMapEntry::Source:
        derive(entry);
        return BGFX_INVALID_HANDLE;
    case NormalMapEntry::Derived:
        entry->handle = lighting.CreateNormalMap(entry->map); // logs why when it cannot
        entry->map = {};
        entry->state = bgfx::isValid(entry->handle) ? NormalMapEntry::Uploaded : NormalMapEntry::Failed;
        if (bgfx::isValid(entry->handle))
            shim::logf("effects: normal map %ux%u of a height texture uploaded", entry->width, entry->height);
        break;
    default:
        break;
    }
    if (entry->state == NormalMapEntry::Failed && !entry->failure_reported)
    {
        entry->failure_reported = true;
        shim::logf("ERROR: effects: no normal map for a %ux%u height texture (derivation or upload failed): no relief "
                   "for it",
                   entry->width, entry->height);
    }
    return entry->handle;
}

} // namespace storm::effects
