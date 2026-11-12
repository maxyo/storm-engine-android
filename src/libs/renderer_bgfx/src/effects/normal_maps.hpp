// Normal maps for the per-pixel lighting's relief (FX_LIGHT_NORMALMAP, fx/normal_map.hpp, fx/relief_store.hpp).
//
// Renderer::TextureLoad reports each texture's file (SetEffectsTextureSource) right after its levels are filled.
// While kLightingNormalMap is on, fx::ReliefClassifier decides from the path (and, for "bump" names, the pixels)
// whether the texture gets relief:
//   * colour textures of the relief classes (stone, wood, buildings, plaster, ground): their maps are made once from
//     the game's files by DeepBump at import ("Подготовка рельефа", src/libs/relief) into the relief directory. The
//     game only reads them. A texture without its prepared map has no relief; how many are missing is logged
//     ("effects relief:" line). Nothing is derived in its place.
//   * grey height ("bump") textures: their values are heights. The game derives their maps itself (fx::DeriveNormalMap
//     on a texture worker) into the same directory, once.
// Maps are EAC RG11 with mips, read on a texture worker and uploaded when a draw first needs them (a smaller preset
// drops their top mips). Turning the option off releases them. Textures loaded while the option is off get no relief
// until they are loaded again (the next location): their levels are not kept.
#pragma once

#include "storm/fx/normal_map.hpp"

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

// The relief directory (render::ConfigureReliefDirectory). Empty: no relief at all (logged once).
void ConfigureReliefDirectory(const std::filesystem::path &directory);

// One texture's normal map, owned by its TexStore (TexStore::normal_map).
struct NormalMapEntry
{
    enum State : int
    {
        Pending,  // on a texture worker (being looked up, read or derived)
        Stored,   // in the relief directory
        Missing,  // a colour texture without its prepared map: no relief
        Derived,  // `map` is ready for the upload
        Uploaded, // `handle` is valid
        Failed,
    };
    std::atomic<int> state{Pending};
    std::atomic<bool> wanted{false}; // a draw asked for it: the worker keeps the map in memory for the upload
    std::string name;                // normalized texture path, for the logs
    fx::ReliefClass relief = fx::ReliefClass::None;
    uint64_t key = 0;     // fx::ReliefKey
    fx::NormalMap map;    // written by the worker before `state` becomes Derived
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    size_t bytes = 0; // of the uploaded texture
    bool failure_reported = false;
    ~NormalMapEntry();
};

class NormalMaps
{
  public:
    // `max_size`: larger side of the uploaded top mip (Settings::normal_map_size). Turning the option off or changing
    // the size releases the uploaded maps; they come back from the relief directory when drawn again.
    void configure(bool enabled, int max_size);
    // Main thread, right after the texture's levels were filled.
    void on_texture_loaded(shim::TexStore &store, const char *path);
    // The uploaded map of the texture, or invalid: no relief for it, no prepared map, still being read or derived
    // (relief appears a few frames later), or failed (logged).
    bgfx::TextureHandle handle(shim::TexStore &store, fx::Lighting &lighting);

    struct Stats
    {
        uint32_t resident = 0;  // uploaded maps
        size_t bytes = 0;       // their GPU memory
        uint32_t missing = 0;   // loaded colour textures of a relief class without a prepared map
        uint32_t derived = 0;   // height maps derived since the start
        double derive_ms = 0.0; // worker time spent deriving them
    };
    Stats stats();
    // Every few seconds, when the stats changed: one "effects relief:" line.
    void end_frame();

  private:
    void release(NormalMapEntry &entry);
    void read_stored(const std::shared_ptr<NormalMapEntry> &entry);

    bool enabled_ = false;
    int max_size_ = 1024;
    fx::ReliefClassifier classifier_;
    bool format_reported_ = false;
    bool directory_reported_ = false;
    bool loaded_while_off_ = false;
    std::vector<std::weak_ptr<NormalMapEntry>> entries_; // for configure() and stats()
    uint32_t frames_ = 0;
    Stats logged_{};
    struct Counters
    {
        std::atomic<uint32_t> derived{0};
        std::atomic<uint64_t> derive_us{0};
    };
    std::shared_ptr<Counters> counters_ = std::make_shared<Counters>(); // shared with the worker tasks
};

} // namespace storm::effects
