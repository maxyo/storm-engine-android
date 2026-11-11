// Sun shadows for the per-pixel lighting (fx::Shadows, FX_LIGHT_SHADOW variants of fs_ff_lit).
//
// One orthographic cascade around the camera's focus, rendered into the pre-game view kPreGameViewBegin (it runs before
// every game view of the frame, so receivers of the same frame read it). The casters are the lit fixed-function draws
// into the game picture (ships with their sails, characters, lit deck and location objects): the shim submits each of
// them a second time into the shadow view with the depth-only caster program vs_ff_caster[_skin] / fs_ff_caster
// (draw.cpp: submitted with BGFX_DISCARD_NONE, then submit_caster() reuses its buffers and bone palette). The map
// sits at stage 15 (fx::kShadowMapStage): 13 is the bone palette of the skinned vertex shaders. The receivers are the same lit draws: they sample the map at stage 13 with their own camera.
// Baked location geometry (lighting off) neither casts nor receives: the legacy shadow entity keeps drawing the
// projected shadows on the ground, and the baked colours already hold the location's static shading.
// The map is prepared lazily by the first lit draw into the game picture of a frame (its camera is the scene camera);
// a sun at or below the horizon gives no shadows (fx::Shadows fades them out by design).
#pragma once

#include "storm/fx/settings.hpp"
#include "storm/fx/shadows.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace storm::shim
{
struct Device;
}

namespace storm::effects
{

class SunShadows
{
  public:
    void configure(const fx::Settings &settings) { settings_ = settings; }
    bool enabled() const { return settings_.shadows != fx::ShadowQuality::Off; }
    // World direction towards the sun of this frame (WEATHER); none: no shadows this frame.
    void set_sun(const std::array<float, 3> &sun_dir, bool present)
    {
        sun_dir_ = sun_dir;
        sun_present_ = present;
    }
    // For a lit draw into the game picture with the camera `view` (D3D row-vector world -> view): prepares this
    // frame's map on the first call. True when the draw casts and receives shadows.
    bool prepare(const std::array<float, 16> &view);
    // Receiver: binds the map and its matrix for the draw's camera.
    void bind(const std::array<float, 16> &view) { shadows_.Bind(view); }
    // Caster: the draw's buffers (and bone palette) are still bound (submitted with BGFX_DISCARD_NONE); `world` is
    // its world matrix.
    void submit_caster(shim::Device &device, const float *world, bool skinned);
    void end_frame();

    void shutdown() { shadows_.Shutdown(); }
    void on_device_lost() { shadows_.OnDeviceLost(); }

  private:
    fx::Settings settings_{};
    fx::Shadows shadows_;
    std::array<float, 3> sun_dir_{0.0f, 1.0f, 0.0f};
    bool sun_present_ = false;
    bool prepared_ = false; // prepare() ran this frame
    bool active_ = false;   // ... and fx::Shadows::Begin() accepted it
    uint32_t vs_caster_[2] = {}; // [skinned]
    uint32_t fs_caster_ = 0;
};

} // namespace storm::effects
