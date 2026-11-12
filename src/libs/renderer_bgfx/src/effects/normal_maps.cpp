#include "effects/normal_maps.hpp"

#include "shim/device.hpp"
#include "storm/fx/lighting.hpp"
#include "texture/core/formats.hpp"
#include "texture/core/service.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>

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

// The formats decode_rgba reads: DXT and the game's uncompressed colour formats.
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

uint32_t mip_count(int width, int height)
{
    uint32_t count = 1;
    for (int size = std::max(width, height); size > 1; size >>= 1)
        ++count;
    return count;
}

// The derived RG8 chain as EAC RG11, level by level: half the memory on the GPU and on disk.
fx::NormalMap encode_eac(const fx::NormalMap &rg8)
{
    fx::NormalMap eac{rg8.width, rg8.height, rg8.mip_count, fx::NormalMapFormat::EacRg11, {}};
    if (rg8.rg.empty())
        return eac;
    eac.rg.resize(fx::NormalMapBytes(rg8.width, rg8.height, fx::NormalMapFormat::EacRg11));
    size_t in = 0, out = 0;
    int w = rg8.width, h = rg8.height;
    for (int level = 0; level < rg8.mip_count; ++level)
    {
        texture::encode_eac_rg11(rg8.rg.data() + in, static_cast<uint32_t>(w), static_cast<uint32_t>(h), eac.rg.data() + out);
        in += fx::NormalMapLevelBytes(fx::NormalMapFormat::Rg8, w, h);
        out += fx::NormalMapLevelBytes(fx::NormalMapFormat::EacRg11, w, h);
        w = std::max(w / 2, 1);
        h = std::max(h / 2, 1);
    }
    return eac;
}

// A map read back from the cache directory.
fx::NormalMap stored_map(const texture::DerivedShape &shape, const texture::Blob &blob)
{
    return {static_cast<int>(shape.width), static_cast<int>(shape.height), static_cast<int>(shape.levels),
            fx::NormalMapFormat::EacRg11, {blob.data.get(), blob.data.get() + blob.size}};
}

// The directory of the derived maps (ConfigureEffectsCache) and its size budget, shared with the worker tasks.
struct CacheDirectory
{
    std::mutex mutex;
    std::filesystem::path path;
    uint64_t max_bytes = 0;
    uint64_t total = 0;
    bool scanned = false;
};
CacheDirectory &cache_directory()
{
    static CacheDirectory directory;
    return directory;
}

std::filesystem::path cache_path()
{
    CacheDirectory &cache = cache_directory();
    std::lock_guard lock(cache.mutex);
    return cache.path;
}

// Worker: after writing `written` bytes, evicts the least recently used maps beyond the budget.
void account_write(uint64_t written)
{
    CacheDirectory &cache = cache_directory();
    std::lock_guard lock(cache.mutex);
    if (cache.path.empty())
        return;
    if (!cache.scanned)
    {
        cache.total = texture::scan_cache(cache.path);
        cache.scanned = true;
    }
    cache.total += written;
    if (cache.total > cache.max_bytes)
        cache.total = texture::evict_cache(cache.path, cache.max_bytes / 10 * 9);
}
} // namespace

void ConfigureNormalMapCache(const std::filesystem::path &directory, uint64_t max_bytes)
{
    CacheDirectory &cache = cache_directory();
    std::lock_guard lock(cache.mutex);
    cache.path = directory;
    cache.max_bytes = max_bytes;
    cache.scanned = false;
    std::error_code error;
    if (!directory.empty() && !std::filesystem::create_directories(directory, error) && error)
    {
        shim::logf("ERROR: effects: normal-map cache %s cannot be created (%s): every map is derived again on each load",
                   directory.string().c_str(), error.message().c_str());
        cache.path.clear();
    }
}

NormalMapEntry::~NormalMapEntry()
{
    if (bgfx::isValid(handle) && shim::Context::instance().initialised())
        bgfx::destroy(handle);
}

void NormalMaps::configure(bool enabled, int max_size)
{
    const bool changed = enabled != enabled_ || max_size != max_size_;
    if (enabled && !enabled_ && loaded_while_off_)
        shim::logf("effects: relief switched on: textures loaded while it was off get their normal maps when they are "
                   "loaded again (the next location)");
    enabled_ = enabled;
    max_size_ = max_size;
    if (!changed)
        return;
    std::erase_if(entries_, [](const std::weak_ptr<NormalMapEntry> &entry) { return entry.expired(); });
    for (const std::weak_ptr<NormalMapEntry> &weak : entries_)
        if (const std::shared_ptr<NormalMapEntry> entry = weak.lock())
            release(*entry);
}

void NormalMaps::release(NormalMapEntry &entry)
{
    if (entry.state != NormalMapEntry::Uploaded)
        return;
    if (bgfx::isValid(entry.handle))
        bgfx::destroy(entry.handle);
    entry.handle = BGFX_INVALID_HANDLE;
    entry.bytes = 0;
    entry.wanted = false;
    entry.state = NormalMapEntry::Stored; // read back from the cache when drawn again
}

void NormalMaps::on_texture_loaded(shim::TexStore &store, const char *path)
{
    if (!path || store.kind != shim::TexKind::Tex2D || store.subs.empty())
        return;
    if (!enabled_)
    {
        loaded_while_off_ = true;
        return;
    }
    const std::string name = fx::NormalizeTexturePath(path);
    const bool readable = readable_format(store.format);
    // Only "bump" names need the pixels (grey or not); classify from a small level (64 px is plenty).
    std::vector<uint8_t> small;
    if (fx::NameSuggestsHeightMap(name) && readable)
    {
        uint32_t level = 0;
        while (level + 1 < store.levels && std::max(store.level_width(level), store.level_height(level)) > 64)
            ++level;
        const std::vector<uint8_t> &data = store.sub(0, level).data;
        if (!data.empty())
            small = decode_rgba(store.format, data.data(), store.level_width(level), store.level_height(level));
    }
    const fx::ReliefVerdict verdict = classifier_.Classify(path, small);
    if (verdict.relief == fx::ReliefClass::None)
        return;
    if (!readable)
    {
        if (!format_reported_)
            shim::logf("ERROR: effects: texture %s (relief %s) has D3D format %u, which the normal-map derivation does "
                       "not read: no relief for it (and textures like it)",
                       name.c_str(), fx::ReliefClassName(verdict.relief), static_cast<unsigned>(store.format));
        format_reported_ = true;
        return;
    }
    const std::vector<uint8_t> &top = store.sub(0, 0).data;
    if (top.empty())
    {
        shim::logf("ERROR: effects: texture %s has no level data when it is reported: no relief for it", name.c_str());
        return;
    }
    const std::filesystem::path directory = cache_path();
    if (directory.empty() && !cache_reported_)
    {
        cache_reported_ = true;
        shim::logf("ERROR: effects: no normal-map cache directory (ConfigureEffectsCache): maps are derived on every "
                   "load and kept in memory until drawn");
    }

    auto entry = std::make_shared<NormalMapEntry>();
    entry->name = name;
    entry->relief = verdict.relief;
    entry->options = fx::ReliefOptions(verdict.relief);
    entry->options.max_size = max_size_; // derived at the preset's size: a smaller preset derives (and stores) less
    int width, height;
    fx::NormalMapSize(static_cast<int>(store.width), static_cast<int>(store.height), entry->options, width, height);
    entry->shape = {.magic = {'S', 'T', 'X', 'C', 'N', 'E', 'A', 'C'}, .version = fx::kNormalMapVersion,
                    .width = static_cast<uint32_t>(width), .height = static_cast<uint32_t>(height),
                    .levels = mip_count(width, height)};
    store.normal_map = entry;
    entries_.push_back(entry);

    texture::Service::instance().post([entry, data = top, format = store.format, source_width = store.width,
                                       source_height = store.height, directory, counters = counters_] {
        entry->key = texture::hash64(data.data(), data.size(),
                                     fx::NormalMapCacheKey(entry->options) ^ (uint64_t{source_width} << 32 | source_height));
        if (!directory.empty())
            if (std::optional<texture::Blob> cached = texture::read_derived_file(directory, entry->key, entry->shape, kMaxMapBytes))
            {
                ++counters->cached;
                if (!entry->wanted)
                {
                    entry->state = NormalMapEntry::Stored;
                    return;
                }
                entry->map = stored_map(entry->shape, *cached);
                entry->state = NormalMapEntry::Derived;
                return;
            }
        const auto start = std::chrono::steady_clock::now();
        const std::vector<uint8_t> rgba = decode_rgba(format, data.data(), source_width, source_height);
        fx::NormalMap map = encode_eac(
            fx::DeriveNormalMap(rgba, static_cast<int>(source_width), static_cast<int>(source_height), entry->options));
        counters->derive_us += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
        ++counters->derived;
        if (map.rg.empty() || map.mip_count != static_cast<int>(entry->shape.levels) ||
            map.width != static_cast<int>(entry->shape.width))
        {
            entry->state = NormalMapEntry::Failed;
            return;
        }
        if (!directory.empty())
        {
            account_write(texture::write_derived_file(directory, entry->key, entry->shape, map.rg.data(), map.rg.size()));
            if (!entry->wanted)
            {
                entry->state = NormalMapEntry::Stored;
                return;
            }
        }
        entry->map = std::move(map);
        entry->state = NormalMapEntry::Derived;
    });
}

void NormalMaps::read_cached(const std::shared_ptr<NormalMapEntry> &entry)
{
    entry->state = NormalMapEntry::Pending;
    texture::Service::instance().post([entry, directory = cache_path()] {
        std::optional<texture::Blob> cached;
        if (!directory.empty())
            cached = texture::read_derived_file(directory, entry->key, entry->shape, kMaxMapBytes);
        if (!cached)
        {
            // Evicted (or the directory went away) while the texture stayed loaded.
            entry->state = NormalMapEntry::Failed;
            return;
        }
        entry->map = stored_map(entry->shape, *cached);
        entry->state = NormalMapEntry::Derived;
    });
}

bgfx::TextureHandle NormalMaps::handle(shim::TexStore &store, fx::Lighting &lighting)
{
    const std::shared_ptr<NormalMapEntry> &entry = store.normal_map;
    if (!entry)
        return BGFX_INVALID_HANDLE;
    entry->wanted = true;
    switch (entry->state.load())
    {
    case NormalMapEntry::Stored:
        read_cached(entry);
        return BGFX_INVALID_HANDLE;
    case NormalMapEntry::Derived:
        fx::DropTopMips(entry->map, max_size_);
        entry->handle = lighting.CreateNormalMap(entry->map); // logs why when it cannot
        entry->bytes = entry->map.rg.size();
        entry->map = {};
        entry->state = bgfx::isValid(entry->handle) ? NormalMapEntry::Uploaded : NormalMapEntry::Failed;
        break;
    default:
        break;
    }
    if (entry->state == NormalMapEntry::Failed && !entry->failure_reported)
    {
        entry->failure_reported = true;
        shim::logf("ERROR: effects: no normal map for texture %s (relief %s): derivation, cache read or upload failed",
                   entry->name.c_str(), fx::ReliefClassName(entry->relief));
    }
    return entry->handle;
}

NormalMaps::Stats NormalMaps::stats()
{
    Stats stats;
    std::erase_if(entries_, [](const std::weak_ptr<NormalMapEntry> &entry) { return entry.expired(); });
    for (const std::weak_ptr<NormalMapEntry> &weak : entries_)
        if (const std::shared_ptr<NormalMapEntry> entry = weak.lock(); entry && entry->state == NormalMapEntry::Uploaded)
        {
            ++stats.resident;
            stats.bytes += entry->bytes;
        }
    stats.derived = counters_->derived;
    stats.cached = counters_->cached;
    stats.derive_ms = static_cast<double>(counters_->derive_us) / 1000.0;
    return stats;
}

void NormalMaps::end_frame()
{
    if (++frames_ < 300)
        return;
    frames_ = 0;
    const Stats now = stats();
    if (now.resident == logged_.resident && now.bytes == logged_.bytes && now.derived == logged_.derived &&
        now.cached == logged_.cached)
        return;
    logged_ = now;
    shim::logf("effects relief: %u normal maps resident (%.1f MB, max %d px); since start %u derived (%.0f ms on texture "
               "workers), %u read from the cache",
               now.resident, static_cast<double>(now.bytes) / 1048576.0, max_size_, now.derived, now.derive_ms, now.cached);
}

} // namespace storm::effects
