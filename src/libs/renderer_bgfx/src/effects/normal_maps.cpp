#include "effects/normal_maps.hpp"

#include "effects/relief_generator.hpp"
#include "relief.hpp"
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
// The relief directory (ConfigureRelief), shared with the worker tasks.
std::mutex directory_mutex;
std::filesystem::path relief_directory;

std::filesystem::path directory()
{
    std::lock_guard lock(directory_mutex);
    return relief_directory;
}
} // namespace

void ConfigureRelief(ReliefSetup setup)
{
    {
        std::lock_guard lock(directory_mutex);
        relief_directory = setup.directory;
    }
    ReliefGenerator::instance().configure(std::move(setup));
}

NormalMapEntry::~NormalMapEntry()
{
    if (bgfx::isValid(handle) && shim::Context::instance().initialised())
        bgfx::destroy(handle);
}

void NormalMaps::configure(bool enabled, int max_size)
{
    const bool switched_on = enabled && !enabled_;
    const bool changed = enabled != enabled_ || max_size != max_size_;
    enabled_ = enabled;
    max_size_ = max_size;
    ReliefGenerator::instance().set_active(enabled);
    if (!changed)
        return;
    std::erase_if(entries_, [](const std::weak_ptr<NormalMapEntry> &entry) { return entry.expired(); });
    for (const std::weak_ptr<NormalMapEntry> &weak : entries_)
        if (const std::shared_ptr<NormalMapEntry> entry = weak.lock())
        {
            release(*entry);
            // The textures loaded while relief was off: looked up now, as if loaded now.
            if (switched_on && entry->state == NormalMapEntry::Unqueued)
                look_up(entry);
        }
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

void NormalMaps::on_texture_loaded(shim::TexStore &store, const char *path, const std::filesystem::path &file)
{
    if (!path || store.kind != shim::TexKind::Tex2D || store.subs.empty())
        return;
    // Every texture of a relief class gets its entry (its key needs the probe level, which only the load has), relief
    // on or off; it is looked up now or when relief is switched on (configure).
    const std::string name = fx::NormalizeTexturePath(path);
    const uint32_t format = static_cast<uint32_t>(store.format);
    const bool readable = texture::tx_format_known(format);
    // The probe level: the key's part of the texture, and where "bump" names are checked for grey (fx/relief_store.hpp).
    const int probe = fx::ReliefProbeLevel(static_cast<int>(store.width), static_cast<int>(store.height),
                                           static_cast<int>(store.levels));
    const uint32_t pw = probe >= 0 ? store.level_width(static_cast<uint32_t>(probe)) : 0,
                   ph = probe >= 0 ? store.level_height(static_cast<uint32_t>(probe)) : 0;
    const std::vector<uint8_t> *probe_data = probe >= 0 ? &store.sub(0, static_cast<uint32_t>(probe)).data : nullptr;
    const size_t probe_bytes = readable && probe >= 0 ? texture::d3d_level_bytes(format, pw, ph) : 0;
    std::vector<uint8_t> small;
    if (fx::NameSuggestsHeightMap(name) && probe_bytes && probe_data->size() >= probe_bytes)
    {
        small.resize(size_t(pw) * ph * 4);
        texture::decode_rgba8(format, probe_data->data(), pw, ph, small.data());
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
        return; // smaller than a 4x4 block below 64 px: no relief (the preparation skips it too)
    if (directory().empty())
    {
        if (!directory_reported_)
            shim::logf("ERROR: effects: no relief directory (render::ConfigureRelief): no relief at all");
        directory_reported_ = true;
        return;
    }

    auto entry = std::make_shared<NormalMapEntry>();
    entry->name = name;
    entry->relief = verdict.relief;
    entry->file = file;
    entry->key = fx::ReliefKey(name, {probe_data->data(), probe_bytes}, static_cast<int>(pw), static_cast<int>(ph),
                               fx::ReliefMethod(verdict.relief), fx::kReliefMapSize);
    store.normal_map = entry;
    entries_.push_back(entry);
    if (enabled_)
        look_up(entry);
}

void NormalMaps::look_up(const std::shared_ptr<NormalMapEntry> &entry)
{
    entry->state = NormalMapEntry::Pending;
    texture::Service::instance().post([entry, relief_dir = directory(), counters = counters_] {
        // Which map the texture takes, for the diagnostics: its key and the method it was (or will be) made with.
        const unsigned long long key = entry->key, method = fx::ReliefMethod(entry->relief);
        if (fx::HasReliefMap(relief_dir, entry->key))
        {
            shim::logf("effects: relief %s (%s): map %016llx of method %016llx", entry->name.c_str(),
                       fx::ReliefClassName(entry->relief), key, method);
            entry->state = NormalMapEntry::Stored;
            return;
        }
        if (entry->relief != fx::ReliefClass::HeightMap)
        {
            shim::logf("effects: relief %s (%s): no map %016llx of method %016llx yet: requested", entry->name.c_str(),
                       fx::ReliefClassName(entry->relief), key, method);
            entry->state = NormalMapEntry::Waiting; // counted in the "effects relief:" line
            ReliefGenerator::instance().request(entry, entry->file);
            return;
        }
        // A height map: derived from the top level of its game file (the game may have skipped top mips of the loaded
        // texture, texture_degradation: the map is the same on every device).
        const auto start = std::chrono::steady_clock::now();
        std::vector<uint8_t> rgba;
        int width = 0, height = 0;
        if (!relief::ReadTextureLevel(entry->file, 1 << 16, rgba, width, height))
        {
            shim::logf("ERROR: effects: relief: height map %s: its file %s cannot be read", entry->name.c_str(),
                       entry->file.string().c_str());
            entry->state = NormalMapEntry::Failed;
            return;
        }
        shim::logf("effects: relief %s (height map): no map %016llx of method %016llx yet: derived", entry->name.c_str(),
                   key, method);
        fx::NormalMapOptions options = fx::HeightMapOptions();
        options.max_size = fx::kReliefMapSize;
        const fx::NormalMap rg8 = fx::DeriveNormalMap(rgba, width, height, options);
        fx::NormalMap map{rg8.width, rg8.height, rg8.mip_count, fx::NormalMapFormat::EacRg11,
                          texture::encode_eac_chain(texture::EacFormat::Rg11, rg8.rg.data(), rg8.width, rg8.height, rg8.mip_count)};
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
            // Gone from the cache while the texture stayed loaded (the system evicted it, «Очистить кэш») or damaged
            // (deleted by the read): made again like a map that never was (the next draw looks it up).
            if (!error.empty())
                shim::logf("ERROR: effects: %s: made again", error.c_str());
            else
                shim::logf("effects: relief %s: its map %016llx left the cache: made again", entry->name.c_str(),
                           static_cast<unsigned long long>(entry->key));
            entry->state = NormalMapEntry::Unqueued;
            return;
        }
        entry->map = std::move(*map);
        entry->state = NormalMapEntry::Derived;
    });
}

NormalMaps::Relief NormalMaps::handle(shim::TexStore &store, fx::Lighting &lighting)
{
    const std::shared_ptr<NormalMapEntry> &entry = store.normal_map;
    if (!entry)
        return {};
    entry->wanted = true;
    entry->drawn = frame_;
    switch (entry->state.load())
    {
    case NormalMapEntry::Unqueued: // its map left the cache since it was looked up (read_stored)
        look_up(entry);
        return {};
    case NormalMapEntry::Stored:
        read_stored(entry);
        return {};
    case NormalMapEntry::Derived:
        fx::DropTopMips(entry->map, max_size_);
        entry->handle = lighting.CreateNormalMap(entry->map); // logs why when it cannot
        entry->bytes = entry->map.rg.size();
        entry->map = {};
        entry->state = bgfx::isValid(entry->handle) ? NormalMapEntry::Uploaded : NormalMapEntry::Failed;
        entry->uploaded = now_;
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
    if (entry->state != NormalMapEntry::Uploaded)
        return {};
    const float fade = std::chrono::duration<float>(now_ - entry->uploaded).count() /
                       std::chrono::duration<float>(kReliefFade).count();
    return {entry->handle, std::clamp(fade, 0.0f, 1.0f)};
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
            stats.unqueued += entry->state == NormalMapEntry::Unqueued ? 1 : 0;
            stats.waiting += entry->state == NormalMapEntry::Waiting ? 1 : 0;
            stats.missing += entry->state == NormalMapEntry::Missing ? 1 : 0;
        }
    stats.derived = counters_->derived;
    stats.derive_ms = static_cast<double>(counters_->derive_us) / 1000.0;
    return stats;
}

void NormalMaps::end_frame()
{
    ++frame_;
    now_ = std::chrono::steady_clock::now();
    if (++frames_ < 300)
        return;
    frames_ = 0;
    const Stats now = stats();
    const ReliefGenerator::Stats generator = ReliefGenerator::instance().stats();
    const uint32_t rest = generator.rest > 0 ? static_cast<uint32_t>(generator.rest) : 0;
    if (now.resident == logged_.resident && now.bytes == logged_.bytes && now.waiting == logged_.waiting &&
        now.missing == logged_.missing && now.unqueued == logged_.unqueued && now.derived == logged_.derived && rest == logged_rest_ &&
        generator.made == logged_made_)
        return;
    logged_ = now;
    logged_rest_ = rest;
    logged_made_ = generator.made;
    shim::logf("effects relief: %u normal maps resident (%.1f MB, max %d px); %u loaded textures waiting for their map, "
               "%u more of the game to make in the background%s; %u maps made (%.0f ms, slowest %.0f ms); %u without "
               "a map (generation unavailable); %u height maps derived (%.0f ms); %u loaded while relief was off",
               now.resident, static_cast<double>(now.bytes) / 1048576.0, max_size_, now.waiting, rest,
               generator.hot ? " (paused: the device is hot)" : "", generator.made, generator.made_ms,
               generator.slowest_ms, now.missing, now.derived, now.derive_ms, now.unqueued);
}

} // namespace storm::effects
