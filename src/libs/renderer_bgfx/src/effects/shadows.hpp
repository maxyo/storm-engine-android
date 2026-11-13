// Sun shadows for the per-pixel lighting (fx::Shadows, FX_LIGHT_SHADOW variants of fs_ff_lit).
//
// One orthographic cascade around the camera's focus, rendered into the pre-game view kPreGameViewBegin (it runs before
// every game view of the frame, so receivers of the same frame read it). The casters are the lit fixed-function draws
// into the game picture (ships with their sails, characters, lit deck and location objects): the shim submits each of
// them a second time into the shadow view with the depth-only caster program vs_ff_caster[_skin] / fs_ff_caster
// (draw.cpp: submitted with BGFX_DISCARD_NONE, then submit_caster() reuses its buffers and bone palette). The map
// sits at stage 15 (fx::kShadowMapStage): 13 is the bone palette of the skinned vertex shaders. The receivers are the same lit draws: they sample the map at stage 13 with their own camera.
// Baked location geometry (lighting off) and surfaces under a lightmap neither cast nor receive: their shading is in
// their colours and lightmaps (outdoors our own: effects/town_light.hpp, for the same sun). Outdoors the place's still
// geometry casts from tiles (set_casters) into the cascade along the hour's sun, so the houses, walls and palms shade
// the characters and ships where the lightmaps shade the ground.
// The map is prepared lazily by the first lit draw into the game picture of a frame (its camera is the scene camera);
// a sun at or below the horizon gives no shadows (fx::Shadows fades them out by design).
#pragma once

#include "storm/fx/settings.hpp"
#include "storm/fx/shadows.hpp"
#include "effects/casters.hpp"
#include "storm/render/effects.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <string>
#include <vector>
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
    // The place's still, collidable geometry (render::SetEffectsLocationGeometry, outdoors): it casts into the
    // cascade in tiles, with the holes its textures' alpha cuts (effects/casters.hpp). Empty: none (indoors, at sea).
    void set_casters(const render::EffectsLocationGeometry &geometry);
    // For a lit draw into the game picture with the camera `view` (D3D row-vector device world -> view; `origin`: the
    // device world's origin in absolute world, Lighting::set_world_origin): prepares this frame's map on the first
    // call, with the place's still geometry near the camera as casters. True when the draw casts and receives shadows.
    bool prepare(shim::Device &device, const std::array<float, 16> &view, const std::array<float, 3> &origin,
                 Casters &casters);
    // Receiver: binds the map and its matrix for the draw's camera.
    void bind(const std::array<float, 16> &view) { shadows_.Bind(view); }
    // Caster: the draw's buffers, textures and bone palette are still bound (submitted with BGFX_DISCARD_NONE);
    // `world` is its world matrix, `material` how it casts (effects/casters.hpp).
    void submit_caster(shim::Device &device, const float *world, bool skinned, Casters &casters, const CasterMaterial &material);
    void end_frame();

    void shutdown();
    void on_device_lost();

  private:
    fx::Settings settings_{};
    fx::Shadows shadows_;
    std::string casters_name_;
    std::vector<fx::Vec3> casters_;
    std::vector<std::array<float, 2>> caster_uvs_;
    std::vector<uint32_t> caster_indices_;
    std::vector<shim::BaseTexture *> caster_texture_; // per triangle: the cut-out texture, null when solid
    std::array<float, 3> sun_dir_{0.0f, 1.0f, 0.0f};
    bool sun_present_ = false;
    bool prepared_ = false; // prepare() ran this frame
    bool active_ = false;   // ... and fx::Shadows::Begin() accepted it
    // The place's still geometry in tiles of kTileSize metres (xz): the tiles the cascade reaches are drawn into it.
    struct Tile
    {
        fx::Vec3 lo{}, hi{};
        bgfx::IndexBufferHandle solid = BGFX_INVALID_HANDLE;
        std::vector<std::pair<shim::BaseTexture *, bgfx::IndexBufferHandle>> cut; // with their texture's holes
        uint32_t triangles = 0;
    };
    bgfx::VertexBufferHandle tiles_vb_ = BGFX_INVALID_HANDLE; // positions and texture coordinates of every tile
    std::vector<Tile> tiles_;
    bool tiles_dirty_ = false;
    void build_tiles();
    void destroy_tiles();
    uint32_t frame_tiles_ = 0, frame_tile_triangles_ = 0;

  public:
    // The still-geometry casters of the last frame (tiles, triangles): the effects profile line.
    std::array<uint32_t, 2> last_static_casters() const { return last_static_; }

  private:
    std::array<uint32_t, 2> last_static_{};
};

} // namespace storm::effects
