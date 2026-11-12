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
#include <vector>

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
    bool prepare(shim::Device &device, const std::array<float, 16> &view);
    // Receiver: binds the maps and their matrices for the draw's camera.
    void bind(const std::array<float, 16> &view);
    // Outdoors: the location's still geometry (empty indoors), drawn into the static map around the player
    // (buildings, walls, palms onto characters and props; the ground keeps its baked lightmap).
    void set_geometry(const std::vector<fx::Vec3> &positions, const std::vector<uint32_t> &indices);
    // Caster: the draw's buffers (and bone palette) are still bound (submitted with BGFX_DISCARD_NONE); `world` is
    // its world matrix.
    void submit_caster(shim::Device &device, const float *world, bool skinned);
    // The caster programs of `settings` (Lighting::prepare_programs).
    bool prepare_programs(shim::Device &device, const fx::Settings &settings, int &budget);
    void end_frame();

    void shutdown();
    void on_device_lost();

  private:
    void find_shaders();

    fx::Settings settings_{};
    fx::Shadows shadows_;
    std::array<float, 3> sun_dir_{0.0f, 1.0f, 0.0f};
    bool sun_present_ = false;
    bool prepared_ = false; // prepare() ran this frame
    bool active_ = false;   // ... and fx::Shadows::Begin() accepted it
    uint32_t vs_caster_[2] = {}; // [skinned]
    uint32_t fs_caster_ = 0;

    // Static map: the location geometry from the sun, cached; re-drawn when the player walks a quarter of its radius
    // or the sun turns half a degree.
    void draw_static(shim::Device &device, const fx::Vec3 &focus);
    std::vector<fx::Vec3> positions_;
    std::vector<uint32_t> indices_;
    bool geometry_dirty_ = false;
    bgfx::VertexBufferHandle static_vb_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle static_ib_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle static_map_ = BGFX_INVALID_HANDLE, open_map_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle static_fb_ = BGFX_INVALID_HANDLE;
    int static_size_ = 0;
    bool static_valid_ = false;
    fx::Vec3 static_focus_{}, static_sun_{};
    std::array<float, 16> world_to_static_{}, view_to_static_{}, static_view_{};
    std::array<float, 4> static_params_{};
    bgfx::UniformHandle s_static_ = BGFX_INVALID_HANDLE, u_static_mtx_ = BGFX_INVALID_HANDLE, u_static_ = BGFX_INVALID_HANDLE;
    bool static_reported_ = false;
};

} // namespace storm::effects
