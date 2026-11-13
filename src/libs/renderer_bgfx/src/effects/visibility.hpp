// How open to the sky every point of the current location is (fx/visibility.hpp): the one visibility bake of every
// location that reports its still geometry (towns, interiors, ship decks and holds), for whatever lights by it.
//
// render::SetEffectsLocationGeometry hands over the still models' collision data. The volume is
// read from the texture cache directory (the app's cache: an entry that is gone is baked again, and said) or baked on
// a texture worker, then uploaded as an RGBA8 3D texture: R sky, G open (fx::VisibilityVolume). Every lit and baked
// fixed-function draw binds it at fx::kVisibilityStage (ff_fragment.sh: ffVisibility); a draw before the volume is
// ready, or where there is none, binds a 1x1x1 open volume (sky 1, open 1) and u_fxVisibilityParams.x = 0.
//
// With the bounce (effects/bounce.hpp, set_stacked) the texture holds a second block of the same cells above the
// first along y, one guard slice between them: block 1 the volume (B and A the bounce's direction x / z), block 2 the
// bounce (fx::EncodeBounceBlocks). u_fxVisibilityParams.z is block 2's offset in v (0: one block, no bounce).
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
        fx::Vec3 origin{};                    // world corner of cell (0, 0, 0)
        float block_offset = 0.0f;            // v of block 2 over block 1 (0: one block)
        float lamp_share = 0.0f;              // of the bounce's light, the lamps'
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
    // Whether the texture carries the bounce's block (Bounce on for this place); changing it uploads it again.
    // False when the doubled grid exceeds the 3D texture size every GLES 3.0 device has (logged once).
    bool set_stacked(bool stacked);
    // The bounce's bytes of every cell (fx::EncodeBounceBlocks): `directions` gives block 1 its B and A, `block` is
    // block 2; uploaded at once. Ignored unless stacked.
    void set_bounce(const std::vector<uint8_t> &directions, std::vector<uint8_t> block, float lamp_share);

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
    bool upload(); // (re)creates the texture from volume_ and the bounce
    bool fits_stacked(); // both blocks fit a 3D texture (else logged once)
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
    fx::VisibilityVolume volume_; // kept: the bounce rewrites the texture
    bool stacked_ = false;
    bool too_tall_logged_ = false;
    bool reupload_ = false;       // the device was lost
    std::vector<uint8_t> bounce_; // block 2, empty: zeros
};

} // namespace storm::effects
