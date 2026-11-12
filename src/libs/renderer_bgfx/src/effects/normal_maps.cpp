#include "effects/normal_maps.hpp"

#include "shim/device.hpp"
#include "storm/fx/lighting.hpp"
#include "storm/fx/relief_store.hpp"
#include "texture/core/formats.hpp"
#include "texture/core/service.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>

namespace storm::effects
{

namespace
{
// The relief directory (ConfigureReliefDirectory), shared with the worker tasks.
std::mutex directory_mutex;
std::filesystem::path relief_directory;

std::filesystem::path directory()
{
    std::lock_guard lock(directory_mutex);
    return relief_directory;
}
} // namespace

void ConfigureReliefDirectory(const std::filesystem::path &path)
{
    std::lock_guard lock(directory_mutex);
    relief_directory = path;
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
    entry.state = NormalMapEntry::Stored; // read back from the relief directory when drawn again
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
    const uint32_t format = static_cast<uint32_t>(store.format);
    const bool readable = texture::tx_format_known(format);
    // The probe level: the key's part of the texture, and where "bump" names are checked for grey (fx/relief_store.hpp).
    const int probe = fx::ReliefProbeLevel(static_cast<int>(store.width), static_cast<int>(store.height),
                                           static_cast<int>(store.levels));
    const uint32_t pw = probe >= 0 ? store.level_width(static_cast<uint32_t>(probe)) : 0,
                   ph = probe >= 0 ? store.level_height(static_cast<uint32_t>(probe)) : 0;
    const std::vector<uint8_t> *probe_data = probe >= 0 ? &store.sub(0, static_cast<uint32_t>(probe)).data : nullptr;
    const size_t probe_bytes = readable && probe >= 0 ? texture::tx_level_bytes(format, pw, ph) : 0;
    std::vector<uint8_t> small;
    if (fx::NameSuggestsHeightMap(name) && probe_bytes && probe_data->size() >= probe_bytes)
    {
        small.resize(size_t(pw) * ph * 4);
        texture::decode_tx_level(format, probe_data->data(), pw, ph, small.data());
    }
    const fx::ReliefVerdict verdict = classifier_.Classify(path, small);
    if (verdict.relief == fx::ReliefClass::None)
        return;
    if (!readable)
    {
        if (!format_reported_)
            shim::logf("ERROR: effects: texture %s (relief %s) has D3D format %u, which the relief does not read: no "
                       "relief for it (and textures like it)",
                       name.c_str(), fx::ReliefClassName(verdict.relief), format);
        format_reported_ = true;
        return;
    }
    if (!probe_bytes || probe_data->size() < probe_bytes)
        return; // smaller than a 4x4 block below 64 px: no relief (the import step skips it too)
    const std::filesystem::path relief_dir = directory();
    if (relief_dir.empty())
    {
        if (!directory_reported_)
            shim::logf("ERROR: effects: no relief directory (render::ConfigureReliefDirectory): no relief at all");
        directory_reported_ = true;
        return;
    }

    auto entry = std::make_shared<NormalMapEntry>();
    entry->name = name;
    entry->relief = verdict.relief;
    const bool height_map = verdict.relief == fx::ReliefClass::HeightMap;
    entry->key = fx::ReliefKey(name, {probe_data->data(), probe_bytes}, static_cast<int>(pw), static_cast<int>(ph),
                               height_map ? fx::kReliefHeightMethod : fx::kReliefMethodVersion, fx::kReliefMapSize);
    store.normal_map = entry;
    entries_.push_back(entry);

    std::vector<uint8_t> source; // height maps: the top level they are derived from, when they have no map yet
    if (height_map)
        source = store.sub(0, 0).data;
    texture::Service::instance().post([entry, relief_dir, height_map, source = std::move(source), format,
                                       width = store.width, height = store.height, counters = counters_] {
        if (fx::HasReliefMap(relief_dir, entry->key))
        {
            entry->state = NormalMapEntry::Stored;
            return;
        }
        if (!height_map)
        {
            shim::logd("effects: relief: no prepared map for %s", entry->name.c_str());
            entry->state = NormalMapEntry::Missing; // not prepared: no relief (counted in the "effects relief:" line)
            return;
        }
        const auto start = std::chrono::steady_clock::now();
        std::vector<uint8_t> rgba(size_t(width) * height * 4);
        texture::decode_tx_level(format, source.data(), width, height, rgba.data());
        fx::NormalMapOptions options = fx::ReliefOptions(fx::ReliefClass::HeightMap);
        options.max_size = fx::kReliefMapSize;
        const fx::NormalMap rg8 = fx::DeriveNormalMap(rgba, static_cast<int>(width), static_cast<int>(height), options);
        fx::NormalMap map{rg8.width, rg8.height, rg8.mip_count, fx::NormalMapFormat::EacRg11,
                          texture::encode_eac_rg11_chain(rg8.rg.data(), rg8.width, rg8.height, rg8.mip_count)};
        counters->derive_us += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
        ++counters->derived;
        if (rg8.rg.empty() || !fx::WriteReliefMap(relief_dir, entry->key, map))
        {
            entry->state = NormalMapEntry::Failed;
            return;
        }
        if (!entry->wanted)
        {
            entry->state = NormalMapEntry::Stored;
            return;
        }
        entry->map = std::move(map);
        entry->state = NormalMapEntry::Derived;
    });
}

void NormalMaps::read_stored(const std::shared_ptr<NormalMapEntry> &entry)
{
    entry->state = NormalMapEntry::Pending;
    texture::Service::instance().post([entry, relief_dir = directory()] {
        std::string error;
        std::optional<fx::NormalMap> map = fx::ReadReliefMap(relief_dir, entry->key, &error);
        if (!map)
        {
            if (!error.empty())
                shim::logf("ERROR: effects: %s", error.c_str());
            entry->state = NormalMapEntry::Failed; // removed or damaged while the texture stayed loaded
            return;
        }
        entry->map = std::move(*map);
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
        read_stored(entry);
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
        shim::logf("ERROR: effects: no normal map for texture %s (relief %s): derivation, reading or upload failed",
                   entry->name.c_str(), fx::ReliefClassName(entry->relief));
    }
    return entry->handle;
}

NormalMaps::Stats NormalMaps::stats()
{
    Stats stats;
    std::erase_if(entries_, [](const std::weak_ptr<NormalMapEntry> &entry) { return entry.expired(); });
    for (const std::weak_ptr<NormalMapEntry> &weak : entries_)
        if (const std::shared_ptr<NormalMapEntry> entry = weak.lock())
        {
            if (entry->state == NormalMapEntry::Uploaded)
            {
                ++stats.resident;
                stats.bytes += entry->bytes;
            }
            stats.missing += entry->state == NormalMapEntry::Missing ? 1 : 0;
        }
    stats.derived = counters_->derived;
    stats.derive_ms = static_cast<double>(counters_->derive_us) / 1000.0;
    return stats;
}

void NormalMaps::end_frame()
{
    if (++frames_ < 300)
        return;
    frames_ = 0;
    const Stats now = stats();
    if (now.resident == logged_.resident && now.bytes == logged_.bytes && now.missing == logged_.missing &&
        now.derived == logged_.derived)
        return;
    logged_ = now;
    shim::logf("effects relief: %u normal maps resident (%.1f MB, max %d px); %u loaded textures without a prepared "
               "map (no relief: «Подготовка рельефа» not run or not finished); %u height maps derived (%.0f ms)",
               now.resident, static_cast<double>(now.bytes) / 1048576.0, max_size_, now.missing, now.derived, now.derive_ms);
}

} // namespace storm::effects
