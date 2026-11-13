// Normal maps for the per-pixel lighting's relief (FX_LIGHT_NORMALMAP, fx/normal_map.hpp, fx/relief_store.hpp).
//
// Renderer::TextureLoad reports each texture's name and game file (SetEffectsTextureSource) right after its levels are
// filled. While kLightingNormalMap is on, fx::ReliefClassifier decides from the path (and, for "bump" names, the
// pixels) whether the texture gets relief:
//   * colour textures of the relief classes (stone, wood, buildings, plaster, ground): their maps are made by PBRify's models
//     in the game process (effects/relief_generator.hpp) the first time the texture is loaded without one, into the
//     relief directory, and kept there. Until its map is ready the texture has no relief; the "effects relief:" line
//     says how many wait.
//   * grey height ("bump") textures: their values are heights. The game derives their maps itself (fx::DeriveNormalMap
//     on a texture worker) into the same directory, once.
// Gloss maps (fx/gloss_map.hpp) follow the same path for the colour textures of a gloss class (TexStore::gloss_map):
// an entry at the load, looked up while Settings::gloss is on (now or when it is switched on), derived on a texture
// worker from the game file when the relief directory has none (~10 ms: no generator queue), stored there as EAC R11
// with mips, read and uploaded when a draw first asks (fading in like the relief).
// Maps are EAC RG11 with mips, read on a texture worker and uploaded when a draw first needs them (a smaller preset
// drops their top mips); with the parallax, the cavity or the gloss on, the colour textures' material texture (EAC RG11:
// R height, G gloss; the same mips, in the same file) is uploaded beside them. A texture whose relief class has a height
// takes its gloss from that material, not from a gloss map of its own. The relief fades in over kReliefFade. Turning the option off releases them. Every texture of
// a relief class gets its entry when it is loaded, relief on or off: one loaded while it is off is looked up (and
// requested or derived) when it is switched on, like one loaded then.
#pragma once

#include "storm/fx/gloss_map.hpp"
#include "storm/fx/normal_map.hpp"

#include <bgfx/bgfx.h>

#include "storm_render_types.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
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

// One texture's normal map (TexStore::normal_map) or gloss map (TexStore::gloss_map: `gloss` is not None), owned by its
// TexStore.
struct NormalMapEntry
{
    enum State : int
    {
        Unqueued, // not looked up (yet, or again): loaded while relief was off (configure() looks it up when it is
                  // switched on), or its map left the cache since (the next draw looks it up)
        Pending,  // on a texture worker (being looked up, read or derived)
        Stored,   // in the relief directory
        Waiting,  // a colour texture whose map the relief generator is making
        Missing,  // a colour texture without a map that will not get one: the generator is unavailable (logged)
        Derived,  // `map` is ready for the upload
        Uploaded, // `handle` is valid
        Failed,
    };
    std::atomic<int> state{Unqueued};
    std::atomic<bool> wanted{false}; // a draw asked for it: the worker keeps the map in memory for the upload
    std::atomic<uint32_t> drawn{0};  // the frame a draw last asked for it (the generator makes those on screen first)
    std::string name;                // normalized texture path, for the logs
    fx::ReliefClass relief = fx::ReliefClass::None;
    fx::GlossClass gloss = fx::GlossClass::None; // a gloss map's class
    fx::GlossClass material_gloss = fx::GlossClass::None; // a relief map's: the gloss class of its material's G
    uint64_t key = 0;     // fx::ReliefKey
    std::filesystem::path file; // the game file the texture was loaded from (the generator and the derivation read it)
    fx::NormalMap map;    // written by the worker before `state` becomes Derived
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle material = BGFX_INVALID_HANDLE; // the uploaded material texture (R height, G gloss)
    float depth = 0.0f;     // the height's 0..1 span in texels of the uploaded top mip
    int width = 0, height_texels = 0; // the uploaded top mip
    size_t bytes = 0; // of the uploaded textures
    size_t material_bytes = 0; // of them the material's
    std::chrono::steady_clock::time_point uploaded{}; // its relief fades in from here
    bool failure_reported = false;
    bool no_height_reported = false;
    ~NormalMapEntry();
};

class NormalMaps
{
  public:
    // `max_size`: larger side of the uploaded top mip (Settings::normal_map_size); `material`: the material textures are
    // uploaded too (the parallax, the cavity or the gloss reads them). Turning the option off or changing the size or
    // `material` releases the uploaded maps; they come back from the relief directory when drawn again. Turning it on
    // looks up the textures loaded while it was off.
    void configure(bool enabled, int max_size, bool material);
    // Main thread, right after the texture's levels were filled. `file`: the game file it was read from.
    void on_texture_loaded(shim::TexStore &store, const char *path, const std::filesystem::path &file);
    // The uploaded map of the texture, or invalid: no relief for it, its map still being made, read or derived (relief
    // appears later), or failed (logged). `fade`: 0..1, the share of its relief while it fades in.
    struct Relief
    {
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        float fade = 0.0f;
        bgfx::TextureHandle material = BGFX_INVALID_HANDLE; // the material texture, for a class with a height
        float depth = 0.0f;                                 // its height's span in texels of the top mip
        int width = 0, height_texels = 0;                 // the top mip
    };
    Relief handle(shim::TexStore &store, fx::Lighting &lighting);

    // Gloss maps: as configure() and handle(), for Settings::gloss and Settings::gloss_map_size.
    void configure_gloss(bool enabled, int max_size);
    Relief gloss(shim::TexStore &store, fx::Lighting &lighting);

    struct Stats
    {
        uint32_t resident = 0;  // uploaded maps
        size_t bytes = 0;       // their GPU memory (with the materials)
        uint32_t materials = 0; // of them with a material texture uploaded
        size_t material_bytes = 0;
        uint32_t unqueued = 0;  // loaded while relief was off, not looked up yet
        uint32_t waiting = 0;   // loaded colour textures of a relief class whose map is being made
        uint32_t missing = 0;   // loaded colour textures of a relief class that get no map (generator unavailable)
        uint32_t derived = 0;   // height maps derived since the start
        double derive_ms = 0.0; // worker time spent deriving them
        uint32_t gloss_resident = 0, gloss_unqueued = 0, gloss_derived = 0;
        size_t gloss_bytes = 0;
        double gloss_derive_ms = 0.0, gloss_slowest_ms = 0.0;
    };
    Stats stats();
    // Every few seconds, when the stats changed: one "effects relief:" line.
    void end_frame();

  private:
    void release(NormalMapEntry &entry);
    void read_stored(const std::shared_ptr<NormalMapEntry> &entry);
    // On a texture worker: Stored when the directory has its map, else requested (colour textures) or derived (height
    // maps, from the game file).
    void look_up(const std::shared_ptr<NormalMapEntry> &entry);
    // The same for a gloss map: Stored, or derived from the game file.
    void look_up_gloss(const std::shared_ptr<NormalMapEntry> &entry);
    Relief upload(const std::shared_ptr<NormalMapEntry> &entry, fx::Lighting &lighting, int max_size);
    void add_gloss(shim::TexStore &store, const std::string &name, std::span<const uint8_t> probe, uint32_t pw, uint32_t ph,
                   const std::filesystem::path &file);

    bool enabled_ = false;
    int max_size_ = 1024;
    bool material_ = false;
    fx::ReliefClassifier classifier_;
    bool format_reported_ = false;
    bool directory_reported_ = false;
    std::vector<std::weak_ptr<NormalMapEntry>> entries_; // for configure() and stats()
    bool gloss_enabled_ = false;
    int gloss_max_size_ = fx::kGlossMapSize;
    std::vector<std::weak_ptr<NormalMapEntry>> gloss_entries_;
    std::vector<std::string> gloss_logged_; // names whose gloss class was logged
    uint32_t frames_ = 0;
    uint32_t frame_ = 1; // for NormalMapEntry::drawn
    std::chrono::steady_clock::time_point now_ = std::chrono::steady_clock::now(); // this frame's, for the fade
    Stats logged_{};
    uint32_t logged_rest_ = 0, logged_made_ = 0;
    struct Counters
    {
        std::atomic<uint32_t> derived{0};
        std::atomic<uint64_t> derive_us{0};
        std::atomic<uint32_t> gloss_derived{0};
        std::atomic<uint64_t> gloss_derive_us{0}, gloss_slowest_us{0};
    };
    std::shared_ptr<Counters> counters_ = std::make_shared<Counters>(); // shared with the worker tasks
};

} // namespace storm::effects
