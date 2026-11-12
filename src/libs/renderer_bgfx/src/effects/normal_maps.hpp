// Normal maps for the per-pixel lighting's relief (FX_LIGHT_NORMALMAP, fx/normal_map.hpp, fx/relief_store.hpp).
//
// Renderer::TextureLoad reports each texture's name and game file (SetEffectsTextureSource) right after its levels are
// filled. While kLightingNormalMap is on, fx::ReliefClassifier decides from the path (and, for "bump" names, the
// pixels) whether the texture gets relief:
//   * colour textures of the relief classes (stone, wood, buildings, plaster, ground): their maps are made by DeepBump
//     in the game process (effects/relief_generator.hpp) the first time the texture is loaded without one, into the
//     relief directory, and kept there. Until its map is ready the texture has no relief; the "effects relief:" line
//     says how many wait.
//   * grey height ("bump") textures: their values are heights. The game derives their maps itself (fx::DeriveNormalMap
//     on a texture worker) into the same directory, once.
// Maps are EAC RG11 with mips, read on a texture worker and uploaded when a draw first needs them (a smaller preset
// drops their top mips); the relief fades in over kReliefFade. Turning the option off releases them. Textures loaded
// while the option is off get no relief until they are loaded again (the next location): their levels are not kept.
#pragma once

#include "storm/fx/normal_map.hpp"

#include <bgfx/bgfx.h>

#include "storm_render_types.h"

#include <atomic>
#include <chrono>
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

struct ReliefSetup;
// The relief directory and the generator's model (render::ConfigureRelief). An empty directory: no relief at all
// (logged once).
void ConfigureRelief(ReliefSetup setup);

inline constexpr std::chrono::milliseconds kReliefFade{300};

// One texture's normal map, owned by its TexStore (TexStore::normal_map).
struct NormalMapEntry
{
    enum State : int
    {
        Pending,  // on a texture worker (being looked up, read or derived)
        Stored,   // in the relief directory
        Waiting,  // a colour texture whose map the relief generator is making
        Missing,  // a colour texture without a map that will not get one: the generator is unavailable (logged)
        Derived,  // `map` is ready for the upload
        Uploaded, // `handle` is valid
        Failed,
    };
    std::atomic<int> state{Pending};
    std::atomic<bool> wanted{false}; // a draw asked for it: the worker keeps the map in memory for the upload
    std::atomic<uint32_t> drawn{0};  // the frame a draw last asked for it (the generator makes those on screen first)
    std::string name;                // normalized texture path, for the logs
    fx::ReliefClass relief = fx::ReliefClass::None;
    uint64_t key = 0;     // fx::ReliefKey
    fx::NormalMap map;    // written by the worker before `state` becomes Derived
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    size_t bytes = 0; // of the uploaded texture
    std::chrono::steady_clock::time_point uploaded{}; // its relief fades in from here
    bool failure_reported = false;
    ~NormalMapEntry();
};

class NormalMaps
{
  public:
    // `max_size`: larger side of the uploaded top mip (Settings::normal_map_size). Turning the option off or changing
    // the size releases the uploaded maps; they come back from the relief directory when drawn again.
    void configure(bool enabled, int max_size);
    // Main thread, right after the texture's levels were filled. `file`: the game file it was read from.
    void on_texture_loaded(shim::TexStore &store, const char *path, const std::filesystem::path &file);
    // The uploaded map of the texture, or invalid: no relief for it, its map still being made, read or derived (relief
    // appears later), or failed (logged). `fade`: 0..1, the share of its relief while it fades in.
    struct Relief
    {
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        float fade = 0.0f;
    };
    Relief handle(shim::TexStore &store, fx::Lighting &lighting);

    struct Stats
    {
        uint32_t resident = 0;  // uploaded maps
        size_t bytes = 0;       // their GPU memory
        uint32_t waiting = 0;   // loaded colour textures of a relief class whose map is being made
        uint32_t missing = 0;   // loaded colour textures of a relief class that get no map (generator unavailable)
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
    uint32_t frame_ = 1; // for NormalMapEntry::drawn
    std::chrono::steady_clock::time_point now_ = std::chrono::steady_clock::now(); // this frame's, for the fade
    Stats logged_{};
    uint32_t logged_rest_ = 0, logged_made_ = 0;
    struct Counters
    {
        std::atomic<uint32_t> derived{0};
        std::atomic<uint64_t> derive_us{0};
    };
    std::shared_ptr<Counters> counters_ = std::make_shared<Counters>(); // shared with the worker tasks
};

} // namespace storm::effects
