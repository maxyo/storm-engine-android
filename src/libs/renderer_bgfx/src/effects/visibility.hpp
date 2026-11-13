// How open to the sky every point of the current location is (fx/visibility.hpp): the one visibility bake of every
// location that reports its still geometry (towns, interiors, ship decks and holds), for whatever lights by it.
//
// render::SetEffectsLocationGeometry hands over the still models' collision data. The volume is
// read from the texture cache directory (the app's cache: an entry that is gone is baked again, and said) or baked on
// a texture worker, then uploaded as an RGBA8 3D texture: R sky, G open (fx::VisibilityVolume). Every lit and baked
// fixed-function draw binds it at fx::kVisibilityStage (ff_fragment.sh: ffVisibility); a draw before the volume is
// ready, or where there is none, binds a 1x1x1 open volume (sky 1, open 1) and u_fxVisibilityParams.x = 0.
#pragma once

#include "storm/fx/visibility.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace storm::effects
{

class Visibility
{
  public:
    enum class State
    {
        None,     // no location geometry
        Building, // on a texture worker
        Ready,
        Failed, // logged once per location
    };
    struct Bound
    {
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
        std::array<float, 16> world_to_uvw{}; // row-vector absolute world -> 0..1 texture coordinates
        std::array<int, 3> size{};            // cells
        float cell = 0.0f;                    // metres
    };

    // Main thread. The location's collision triangles (absolute world; what its traces hit); `interior`:
    // room_lo..room_hi bound the cells (else the geometry's box). An empty geometry: no location, no volume.
    void set_geometry(std::string name, std::vector<fx::Vec3> positions, std::vector<uint32_t> indices, bool interior,
                      const std::array<float, 3> &room_lo, const std::array<float, 3> &room_hi);
    // Main thread. A town's lightmapped ground (render::SetEffectsLightmaps: absolute world box): outdoors the cells
    // cover it (and the air above it) instead of the whole island, so they come out finer under the same budget.
    void set_focus(std::string_view name, const std::array<float, 3> &lo, const std::array<float, 3> &hi);
    // Main thread, once per frame: starts the load or bake, uploads a finished one. The volume to bind, or nullptr.
    const Bound *update();
    State state() const { return state_; }

    void shutdown();
    void on_device_lost();

  private:
    struct Job
    {
        std::atomic<int> state{0}; // 0 pending, 1 ready, 2 failed
        fx::VisibilityVolume volume;
        std::string how;
    };
    void start();
    void destroy_texture();
    void restart(); // the parameters changed: the volume goes, a new one is made
    fx::VisibilityParams params_for() const;

    std::string name_;
    std::vector<fx::Vec3> positions_;
    std::vector<uint32_t> indices_;
    fx::VisibilityParams params_;
    bool interior_ = false;
    std::array<float, 3> room_lo_{}, room_hi_{};
    std::string focus_name_;
    std::array<float, 3> focus_lo_{}, focus_hi_{};
    std::shared_ptr<Job> job_;
    State state_ = State::None;
    Bound bound_{};
};

} // namespace storm::effects
