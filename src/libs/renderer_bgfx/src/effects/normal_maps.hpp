// Normal maps for the per-pixel lighting's relief (FX_LIGHT_NORMALMAP, fx/normal_map.hpp, FX.md "Normal maps").
//
// Renderer::TextureLoad reports each texture's file (SetEffectsTextureSource) right after its levels are filled.
// While kLightingNormalMap is on, fx::ReliefClassifier decides from the path (and, for "bump" names, the pixels)
// whether the texture gets relief and of which class; such a texture's map is derived from its top level on a texture
// worker (fx::DeriveNormalMap: grey height maps as heights, colour textures from their band-passed luminance) and
// kept on disk (texture::write_derived_file into the directory of ConfigureNormalMapCache, keyed by the source pixels
// and fx::NormalMapCacheKey, which includes the preset's resolution it is derived at), encoded as EAC RG11
// (texture::encode_eac_rg11, 1 byte per texel). A map is uploaded (a smaller preset chosen later drops its top mips)
// when a draw first needs it; until then, and whenever the option is off, only the cache file exists. Textures loaded while the option
// is off get no relief until they are loaded again (the next location): their pixels are not kept.
#pragma once

#include "storm/fx/normal_map.hpp"
#include "texture/core/cache.hpp"

#include <bgfx/bgfx.h>

#include "storm_render_types.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace storm::fx
{
class Lighting;
}
namespace storm::shim
{
struct TexStore;
}

namespace storm::effects
{

// Directory and size budget of the derived maps (render::ConfigureEffectsCache): the app's cache directory on
// Android. Least recently used maps are evicted beyond the budget. Empty: no disk cache (logged on first use).
void ConfigureNormalMapCache(const std::filesystem::path &directory, uint64_t max_bytes);

// One texture's normal map, owned by its TexStore (TexStore::normal_map).
struct NormalMapEntry
{
    enum State : int
    {
        Pending,  // on a texture worker (derived, or read from the cache)
        Stored,   // only in the cache directory
        Derived,  // `map` is ready for the upload
        Uploaded, // `handle` is valid
        Failed,
    };
    std::atomic<int> state{Pending};
    std::atomic<bool> wanted{false}; // a draw asked for it: the worker keeps the map in memory for the upload
    std::string name;                // normalized texture path, for the logs
    fx::ReliefClass relief = fx::ReliefClass::None;
    fx::NormalMapOptions options{};
    uint64_t key = 0;               // cache key (source pixels, options, size)
    texture::DerivedShape shape{};  // cache file shape: the derived top mip and its levels
    fx::NormalMap map;              // written by the worker before `state` becomes Derived
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    size_t bytes = 0; // of the uploaded texture
    bool failure_reported = false;
    ~NormalMapEntry();
};

class NormalMaps
{
  public:
    // `max_size`: larger side of the uploaded top mip (Settings::normal_map_size). Turning the option off or changing
    // the size releases the uploaded maps; they come back from the cache when drawn again.
    void configure(bool enabled, int max_size);
    // Main thread, right after the texture's levels were filled.
    void on_texture_loaded(shim::TexStore &store, const char *path);
    // The uploaded map of the texture, or invalid: no relief for it, still being derived or read (relief appears a few
    // frames later), or failed (logged).
    bgfx::TextureHandle handle(shim::TexStore &store, fx::Lighting &lighting);

    struct Stats
    {
        uint32_t resident = 0;   // uploaded maps
        size_t bytes = 0;        // their GPU memory
        uint32_t derived = 0;    // maps derived since the start (cache misses)
        uint32_t cached = 0;     // maps read from the cache
        double derive_ms = 0.0;  // worker time spent deriving them
    };
    Stats stats();
    // Every few seconds, when the stats changed: one "effects relief:" line (memory and load-time cost).
    void end_frame();

  private:
    void release(NormalMapEntry &entry);
    void read_cached(const std::shared_ptr<NormalMapEntry> &entry);

    bool enabled_ = false;
    int max_size_ = 1024;
    fx::ReliefClassifier classifier_;
    bool format_reported_ = false;
    bool cache_reported_ = false;
    bool loaded_while_off_ = false;
    std::vector<std::weak_ptr<NormalMapEntry>> entries_; // for configure() and stats()
    uint32_t frames_ = 0;
    Stats logged_{};
    struct Counters
    {
        std::atomic<uint32_t> derived{0}, cached{0};
        std::atomic<uint64_t> derive_us{0};
    };
    std::shared_ptr<Counters> counters_ = std::make_shared<Counters>(); // shared with the worker tasks
};

} // namespace storm::effects
