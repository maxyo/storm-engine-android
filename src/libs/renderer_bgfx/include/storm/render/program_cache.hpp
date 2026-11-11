// Linked GPU programs kept between runs: bgfx's OpenGL backend hands every program binary it links to
// bgfx::CallbackI::cacheWrite and asks cacheReadSize/cacheRead before the next link of the same shaders. The
// renderer's callback stores them as files (src/shim/program_cache.hpp), so a second start, and a preset switch
// to programs an earlier run already linked, load binaries instead of linking (tens of milliseconds per program
// on a phone, seconds on a first run under ANGLE).
#pragma once

#include <cstdint>
#include <filesystem>

namespace storm::render
{

struct ProgramCacheConfig
{
    // Per-user cache storage (Android: Context.getCacheDir(); desktop: next to the texture cache). The binaries
    // of one GPU + driver + bgfx version live in a subdirectory named by them; the others are removed.
    std::filesystem::path directory;
    uint64_t max_bytes = 64ull << 20; // the oldest entries go above this total (ANGLE: ~0.4 MB per program)
};

// Before the device is created. Without it (host checks) every program is linked, and that is logged.
void ConfigureProgramCache(const ProgramCacheConfig &config);

struct ProgramCacheStats
{
    uint32_t loaded = 0;   // binaries handed to the driver (bgfx then counts them cached unless the driver rejects one)
    uint32_t stored = 0;   // programs linked and written
    uint32_t replaced = 0; // ... of which over an entry the driver rejected or that failed to load
    uint32_t invalid = 0;  // entries ignored and removed: corrupt, truncated or of another device
    uint32_t entries = 0;  // files in the cache now
    uint64_t bytes = 0;
};
ProgramCacheStats CurrentProgramCacheStats();

} // namespace storm::render
