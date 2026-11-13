#include "effects/upscale.hpp"

#include "effects/relief_generator.hpp"
#include "shim/device.hpp"
#include "storm/fx/relief_store.hpp"
#include "texture/core/formats.hpp"
#include "texture/core/service.hpp"

#include <algorithm>
#include <mutex>

#if defined(__linux__) || defined(__ANDROID__)
#include <unistd.h>
#endif

namespace storm::effects
{

namespace
{
using Clock = std::chrono::steady_clock;
constexpr auto kPlanPeriod = std::chrono::milliseconds(500);
constexpr auto kLogPeriod = std::chrono::seconds(5);
constexpr auto kForget = std::chrono::seconds(5);
constexpr int kUploadsPerFrame = 2; // a 2048² ×2 is ~3 MB: two per frame keep the frame time flat

std::mutex directory_mutex;
std::filesystem::path upscale_directory;

uint64_t DeviceRam()
{
#if defined(__linux__) || defined(__ANDROID__)
    const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page > 0)
        return static_cast<uint64_t>(pages) * static_cast<uint64_t>(page);
#endif
    return 0;
}

// GPU memory of a ×2 of width x height in the tier's block format with its whole chain.
uint64_t ChainBytes(uint32_t width, uint32_t height, uint32_t block_bytes)
{
    uint64_t bytes = 0;
    for (uint32_t w = width, h = height;; w = std::max(1u, w / 2), h = std::max(1u, h / 2))
    {
        bytes += uint64_t((w + 3) / 4) * ((h + 3) / 4) * block_bytes;
        if (w == 1 && h == 1)
            break;
    }
    return bytes;
}
} // namespace

void ConfigureUpscaleDirectory(const std::filesystem::path &directory)
{
    {
        std::lock_guard lock(directory_mutex);
        upscale_directory = directory;
    }
    upscales().reconfigure();
}

Upscales &upscales()
{
    static Upscales instance;
    return instance;
}

UpscaleEntry::~UpscaleEntry()
{
    if (bgfx::isValid(handle) && shim::Context::instance().initialised())
        bgfx::destroy(handle);
}

bool Upscales::resolve()
{
    if (configured_)
        return true;
    {
        // The effects are configured when the renderer starts, the relief (and this directory) a little later, the GPU's
        // caps once bgfx runs: until all are there nothing is decided.
        std::lock_guard lock(directory_mutex);
        if (upscale_directory.empty() || !shim::Context::instance().initialised() || !shim::Context::instance().caps())
            return false;
        directory_ = upscale_directory;
    }
    configured_ = true;
    {
        // The device's tier, as its DXT textures (shim objects.cpp resolve_tier): BC on GPUs with S3TC, else ETC2; a
        // GPU with neither would hold the ×2 uncompressed, eight times the memory: off, said so.
        const bgfx::Caps *caps = shim::Context::instance().caps();
        const uint16_t need = BGFX_CAPS_FORMAT_TEXTURE_2D;
        const bool bc = (caps->formats[bgfx::TextureFormat::BC1] & need) && (caps->formats[bgfx::TextureFormat::BC3] & need);
        const bool etc2 = (caps->formats[bgfx::TextureFormat::ETC2] & need) && (caps->formats[bgfx::TextureFormat::ETC2A] & need);
        bc_ = bc && !(texture::Service::instance().mode() == texture::Mode::Etc2 && etc2);
        supported_ = bc || etc2;
        max_size_ = caps->limits.maxTextureSize;
        const uint64_t ram = DeviceRam();
        budget_ = wanted_budget_mb_ ? static_cast<uint64_t>(wanted_budget_mb_) << 20 : fx::UpscaleBudgetBytes(ram);
        // The storage limit (kUpscaleCacheBytes): the least recently used go first. Off the main thread.
        texture::Service::instance().post([directory = directory_] {
            const uint64_t bytes = fx::TrimUpscaled(directory, fx::kUpscaleCacheBytes);
            shim::logf("effects upscale: the upscale directory holds %.1f MB (limit %.0f MB)",
                       static_cast<double>(bytes) / 1048576.0, static_cast<double>(fx::kUpscaleCacheBytes) / 1048576.0);
        });
        if (!supported_)
            shim::logf("ERROR: effects upscale: unavailable (the GPU has neither ETC2 nor BC textures): every texture draws "
                       "at its own size");
        else
            shim::logf("effects upscale: budget %.0f MB (%.0f MB RAM: RAM / 32, 64..384 MB, unless fx_upscale_budget), %s, GPU textures up to %u px, "
                       "×2 within %.0f m (kept until %.0f m), directory %s",
                       static_cast<double>(budget_) / 1048576.0, static_cast<double>(ram) / 1048576.0, bc_ ? "BC1/BC3" : "ETC2",
                       max_size_, static_cast<double>(fx::kUpscaleNear), static_cast<double>(fx::kUpscaleFar),
                       directory_.string().c_str());
    }
    return true;
}

void Upscales::configure(bool enabled, uint16_t budget_mb)
{
    wanted_ = enabled;
    wanted_budget_mb_ = budget_mb;
    if (configured_ && budget_mb && static_cast<uint64_t>(budget_mb) << 20 != budget_)
    {
        budget_ = static_cast<uint64_t>(budget_mb) << 20;
        shim::logf("effects upscale: budget %u MB (fx_upscale_budget)", budget_mb);
    }
    if (!resolve())
        return; // applied when the directory is known (the first texture)
    if (enabled && !supported_)
        enabled = false;
    if (enabled == enabled_)
        return;
    enabled_ = enabled;
    shim::logf("effects upscale: %s", enabled ? "on" : "off: every ×2 released");
    planned_ = false;
    frames_since_on_ = 0;
    next_plan_ = Clock::now() + kPlanPeriod; // the draws report their distances first
    ReliefGenerator::instance().set_display(enabled, directory_, bc_);
    if (enabled)
        return;
    std::erase_if(entries_, [](const std::weak_ptr<UpscaleEntry> &entry) { return entry.expired(); });
    for (const std::weak_ptr<UpscaleEntry> &weak : entries_)
        if (const std::shared_ptr<UpscaleEntry> entry = weak.lock())
            release(*entry);
}

void Upscales::release(UpscaleEntry &entry)
{
    if (entry.state == UpscaleEntry::Resident)
    {
        bgfx::destroy(entry.handle);
        entry.handle = BGFX_INVALID_HANDLE;
        entry.state = UpscaleEntry::Stored;
    }
    else if (entry.state == UpscaleEntry::Read)
    {
        std::vector<uint8_t>().swap(entry.ktx);
        entry.state = UpscaleEntry::Stored;
    }
}

void Upscales::on_texture_loaded(shim::TexStore &store, const std::string &name, const std::filesystem::path &file)
{
    if (store.kind != shim::TexKind::Tex2D || store.subs.empty() || file.empty())
        return;
    if (!configured_)
    {
        reconfigure(); // the tier and the directory, once they are known
        if (!configured_)
        {
            if (!directory_reported_)
                shim::logf("ERROR: effects upscale: a texture loaded before the upscale directory (render::ConfigureRelief) "
                           "or the renderer: no ×2 for it");
            directory_reported_ = true;
            return;
        }
    }
    if (!supported_)
        return;
    const fx::ReliefClass relief =
        fx::NameSuggestsHeightMap(name) ? fx::ReliefClass::HeightMap : fx::ClassifyTextureName(name).relief;
    const fx::UpscaleTarget target = fx::ClassifyUpscale(name, relief, store.cutout != nullptr);
    const uint32_t format = static_cast<uint32_t>(store.format);
    if (target == fx::UpscaleTarget::None || !texture::tx_format_known(format))
        return;
    // The key as the preparation makes it (fx::ReliefKey of the probe level, src/libs/relief).
    const int probe = fx::ReliefProbeLevel(static_cast<int>(store.width), static_cast<int>(store.height),
                                           static_cast<int>(store.levels));
    if (probe < 0)
        return;
    const uint32_t pw = store.level_width(static_cast<uint32_t>(probe)), ph = store.level_height(static_cast<uint32_t>(probe));
    const std::vector<uint8_t> &probe_data = store.sub(0, static_cast<uint32_t>(probe)).data;
    const size_t probe_bytes = texture::d3d_level_bytes(format, pw, ph);
    if (probe_data.size() < probe_bytes)
        return;
    if (!fx::UpscaleSize(static_cast<int>(store.width), static_cast<int>(store.height)))
        return; // larger than fx::kUpscaleMaxSource: its own density is the ×2's
    if (std::max(store.width, store.height) * 2 > max_size_)
    {
        if (!size_reported_)
            shim::logf("effects upscale: %s (%ux%u) and textures like it get no ×2: larger than the GPU's %u px", name.c_str(),
                       store.width, store.height, max_size_);
        size_reported_ = true;
        return;
    }
    auto entry = std::make_shared<UpscaleEntry>();
    entry->name = name;
    entry->file = file;
    entry->target = target;
    entry->width = store.width;
    entry->height = store.height;
    // Until the file is read: an opaque DXT1 becomes BC1 / ETC2 RGB8 (8-byte blocks), anything else 16-byte blocks.
    const bool opaque = format == static_cast<uint32_t>(texture::TxFormat::Dxt1) ||
                        format == static_cast<uint32_t>(texture::TxFormat::X8R8G8B8) ||
                        format == static_cast<uint32_t>(texture::TxFormat::R5G6B5);
    entry->bytes = ChainBytes(store.width * 2, store.height * 2, opaque ? 8 : 16);
    entry->key = fx::ReliefKey(name, {probe_data.data(), probe_bytes}, static_cast<int>(pw), static_cast<int>(ph),
                               fx::UpscaleMethod(bc_), static_cast<int>(std::max(store.width, store.height)));
    store.upscale = entry;
    entries_.push_back(entry);
}

bgfx::TextureHandle Upscales::replacement(shim::TexStore &store, float distance)
{
    UpscaleEntry *entry = store.upscale.get();
    if (!entry || !enabled_)
        return BGFX_INVALID_HANDLE;
    if (entry->nearest < 0.0f || distance < entry->nearest)
        entry->nearest = std::max(distance, 0.0f);
    if (entry->state != UpscaleEntry::Resident)
        return BGFX_INVALID_HANDLE;
    return entry->handle;
}

void Upscales::look_up(const std::shared_ptr<UpscaleEntry> &entry, bool read)
{
    entry->state = UpscaleEntry::Pending;
    texture::Service::instance().post([entry, read, directory = directory_] {
        if (!fx::HasUpscaled(directory, entry->key))
        {
            entry->state = UpscaleEntry::Missing;
            return;
        }
        if (!read)
        {
            entry->state = UpscaleEntry::Stored;
            return;
        }
        std::string error;
        std::optional<std::vector<uint8_t>> ktx = fx::ReadUpscaled(directory, entry->key, &error);
        if (!ktx)
        {
            if (!error.empty())
                shim::logf("ERROR: effects upscale: %s: %s (made again)", entry->name.c_str(), error.c_str());
            entry->state = UpscaleEntry::Missing;
            return;
        }
        entry->ktx = std::move(*ktx);
        entry->state = UpscaleEntry::Read;
    });
}

void Upscales::end_frame()
{
    if (!enabled_)
        return;
    const Clock::time_point now = Clock::now();
    // The uploads of the ×2 read since (the plan chose them; it may have dropped them meanwhile: release()).
    int uploads = 0;
    for (const std::weak_ptr<UpscaleEntry> &weak : entries_)
    {
        if (uploads >= kUploadsPerFrame)
            break;
        const std::shared_ptr<UpscaleEntry> entry = weak.lock();
        if (!entry || entry->state != UpscaleEntry::Read)
            continue;
        ++uploads;
        bgfx::TextureInfo info{};
        const bgfx::TextureHandle handle =
            bgfx::createTexture(bgfx::copy(entry->ktx.data(), static_cast<uint32_t>(entry->ktx.size())), BGFX_TEXTURE_NONE,
                                0, &info);
        std::vector<uint8_t>().swap(entry->ktx);
        if (!bgfx::isValid(handle) || info.width != entry->width * 2 || info.height != entry->height * 2)
        {
            // A ×2 of another size: the game file is not the texture loaded (a smaller top level, texture_degradation).
            shim::logf("ERROR: effects upscale: %s: its ×2 is %ux%u, the texture %ux%u: drawn at its own size",
                       entry->name.c_str(), info.width, info.height, entry->width, entry->height);
            if (bgfx::isValid(handle))
                bgfx::destroy(handle);
            entry->state = UpscaleEntry::Failed;
            continue;
        }
        entry->bytes = info.storageSize;
        entry->handle = handle;
        entry->state = UpscaleEntry::Resident;
    }
    // The first plan after switching on waits for two frames of draws too (a slow frame is longer than the period).
    ++frames_since_on_;
    ++frames_since_plan_;
    if (now >= next_plan_ && frames_since_on_ > 2 && frames_since_plan_ >= 2)
    {
        frames_since_plan_ = 0;
        next_plan_ = now + kPlanPeriod;
        plan();
    }
    if (now >= next_log_)
    {
        const Stats s = stats();
        if (s.resident != logged_.resident || s.bytes != logged_.bytes || s.waiting != logged_.waiting || s.reading != logged_.reading ||
            s.missing != logged_.missing || s.failed != logged_.failed || s.entries != logged_.entries)
        {
            logged_ = s;
            next_log_ = now + kLogPeriod;
            shim::logf("effects upscale: %u textures at ×2 (%.1f MB of the %.0f MB budget); %u being read, %u being made, %u not made yet, "
                       "%u unusable (logged); %u loaded textures with a ×2 target",
                       s.resident, static_cast<double>(s.bytes) / 1048576.0, static_cast<double>(s.budget) / 1048576.0,
                       s.reading, s.waiting, s.missing, s.failed, s.entries);
        }
    }
}

void Upscales::plan()
{
    const Clock::time_point now = Clock::now();
    std::erase_if(entries_, [](const std::weak_ptr<UpscaleEntry> &entry) { return entry.expired(); });
    std::vector<std::shared_ptr<UpscaleEntry>> alive;
    std::vector<fx::UpscaleCandidate> candidates;
    alive.reserve(entries_.size());
    candidates.reserve(entries_.size());
    for (const std::weak_ptr<UpscaleEntry> &weak : entries_)
        if (std::shared_ptr<UpscaleEntry> entry = weak.lock())
        {
            if (entry->state == UpscaleEntry::Failed)
                continue;
            // A texture not drawn this period (out of view a moment, a hitch) keeps its last distance for kForget.
            if (entry->nearest >= 0.0f)
            {
                entry->last_distance = entry->nearest;
                entry->last_drawn = now;
            }
            const float distance = entry->nearest >= 0.0f ? entry->nearest
                                   : now - entry->last_drawn < kForget ? entry->last_distance
                                                                       : -1.0f;
            candidates.push_back({distance, entry->bytes, entry->state == UpscaleEntry::Resident});
            alive.push_back(std::move(entry));
        }
    const fx::UpscalePlan plan = fx::PlanUpscaleResidency(candidates, budget_);
    outstanding_ = 0;
    for (size_t i = 0; i < alive.size(); ++i)
    {
        UpscaleEntry &entry = *alive[i];
        entry.nearest = -1.0f;
        const int state = entry.state;
        if (!plan.keep[i])
        {
            release(entry);
            continue;
        }
        outstanding_ += state == UpscaleEntry::Resident ? 0 : 1;
        if (state == UpscaleEntry::Unknown || state == UpscaleEntry::Stored)
            look_up(alive[i], true);
        else if (state == UpscaleEntry::Missing)
        {
            // Waiting before the request: the generator may finish it (Stored) before request_upscale returns.
            entry.state = UpscaleEntry::Waiting;
            if (ReliefGenerator::instance().request_upscale(alive[i]))
                shim::logf("effects upscale: %s (%s, %ux%u): no ×2 %016llx yet: requested", entry.name.c_str(),
                           fx::UpscaleTargetName(entry.target), entry.width, entry.height,
                           static_cast<unsigned long long>(entry.key));
            else
                entry.state = UpscaleEntry::Missing; // its queue is full: the next plan asks again
        }
    }
    limited_ = plan.limited;
    planned_ = true;
    if (plan.limited != logged_limited_)
    {
        if (plan.limited > 0)
            shim::logf("effects upscale: budget: %d textures within %.0f m stay at their own size (the nearest at %.1f m): "
                       "the %.0f MB budget holds %.1f MB of ×2",
                       plan.limited, static_cast<double>(fx::kUpscaleNear), static_cast<double>(plan.nearest_limited),
                       static_cast<double>(budget_) / 1048576.0, static_cast<double>(plan.bytes) / 1048576.0);
        else if (logged_limited_ > 0)
            shim::logf("effects upscale: budget: every texture within %.0f m fits again", static_cast<double>(fx::kUpscaleNear));
        logged_limited_ = plan.limited;
    }
}

bool Upscales::settled() const
{
    if (!enabled_)
        return true;
    return planned_ && outstanding_ == 0;
}

Upscales::Stats Upscales::stats() const
{
    Stats s;
    s.budget = budget_;
    s.limited = limited_;
    for (const std::weak_ptr<UpscaleEntry> &weak : entries_)
        if (const std::shared_ptr<UpscaleEntry> entry = weak.lock())
        {
            ++s.entries;
            switch (entry->state.load())
            {
            case UpscaleEntry::Resident:
                ++s.resident;
                s.bytes += entry->bytes;
                break;
            case UpscaleEntry::Waiting: ++s.waiting; break;
            case UpscaleEntry::Pending:
            case UpscaleEntry::Read: ++s.reading; break;
            case UpscaleEntry::Missing: ++s.missing; break;
            case UpscaleEntry::Failed: ++s.failed; break;
            default: break;
            }
        }
    return s;
}

} // namespace storm::effects
