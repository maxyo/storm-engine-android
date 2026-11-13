// The neural ×2 textures on the GPU (fx/upscale.hpp, Settings::upscale: Высокая; fx_upscale forces it on or off).
//
// Every loaded texture fx::ClassifyUpscale gives a target (the relief classes' colour textures, the characters') gets
// an entry. Its ×2 costs four times its memory, so only the textures drawn near the camera are resident at ×2:
//   * each draw that binds the texture reports the distance from the camera to its vertices' bounding sphere (the
//     skinned draws theirs in the bind pose under the character's world matrix);
//   * twice a second (and only after two frames drew, a slow frame being longer) fx::PlanUpscaleResidency picks, nearest first, those within fx::kUpscaleNear (a resident one until
//     fx::kUpscaleFar) while their sum fits the budget (fx::UpscaleBudgetBytes of the device's RAM, logged at the
//     start). The others are released and draw at their own size. When the budget leaves near textures at their own size
//     an "effects upscale: budget" line says how many and the nearest (each time that changes);
//   * a chosen texture's ×2 is read from the upscale directory on a texture worker and uploaded (a KTX container with
//     its whole chain, in the device's tier: ETC2 or BC); one not made yet is requested from the relief generator
//     (effects/relief_generator.hpp: nice 10, the shared thermal pause, at most kUpscaleQueue waiting).
// Draws bind the ×2 in place of the texture (shim Device::draw); nothing else about the draw changes. The relief maps
// of textures up to the map's size are made from their ×2 whatever this setting (src/libs/relief).
#pragma once

#include "storm/fx/upscale.hpp"

#include <bgfx/bgfx.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace storm::shim
{
struct TexStore;
}

namespace storm::effects
{

// Requests of the ×2 the generator keeps at most; more wait for the next plan.
inline constexpr size_t kUpscaleQueue = 32;

struct UpscaleEntry
{
    enum State : int
    {
        Unknown,  // not looked up yet
        Pending,  // on a texture worker: looked up or read
        Stored,   // its file is in the upscale directory
        Missing,  // no file: requested when chosen
        Waiting,  // the generator is making it
        Read,     // `ktx` holds the file for the upload
        Resident, // `handle` is valid
        Failed,   // logged; drawn at its own size
    };
    std::atomic<int> state{Unknown};
    std::string name; // normalized texture path
    std::filesystem::path file;
    fx::UpscaleTarget target = fx::UpscaleTarget::None;
    uint64_t key = 0;
    uint32_t width = 0, height = 0; // of the texture (the ×2 is twice that)
    uint64_t bytes = 0;             // GPU memory of the ×2: estimated from the texture's format until it is read
    float nearest = -1.0f;          // nearest draw since the last plan, metres; -1 not drawn
    float last_distance = -1.0f;    // the last plan's distance of a drawn entry
    std::chrono::steady_clock::time_point last_drawn{}; // when a plan last found it drawn
    std::vector<uint8_t> ktx;       // written by the worker before `state` becomes Read
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    ~UpscaleEntry();
};

class Upscales
{
  public:
    // Main thread, after the renderer started (bgfx caps). `enabled`: Settings::upscale. Switching it off releases
    // every ×2; the entries stay (switching it on finds them).
    // `budget_mb`: Settings::upscale_budget_mb (0: from the device's RAM).
    void configure(bool enabled, uint16_t budget_mb);
    // The same setting again (the directory is known now: ConfigureUpscaleDirectory).
    void reconfigure() { configure(wanted_, wanted_budget_mb_); }
    // Main thread, right after the texture's levels were filled (effects SetEffectsTextureSource).
    void on_texture_loaded(shim::TexStore &store, const std::string &name, const std::filesystem::path &file);
    // A draw binding `store` at `distance` metres: the ×2 to bind instead, or invalid.
    bgfx::TextureHandle replacement(shim::TexStore &store, float distance);
    // Whether draws must report their distance (any entry and the option on).
    bool active() const { return enabled_; }
    // On: the last plan (they run twice a second) found every texture it chose resident at ×2 (render::UpscaleSettled).
    bool settled() const;
    void end_frame();

    struct Stats
    {
        uint32_t entries = 0, resident = 0, reading = 0, waiting = 0, missing = 0, failed = 0;
        uint64_t bytes = 0, budget = 0;
        int limited = 0;
    };
    Stats stats() const;

  private:
    bool resolve(); // the directory, the tier and the budget, once the directory is known
    void plan();
    void look_up(const std::shared_ptr<UpscaleEntry> &entry, bool read);
    void release(UpscaleEntry &entry);

    bool enabled_ = false;
    bool wanted_ = false; // Settings::upscale
    uint16_t wanted_budget_mb_ = 0;
    bool configured_ = false;
    bool directory_reported_ = false;
    bool supported_ = false;
    bool bc_ = false;
    uint32_t max_size_ = 0;
    uint64_t budget_ = 0;
    std::filesystem::path directory_;
    std::vector<std::weak_ptr<UpscaleEntry>> entries_;
    std::chrono::steady_clock::time_point next_plan_{};
    bool planned_ = false; // a plan ran since the option was switched on
    int outstanding_ = 0;  // textures the last plan chose that were not resident yet
    uint32_t frames_since_on_ = 0;
    uint32_t frames_since_plan_ = 0;
    int limited_ = 0;
    int logged_limited_ = -1;
    Stats logged_{};
    std::chrono::steady_clock::time_point next_log_{};
    bool size_reported_ = false;
};

Upscales &upscales();
// The upscale directory (render::ConfigureRelief), before the first texture is loaded.
void ConfigureUpscaleDirectory(const std::filesystem::path &directory);

} // namespace storm::effects
