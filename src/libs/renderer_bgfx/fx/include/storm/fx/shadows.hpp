// Sun shadow map for per-pixel lit surfaces (ships, sails, characters and
// whatever else the emulator draws with FX_LIGHT_SHADOW): one orthographic
// cascade around the camera's focus, hardware-filtered depth compare.
//
// Per frame:
//   if (shadows.Begin(settings, frame))            // sets up frame.view
//       for each caster (ships, characters: what the legacy shadow entity projects):
//           bgfx::setTransform(world); set buffers; bgfx::setState(Shadows::kCasterState);
//           bgfx::submit(frame.view, shadows.CasterProgram());
//   ...
//   shadows.Bind(camera_view);                     // before every receiver draw call
#pragma once

#include "storm/fx/shadow_model.hpp"

#include <bgfx/bgfx.h>

namespace storm::fx
{

struct ShadowFrame
{
    bgfx::ViewId view = 0;         // a view before the scene views
    Vec3 sun_dir{0.0f, 1.0f, 0.0f}; // unit, towards the sun
    Vec3 focus{0.0f, 0.0f, 0.0f};   // centre of the shadowed region: the player's ship or character
    float radius = 60.0f;          // metres around the focus that receive shadows
    float caster_reach = 120.0f;   // how far towards the sun casters are still captured
};

class Shadows
{
  public:
    // Depth only, front and back faces (sails are single-sided sheets).
    static constexpr uint64_t kCasterState = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;

    Shadows() = default;
    ~Shadows();
    Shadows(const Shadows &) = delete;
    Shadows &operator=(const Shadows &) = delete;

    // Prepares the shadow view. Returns false when nothing should be rendered into it this frame (shadows
    // off, or the light is at or below the horizon: shadows fade out as the sun sets, by design); receivers
    // then get "fully lit" from Bind(). An unsupported device is an error (Support::shadow_map).
    bool Begin(const Settings &settings, const ShadowFrame &frame);
    bgfx::ProgramHandle CasterProgram();

    // Sets the shadow map (sampler stage kShadowMapStage), u_fxShadowMtx and u_fxShadow for the next draw call.
    // `camera_view`: the receiver's world -> view matrix (receivers shade in view space; draws of one frame may use
    // different cameras). Required for every draw of a FX_LIGHT_SHADOW variant; calling it before the first Begin()
    // is an error. `ground_sun`: the receiver's sun from the location's lightmaps (fx::GroundLight; 1 = none): the
    // receiver is never more in the sun than that.
    void Bind(const std::array<float, 16> &camera_view, float ground_sun);

    void Shutdown();
    void OnDeviceLost();

  private:
    void EnsureResources(int map_size);

    bgfx::FrameBufferHandle target_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle map_ = BGFX_INVALID_HANDLE;
    int map_size_ = 0;
    bgfx::ProgramHandle caster_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_map_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_matrix_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_params_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_ground_ = BGFX_INVALID_HANDLE;
    std::array<float, 16> world_to_shadow_{}; // world -> shadow map uv and 0..1 depth
    std::array<float, 16> camera_view_{};     // the camera view_to_shadow_ was made for
    std::array<float, 16> view_to_shadow_{};
    std::array<float, 4> params_{};
    bool reported_bind_ = false; // one report per object is enough: both causes mean "no shadow map"
};

} // namespace storm::fx
