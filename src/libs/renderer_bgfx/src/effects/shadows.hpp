// Sun shadows for the per-pixel lighting (fx::Shadows, FX_LIGHT_SHADOW variants of fs_ff_lit).
//
// One orthographic cascade around the camera's focus, rendered into the pre-game view kPreGameViewBegin (it runs before
// every game view of the frame, so receivers of the same frame read it). The casters are the lit fixed-function draws
// into the game picture (ships with their sails, characters, lit deck and location objects): the shim submits each of
// them a second time into the shadow view with the depth-only caster program vs_ff_caster[_skin] / fs_ff_caster
// (draw.cpp: submitted with BGFX_DISCARD_NONE, then submit_caster() reuses its buffers and bone palette). The map
// sits at stage 15 (fx::kShadowMapStage): 13 is the bone palette of the skinned vertex shaders. The receivers are the same lit draws: they sample the map at stage 13 with their own camera.
// Baked location geometry (lighting off) neither casts nor receives: the legacy shadow entity keeps drawing the
// projected shadows on the ground, and the baked colours already hold the location's static shading. The shade of the
// buildings and palms reaches the receivers from those baked colours too: outdoors each lit draw takes the sun the
// location's lightmaps show under it (fx::GroundLight, set_ground), so a character is in shade exactly where the
// ground around him is.
// The map is prepared lazily by the first lit draw into the game picture of a frame (its camera is the scene camera);
// a sun at or below the horizon gives no shadows (fx::Shadows fades them out by design).
#pragma once

#include "storm/fx/settings.hpp"
#include "storm/fx/ground_light.hpp"
#include "storm/fx/shadows.hpp"

#include "effects/casters.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <utility>

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
    // Receiver: binds the map and its matrix for the draw's camera, and its sun from the ground (ground_sun).
    void bind(const std::array<float, 16> &view, float ground_sun) { shadows_.Bind(view, ground_sun); }
    // The location's lightmapped ground (empty: none, e.g. at sea or indoors).
    void set_ground(fx::GroundLight ground) { ground_ = std::move(ground); }
    bool has_ground() const { return !ground_.Empty(); }
    // The sun of a lit draw whose world-space bounding sphere is centred at `center`: the lightmaps' sun on the ground
    // under it (1 where there is no lightmapped ground).
    float ground_sun(const std::array<float, 3> &center) const
    {
        const auto sun = ground_.SunVisibility({center[0], center[1], center[2]});
        return sun ? *sun : 1.0f;
    }
    // Caster: the draw's buffers, textures and bone palette are still bound (submitted with BGFX_DISCARD_NONE);
    // `world` is its world matrix, `material` how it casts (effects/casters.hpp).
    void submit_caster(shim::Device &device, const float *world, bool skinned, Casters &casters, const CasterMaterial &material);
    void end_frame();

    void shutdown() { shadows_.Shutdown(); }
    void on_device_lost() { shadows_.OnDeviceLost(); }

  private:
    fx::Settings settings_{};
    fx::Shadows shadows_;
    fx::GroundLight ground_;
    std::array<float, 3> sun_dir_{0.0f, 1.0f, 0.0f};
    bool sun_present_ = false;
    bool prepared_ = false; // prepare() ran this frame
    bool active_ = false;   // ... and fx::Shadows::Begin() accepted it
};

} // namespace storm::effects
