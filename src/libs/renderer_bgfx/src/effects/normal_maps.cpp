#include "effects/normal_maps.hpp"

#include "effects/relief_generator.hpp"
#include "relief.hpp"
#include "shim/device.hpp"
#include "storm/fx/height_map.hpp"
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
    if (!shim::Context::instance().initialised())
        return;
    if (bgfx::isValid(handle))
        bgfx::destroy(handle);
    if (bgfx::isValid(material))
        bgfx::destroy(material);
}

void NormalMaps::configure(bool enabled, int max_size, bool material)
{
    const bool switched_on = enabled && !enabled_;
    const bool changed = enabled != enabled_ || max_size != max_size_ || material != material_;
    enabled_ = enabled;
    max_size_ = max_size;
    material_ = material;
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
    if (bgfx::isValid(entry.material))
        bgfx::destroy(entry.material);
    entry.handle = entry.material = BGFX_INVALID_HANDLE;
    entry.depth = 0.0f;
    entry.bytes = entry.material_bytes = 0;
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
    // A texture of a relief class with a height has its gloss in its relief's material texture; the others their own
    // gloss map.
    if (readable && probe_bytes && probe_data->size() >= probe_bytes && !fx::ReliefHasHeight(verdict.relief))
        add_gloss(store, name, {probe_data->data(), probe_bytes}, pw, ph, file);
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
    entry->material_gloss = fx::ReliefHasHeight(verdict.relief) ? fx::ClassifyGlossName(name).gloss : fx::GlossClass::None;
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
        std::optional<fx::NormalMap> map = entry->gloss != fx::GlossClass::None
                                               ? fx::ReadGlossMap(relief_dir, entry->key, &error)
                                               : fx::ReadReliefMap(relief_dir, entry->key, &error);
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
    return store.normal_map ? upload(store.normal_map, lighting, max_size_) : Relief{};
}

NormalMaps::Relief NormalMaps::gloss(shim::TexStore &store, fx::Lighting &lighting)
{
    return store.gloss_map ? upload(store.gloss_map, lighting, gloss_max_size_) : Relief{};
}

NormalMaps::Relief NormalMaps::upload(const std::shared_ptr<NormalMapEntry> &entry, fx::Lighting &lighting, int max_size)
{
    entry->wanted = true;
    entry->drawn = frame_;
    switch (entry->state.load())
    {
    case NormalMapEntry::Unqueued: // its map left the cache since it was looked up (read_stored)
        if (entry->gloss != fx::GlossClass::None)
            look_up_gloss(entry);
        else
            look_up(entry);
        return {};
    case NormalMapEntry::Stored:
        read_stored(entry);
        return {};
    case NormalMapEntry::Derived:
        fx::DropTopMips(entry->map, max_size);
        entry->handle = lighting.CreateNormalMap(entry->map); // logs why when it cannot
        entry->bytes = entry->map.rg.size();
        entry->width = entry->map.width;
        entry->height_texels = entry->map.height;
        if (material_ && !entry->map.material.empty())
        {
            const fx::NormalMap material{entry->map.width, entry->map.height, entry->map.mip_count, entry->map.format,
                                         std::move(entry->map.material)};
            entry->material = lighting.CreateNormalMap(material); // logs why when it cannot
            entry->depth = entry->map.depth;
            entry->material_bytes = bgfx::isValid(entry->material) ? material.rg.size() : 0;
            entry->bytes += entry->material_bytes;
        }
        else if (material_ && fx::ReliefHasHeight(entry->relief) && !entry->no_height_reported)
        {
            // Flat (the preparation logs "flat, no height"): this texture has no parallax, cavity or gloss.
            entry->no_height_reported = true;
            shim::logf("effects: relief %s (%s): its map has no material texture: no parallax, cavity or gloss for it",
                       entry->name.c_str(), fx::ReliefClassName(entry->relief));
        }
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
        if (entry->gloss != fx::GlossClass::None)
            shim::logf("ERROR: effects: no gloss map for texture %s (gloss %s): derivation, reading or upload failed",
                       entry->name.c_str(), fx::GlossClassName(entry->gloss));
        else
            shim::logf("ERROR: effects: no normal map for texture %s (relief %s): derivation, reading or upload failed",
                       entry->name.c_str(), fx::ReliefClassName(entry->relief));
    }
    if (entry->state != NormalMapEntry::Uploaded)
        return {};
    const float fade = std::chrono::duration<float>(now_ - entry->uploaded).count() /
                       std::chrono::duration<float>(kReliefFade).count();
    return {entry->handle, std::clamp(fade, 0.0f, 1.0f), entry->material, entry->depth, entry->width, entry->height_texels};
}

void NormalMaps::configure_gloss(bool enabled, int max_size)
{
    const bool switched_on = enabled && !gloss_enabled_;
    const bool changed = enabled != gloss_enabled_ || max_size != gloss_max_size_;
    gloss_enabled_ = enabled;
    gloss_max_size_ = max_size;
    if (!changed)
        return;
    std::erase_if(gloss_entries_, [](const std::weak_ptr<NormalMapEntry> &entry) { return entry.expired(); });
    for (const std::weak_ptr<NormalMapEntry> &weak : gloss_entries_)
        if (const std::shared_ptr<NormalMapEntry> entry = weak.lock())
        {
            release(*entry);
            if (switched_on && entry->state == NormalMapEntry::Unqueued)
                look_up_gloss(entry);
        }
}

void NormalMaps::add_gloss(shim::TexStore &store, const std::string &name, std::span<const uint8_t> probe, uint32_t pw,
                           uint32_t ph, const std::filesystem::path &file)
{
    const fx::GlossVerdict verdict = fx::ClassifyGlossName(name);
    if (std::find(gloss_logged_.begin(), gloss_logged_.end(), name) == gloss_logged_.end())
    {
        gloss_logged_.push_back(name);
        if (verdict.rule)
            shim::logd("effects: texture %s: gloss %s (rule %s)", name.c_str(), fx::GlossClassName(verdict.gloss), verdict.rule);
        else
            shim::logd("effects: texture %s: no gloss map (no rule knows it)", name.c_str());
    }
    if (verdict.gloss == fx::GlossClass::None || directory().empty())
        return; // no directory: reported once by the relief
    auto entry = std::make_shared<NormalMapEntry>();
    entry->name = name;
    entry->gloss = verdict.gloss;
    entry->file = file;
    entry->key = fx::ReliefKey(name, probe, static_cast<int>(pw), static_cast<int>(ph), fx::GlossMethod(verdict.gloss), fx::kGlossMapSize);
    store.gloss_map = entry;
    gloss_entries_.push_back(entry);
    if (gloss_enabled_)
        look_up_gloss(entry);
}

void NormalMaps::look_up_gloss(const std::shared_ptr<NormalMapEntry> &entry)
{
    entry->state = NormalMapEntry::Pending;
    texture::Service::instance().post([entry, relief_dir = directory()] {
        if (fx::HasGlossMap(relief_dir, entry->key))
        {
            entry->state = NormalMapEntry::Stored;
            return;
        }
        // Made by PBRify's roughness network with the texture's relief, in the generator's queue.
        shim::logf("effects: gloss %s (%s): no map %016llx yet: requested", entry->name.c_str(),
                   fx::GlossClassName(entry->gloss), static_cast<unsigned long long>(entry->key));
        entry->state = NormalMapEntry::Waiting;
        ReliefGenerator::instance().request(entry, entry->file);
    });
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
                if (bgfx::isValid(entry->material))
                {
                    ++stats.materials;
                    stats.material_bytes += entry->material_bytes;
                }
            }
            stats.unqueued += entry->state == NormalMapEntry::Unqueued ? 1 : 0;
            stats.waiting += entry->state == NormalMapEntry::Waiting ? 1 : 0;
            stats.missing += entry->state == NormalMapEntry::Missing ? 1 : 0;
        }
    std::erase_if(gloss_entries_, [](const std::weak_ptr<NormalMapEntry> &entry) { return entry.expired(); });
    for (const std::weak_ptr<NormalMapEntry> &weak : gloss_entries_)
        if (const std::shared_ptr<NormalMapEntry> entry = weak.lock())
        {
            if (entry->state == NormalMapEntry::Uploaded)
            {
                ++stats.gloss_resident;
                stats.gloss_bytes += entry->bytes;
            }
            stats.gloss_unqueued += entry->state == NormalMapEntry::Unqueued ? 1 : 0;
            stats.gloss_waiting += entry->state == NormalMapEntry::Waiting ? 1 : 0;
            stats.gloss_missing += entry->state == NormalMapEntry::Missing ? 1 : 0;
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
    const bool gloss_same = now.gloss_resident == logged_.gloss_resident && now.gloss_bytes == logged_.gloss_bytes &&
                            now.gloss_unqueued == logged_.gloss_unqueued && now.gloss_waiting == logged_.gloss_waiting &&
                            now.gloss_missing == logged_.gloss_missing;
    if (gloss_same && now.resident == logged_.resident && now.materials == logged_.materials && now.bytes == logged_.bytes && now.waiting == logged_.waiting &&
        now.missing == logged_.missing && now.unqueued == logged_.unqueued && now.derived == logged_.derived && rest == logged_rest_ &&
        generator.made == logged_made_)
        return;
    logged_ = now;
    logged_rest_ = rest;
    logged_made_ = generator.made;
    shim::logf("effects relief: %u normal maps resident (%.1f MB, max %d px; %u with a material texture (height, gloss), %.1f MB of "
               "it); %u loaded textures waiting for their map, "
               "%u more of the game to make in the background%s; %u maps made (%.0f ms, slowest %.0f ms); %u without "
               "a map (generation unavailable); %u height maps derived (%.0f ms); %u loaded while relief was off",
               now.resident, static_cast<double>(now.bytes) / 1048576.0, max_size_, now.materials,
               static_cast<double>(now.material_bytes) / 1048576.0, now.waiting, rest,
               generator.hot ? " (paused: the device is hot)" : "", generator.made, generator.made_ms,
               generator.slowest_ms, now.missing, now.derived, now.derive_ms, now.unqueued);
    if (!gloss_same)
        shim::logf("effects gloss: %u gloss maps resident (%.1f MB, max %d px); %u loaded textures waiting for their map; "
                   "%u without a map (generation unavailable); %u loaded while gloss was off",
                   now.gloss_resident, static_cast<double>(now.gloss_bytes) / 1048576.0, gloss_max_size_, now.gloss_waiting,
                   now.gloss_missing, now.gloss_unqueued);
}

} // namespace storm::effects
