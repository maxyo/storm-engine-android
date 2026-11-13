#include "shim/program_cache.hpp"

#include "shim/shim.hpp"
#include "texture/core/cache.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <system_error>
#include <vector>
#ifdef STORM_TRACY
#include <tracy/TracyC.h>
#endif

// patches/bgfx-vulkan-pipeline-cache.patch (src/renderer_vk.cpp) defines these two.
namespace bgfx
{
typedef void (*VkPipelineCreatedFn)(const char *_vs, const char *_fs, uint64_t _state, double _ms, bool _async);
void setVkPipelineCreated(VkPipelineCreatedFn _created);
} // namespace bgfx

namespace storm::shim
{

namespace
{
namespace fs = std::filesystem;
// A pipeline creation this long holds the render thread over a 60 Hz frame: logged with its program and state.
constexpr double kSlowPipelineMs = 16.0;

constexpr char kMagic[8] = {'S', 'T', 'P', 'R', 'O', 'G', 'B', 'N'};
constexpr uint32_t kVersion = 1;

struct Header
{
    char magic[8];
    uint32_t version;
    uint32_t size;     // of the binary that follows
    uint64_t key;      // device key
    uint64_t id;       // bgfx's program id
    uint64_t checksum; // texture::hash64 of the binary
};
static_assert(sizeof(Header) == 40);

uint64_t checksum(const void *data, size_t size)
{
    return texture::hash64(data, size, 0x5354505247u);
}
} // namespace

ProgramCache &ProgramCache::instance()
{
    static ProgramCache cache;
    return cache;
}

void ProgramCache::configure(const render::ProgramCacheConfig &config)
{
    std::lock_guard lock(mutex_);
    config_ = config;
    configured_ = !config.directory.empty();
}

void ProgramCache::attach_device(const std::string &device, bool vulkan)
{
    std::lock_guard lock(mutex_);
    entries_.clear();
    total_bytes_ = 0;
    served_.clear();
    served_checksums_.clear();
    vulkan_ = vulkan;
    pipelines_ = slow_pipelines_ = async_pipelines_ = 0;
    pipelines_ms_ = async_pipelines_ms_ = 0.0;
    bgfx::setVkPipelineCreated(vulkan ? &ProgramCache::pipeline_created : nullptr);
    device_dir_.clear();
    if (!configured_)
    {
        if (!unconfigured_logged_)
            logf("program cache: no directory configured (render::ConfigureProgramCache): every program is linked "
                 "at every start");
        unconfigured_logged_ = true;
        return;
    }
    key_ = checksum(device.data(), device.size());
    char name[17];
    snprintf(name, sizeof(name), "%016" PRIx64, key_);
    std::error_code error;
    fs::create_directories(config_.directory / name, error);
    if (error)
    {
        logf("ERROR: program cache: cannot create %s (%s): every program is linked at every start",
             (config_.directory / name).string().c_str(), error.message().c_str());
        return;
    }
    device_dir_ = config_.directory / name;
    // Binaries of another GPU, driver or bgfx version can never load again.
    for (const auto &item : fs::directory_iterator(config_.directory, error))
        if (item.is_directory() && item.path().filename() != name)
        {
            fs::remove_all(item.path(), error);
            logf("program cache: removed the binaries of another device or driver (%s)",
                 item.path().filename().string().c_str());
        }
    for (const auto &item : fs::directory_iterator(device_dir_, error))
    {
        const std::string file = item.path().filename().string();
        uint64_t id = 0;
        char rest = 0;
        if (!item.is_regular_file() || std::sscanf(file.c_str(), "%" SCNx64 ".bi%c", &id, &rest) != 2 || rest != 'n')
        {
            fs::remove_all(item.path(), error); // a temporary of an interrupted write, or a stranger
            continue;
        }
        Entry entry;
        entry.bytes = item.file_size(error);
        entry.used = item.last_write_time(error);
        entries_[id] = entry;
        total_bytes_ += entry.bytes;
    }
    stats_.entries = static_cast<uint32_t>(entries_.size());
    stats_.bytes = total_bytes_;
    logf("program cache: %s, %zu binaries (%.1f MB of %.0f MB) for %s", device_dir_.string().c_str(), entries_.size(),
         total_bytes_ / 1048576.0, config_.max_bytes / 1048576.0, device.c_str());
    evict();
}

void ProgramCache::pipeline_created(const char *vs, const char *fs, uint64_t state, double ms, bool async)
{
    ProgramCache &cache = instance();
    std::lock_guard lock(cache.mutex_);
    ++cache.pipelines_;
    if (async)
    {
        // Created on bgfx's pipeline thread while its draw was left out: no frame waited for it.
        ++cache.async_pipelines_;
        cache.async_pipelines_ms_ += ms;
        if (ms >= kSlowPipelineMs)
            logf("pipeline: %.1f ms off the render thread for %s + %s, state 0x%016" PRIx64 " (its draw appeared that much "
                 "later)",
                 ms, vs, fs, state);
        return;
    }
    cache.pipelines_ms_ += ms;
    if (ms < kSlowPipelineMs)
        return;
    ++cache.slow_pipelines_;
    logf("pipeline: %.1f ms on the render thread for %s + %s, state 0x%016" PRIx64 " (pipeline %u of this start, %u of "
         "them slow on the render thread, %.0f ms there in all)",
         ms, vs, fs, state, cache.pipelines_, cache.slow_pipelines_, cache.pipelines_ms_);
}

void ProgramCache::detach_device()
{
    std::lock_guard lock(mutex_);
    if (writer_.joinable())
        writer_.join();
    if (vulkan_ && pipelines_)
        logf("pipelines: %u created by this device: %u on the render thread (%u of them over %.0f ms, %.0f ms in all), %u "
             "off it while their draws waited (%.0f ms in all)",
             pipelines_, pipelines_ - async_pipelines_, slow_pipelines_, kSlowPipelineMs, pipelines_ms_, async_pipelines_,
             async_pipelines_ms_);
    bgfx::setVkPipelineCreated(nullptr);
    device_dir_.clear();
    entries_.clear();
    pending_.clear();
    served_.clear();
    served_checksums_.clear();
}

render::ProgramCacheStats ProgramCache::stats()
{
    std::lock_guard lock(mutex_);
    return stats_;
}

void ProgramCache::log_activity()
{
    std::lock_guard lock(mutex_);
    if (stats_.loaded == logged_.loaded && stats_.stored == logged_.stored && stats_.invalid == logged_.invalid)
        return;
    // Statistics only: every unusable binary already logged its own ERROR line (forget). No problem words here, so
    // log checks such as tools/scene_sweep (keywords error/fail/invalid/...) do not take the summary for a failure.
    logf("program cache: %u loaded, %u linked and stored (%u over binaries the driver refused), %u removed as "
         "unusable this frame; %u binaries, %.1f MB",
         stats_.loaded - logged_.loaded, stats_.stored - logged_.stored, stats_.replaced - logged_.replaced,
         stats_.invalid - logged_.invalid, stats_.entries, stats_.bytes / 1048576.0);
    logged_ = stats_;
}

fs::path ProgramCache::file_of(uint64_t id) const
{
    char name[24];
    snprintf(name, sizeof(name), "%016" PRIx64 ".bin", id);
    return device_dir_ / name;
}

void ProgramCache::forget(uint64_t id, const char *why)
{
    logf("ERROR: program cache: binary %016" PRIx64 " %s: removed, the program is linked", id, why);
    std::error_code error;
    fs::remove(file_of(id), error);
    if (const auto it = entries_.find(id); it != entries_.end())
    {
        total_bytes_ -= it->second.bytes;
        entries_.erase(it);
    }
    ++stats_.invalid;
    stats_.entries = static_cast<uint32_t>(entries_.size());
    stats_.bytes = total_bytes_;
}

void ProgramCache::evict()
{
    while (total_bytes_ > config_.max_bytes && !entries_.empty())
    {
        const auto oldest = std::min_element(entries_.begin(), entries_.end(),
                                             [](const auto &a, const auto &b) { return a.second.used < b.second.used; });
        std::error_code error;
        fs::remove(file_of(oldest->first), error);
        total_bytes_ -= oldest->second.bytes;
        entries_.erase(oldest);
    }
    stats_.entries = static_cast<uint32_t>(entries_.size());
    stats_.bytes = total_bytes_;
}

uint32_t ProgramCache::cacheReadSize(uint64_t id)
{
    std::lock_guard lock(mutex_);
    pending_.clear();
    pending_id_ = 0;
    if (device_dir_.empty() || !entries_.contains(id))
        return 0;
    std::error_code missing;
    if (!fs::exists(file_of(id), missing))
    {
        // The app cache is emptied by the system (low storage, «Очистить кэш») while the game runs: not a defect.
        logf("program cache: binary %016" PRIx64 " is gone (the system cleared the cache): the program is linked "
             "again",
             id);
        total_bytes_ -= entries_[id].bytes;
        entries_.erase(id);
        stats_.entries = static_cast<uint32_t>(entries_.size());
        stats_.bytes = total_bytes_;
        return 0;
    }
    std::ifstream in(file_of(id), std::ios::binary);
    Header header{};
    if (!in.read(reinterpret_cast<char *>(&header), sizeof(header)))
    {
        forget(id, "is truncated (no header)");
        return 0;
    }
    if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 || header.version != kVersion)
    {
        forget(id, "has another format");
        return 0;
    }
    if (header.key != key_ || header.id != id)
    {
        forget(id, "belongs to another device or program");
        return 0;
    }
    pending_.resize(header.size);
    if (header.size == 0 || !in.read(reinterpret_cast<char *>(pending_.data()), header.size) ||
        checksum(pending_.data(), pending_.size()) != header.checksum)
    {
        pending_.clear();
        forget(id, "is truncated or corrupt (checksum)");
        return 0;
    }
    pending_id_ = id;
    return header.size;
}

bool ProgramCache::cacheRead(uint64_t id, void *data, uint32_t size)
{
    std::lock_guard lock(mutex_);
    if (id != pending_id_ || size != pending_.size())
    {
        logf("ERROR: program cache: bgfx read %016" PRIx64 " (%u bytes) without its size query: the program is linked",
             id, size);
        return false;
    }
    std::memcpy(data, pending_.data(), size);
    if (vulkan_)
        served_checksums_[id] = checksum(pending_.data(), pending_.size());
    pending_.clear();
    pending_id_ = 0;
    // Most recently used: kept longest by evict().
    std::error_code error;
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(file_of(id), now, error);
    entries_[id].used = now;
    served_.insert(id);
    ++stats_.loaded;
    return true;
}

void ProgramCache::cacheWrite(uint64_t id, const void *data, uint32_t size)
{
    std::lock_guard lock(mutex_);
    if (device_dir_.empty() || !data || size == 0)
        return;
    if (size > config_.max_bytes / 4)
    {
        logf("ERROR: program cache: binary %016" PRIx64 " of %u bytes exceeds a quarter of the cache: not stored", id,
             size);
        return;
    }
    if (vulkan_)
    {
        // The device's pipeline cache, saved again: unchanged when nothing new was compiled since it was loaded.
        // (The file may be gone meanwhile, the app cache cleared: then it is written again.)
        const auto served = served_checksums_.find(id);
        std::error_code missing;
        if (served != served_checksums_.end() && served->second == checksum(data, size) && fs::exists(file_of(id), missing))
            return;
        if (served != served_checksums_.end())
        {
            // The driver's cache differs from what was loaded (it added pipelines, or its header check refused the data).
            ++stats_.replaced;
            served_checksums_.erase(served);
        }
    }
    else if (served_.erase(id))
    {
        // bgfx linked a program this run loaded: the driver rejected the binary (logged by bgfx).
        logf("program cache: binary %016" PRIx64 " was rejected by the driver: replaced by the new link", id);
        ++stats_.replaced;
    }
    std::error_code made;
    if (fs::create_directories(device_dir_, made))
        logf("program cache: %s was gone (the system cleared the cache): made again", device_dir_.string().c_str());
    Header header{};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = kVersion;
    header.size = size;
    header.key = key_;
    header.id = id;
    header.checksum = checksum(data, size);
    const fs::path file = file_of(id);
    const auto write = [file, id, header](const void *bytes) {
        fs::path temporary = file;
        temporary += ".tmp";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char *>(&header), sizeof(header));
            out.write(static_cast<const char *>(bytes), header.size);
            if (!out)
            {
                logf("ERROR: program cache: cannot write %s: binary %016" PRIx64 " not stored", temporary.string().c_str(), id);
                std::error_code error;
                fs::remove(temporary, error);
                return false;
            }
        }
        std::error_code error;
        fs::rename(temporary, file, error);
        if (error)
        {
            logf("ERROR: program cache: cannot store %s (%s)", file.string().c_str(), error.message().c_str());
            fs::remove(temporary, error);
            return false;
        }
        return true;
    };
    if (vulkan_)
    {
        // Megabytes, from the render thread between two frames: the file is written off it. One write at a time;
        // the one before has long finished (bgfx saves seconds apart).
        if (writer_.joinable())
            writer_.join();
        writer_ = std::thread([write, copy = std::vector<uint8_t>(static_cast<const uint8_t *>(data),
                                                                  static_cast<const uint8_t *>(data) + size)] {
            write(copy.data());
        });
    }
    else if (!write(data))
        return;
    if (const auto it = entries_.find(id); it != entries_.end())
        total_bytes_ -= it->second.bytes;
    Entry &entry = entries_[id];
    entry.bytes = sizeof(header) + size;
    entry.used = fs::file_time_type::clock::now();
    total_bytes_ += entry.bytes;
    ++stats_.stored;
    evict();
}

void ProgramCache::fatal(const char *file, uint16_t line, bgfx::Fatal::Enum code, const char *str)
{
    logf("bgfx %s (%s:%u): %s", code == bgfx::Fatal::DebugCheck ? "debug check" : "FATAL", file, line, str);
    if (code != bgfx::Fatal::DebugCheck)
        std::abort(); // bgfx's contract: the callback must not return from a fatal error
}

#ifdef STORM_TRACY
namespace
{
// Zones bgfx opened on this thread: its API scopes on the game thread, its backend scopes on the render thread.
thread_local std::vector<TracyCZoneCtx> bgfx_zones;
} // namespace

void ProgramCache::profilerBegin(const char *name, uint32_t abgr, const char *file, uint16_t line)
{
    // bgfx's 0xAABBGGRR as Tracy's 0xRRGGBB. The source location is copied: bgfx's names may be built per call.
    const uint32_t rgb = ((abgr & 0xffu) << 16) | (abgr & 0xff00u) | ((abgr >> 16) & 0xffu);
    const uint64_t location =
        ___tracy_alloc_srcloc_name(line, file, std::strlen(file), "bgfx", 4, name, std::strlen(name), rgb);
    bgfx_zones.push_back(___tracy_emit_zone_begin_alloc(location, 1));
}

void ProgramCache::profilerBeginLiteral(const char *name, uint32_t abgr, const char *file, uint16_t line)
{
    profilerBegin(name, abgr, file, line);
}

void ProgramCache::profilerEnd()
{
    if (bgfx_zones.empty())
        return;
    ___tracy_emit_zone_end(bgfx_zones.back());
    bgfx_zones.pop_back();
}
#else
void ProgramCache::profilerBegin(const char *, uint32_t, const char *, uint16_t)
{
}

void ProgramCache::profilerBeginLiteral(const char *, uint32_t, const char *, uint16_t)
{
}

void ProgramCache::profilerEnd()
{
}
#endif

void ProgramCache::traceVargs(const char *, uint16_t, const char *format, va_list args)
{
    char text[1024];
    vsnprintf(text, sizeof(text), format, args);
    size_t length = strlen(text);
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r'))
        text[--length] = '\0';
    logf("%s", text);
}

void ProgramCache::screenShot(const char *, uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, const void *,
                              uint32_t, bool)
{
    logf("ERROR: bgfx delivered a screenshot, which the renderer never requests: dropped");
}

void ProgramCache::captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool)
{
    logf("ERROR: bgfx started a frame capture, which the renderer never enables (BGFX_RESET_CAPTURE): ignored");
}

} // namespace storm::shim

namespace storm::render
{
void ConfigureProgramCache(const ProgramCacheConfig &config)
{
    shim::ProgramCache::instance().configure(config);
}

ProgramCacheStats CurrentProgramCacheStats()
{
    return shim::ProgramCache::instance().stats();
}
} // namespace storm::render
