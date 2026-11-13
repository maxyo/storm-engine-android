// Our own lightmaps for the towns (fx/town_light.hpp), in place of the game's on Средняя / Высокая.
//
// When an outdoor place is loaded the location reports its lightmapped triangles and lightmaps
// (render::SetEffectsLightmaps) and its still geometry (render::SetEffectsLocationGeometry). On a worker, once per
// place (cached on disk): each lightmap's texels, the sky's visibility on them, the direction the game's lightmaps
// were baked with (fx::FitBakedSun) and their tone (fx::CalibrateTownLight). Then, whenever the sun of the hour has
// moved by kSunStepDegrees, its visibility on the texels for the new direction, again on a worker; the lightmap
// blends from the last direction to the new one over kBlendSeconds and is uploaded to our own texture. A draw that
// samples one of the game's lightmaps samples ours instead at the same stage (Lighting::prepare / submit): one texture
// fetch as before. Until the first bake of a place is done, and on Низкая, the game's lightmaps stay.
//
// With the bounce (set_bounce_scene, effects/bounce.hpp) each sun bake also gathers the sun's bounce field for the same
// direction and reads it on the texels: the light the lit surfaces around reflect adds to the sun's on our lightmap,
// and the bounce of the bake's own sun (once per place) leaves the game's. The field goes on to the bounce texture
// (sun_bounce): lit models in the street take the same light.
#pragma once

#include "effects/bounce.hpp"
#include "storm/fx/sun_fit.hpp"
#include "storm/fx/town_light.hpp"
#include "storm/render/effects.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace storm::shim
{
struct TexStore;
}

namespace storm::effects
{

class TownLight
{
  public:
    void set_enabled(bool enabled) { enabled_ = enabled; }
    // Debug view (option fx_town_light_debug): our lightmap shows the sun's visibility it holds (grey) in place of
    // the relit paint, to prove where the hour's sun reaches.
    void set_debug(bool debug);
    // The place's lightmapped triangles and lightmaps (empty: none, e.g. at sea, indoors, when leaving).
    void set_lightmaps(std::string_view name, std::span<const std::array<float, 3>> positions,
                       std::span<const std::array<float, 2>> uvs, std::span<const uint32_t> indices,
                       std::span<const uint32_t> triangle_lightmap, std::span<const render::EffectsLightmap> lightmaps);
    // The place's still geometry (outdoors): what casts the shadows and closes the sky.
    // `uvs` per position and `cut` per triangle: palm leaves and nets let the sun and the sky through their holes.
    // `colors`: the baked colours per position (the walls' relight is calibrated on them).
    void set_occluders(std::string_view name, std::span<const std::array<float, 3>> positions,
                       std::span<const std::array<float, 2>> uvs, std::span<const uint32_t> indices,
                       std::vector<fx::Occluders::Cut> cut, std::span<const uint32_t> colors);
    // Main thread, every frame: the place's visibility volume (null: none yet); the walls are calibrated with it.
    void set_volume(const fx::VisibilityVolume *volume);
    // The walls' relight (Lighting, u_fxTownWall): on while the sun is up and the walls are calibrated.
    bool walls_active() const { return active() && wall_ready_ && sun_present_ && wall_.valid; }
    const fx::TownLightTone *wall_light() const { return wall_ready_ ? &wall_light_ : nullptr; }
    const fx::TownWallTone *wall_tone() const { return &wall_; }
    const std::array<float, 3> &live_sun() const { return live_sun_; }
    float live_sun_luminance() const { return live_sun_luminance_; }
    std::array<float, 3> bake_sun() const { return {bake_dir_[0], bake_dir_[1], bake_dir_[2]}; }
    // Once per frame, before the draws: `sun` the hour's direction towards the sun (world), `present` false at night.
    void update(const std::array<float, 3> &sun, const std::array<float, 3> &sun_color, bool present, float seconds);
    // Main thread, every frame: the place's bounce scene (null: none, or not ready).
    void set_bounce_scene(std::shared_ptr<const Bounce::Scene> scene);
    // The sun's bounce field of the last sun bake (white sun; null: none yet).
    std::shared_ptr<const fx::BounceField> sun_bounce() const { return sun_bounce_; }
    // The texture a draw samples in place of the game's lightmap `store` (invalid: keep the game's).
    // Our lightmap for a draw that binds the game's `store` (any draw: the shim asks for every lightmap it binds),
    // invalid when it has none; counted for the effects profile.
    bgfx::TextureHandle replacement(const shim::TexStore *store);
    void end_frame();
    // Draws of the last frame that bound a game town lightmap / were given ours instead.
    std::array<uint32_t, 2> last_counts() const { return last_counts_; }
    bool active() const { return enabled_ && ready_; }

    void shutdown();
    void on_device_lost();

  private:
    struct Map
    {
        const shim::TexStore *store = nullptr; // the game's lightmap (its texture) this one replaces
        std::shared_ptr<const fx::LightmapTexels> texels; // shared with the sun jobs on the worker
        std::vector<uint8_t> game;   // its RGBA8 texels at texels' size
        std::vector<uint8_t> sky;    // per texel
        std::vector<uint8_t> baked;  // per texel: the sun's visibility along the direction the game's was baked with
        fx::TownLightTone tone;
        std::vector<uint8_t> sun_from, sun_to; // per texel: the last and the coming sun direction's visibility
        std::vector<uint8_t> bounce_baked, bounce_from, bounce_to; // RGB per texel (fx::TexelBounce), empty: none
        std::vector<uint8_t> rgba;             // composed
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    };
    struct PlaceJob
    {
        std::atomic<int> state{0}; // 0 running, 1 done
        std::vector<Map> maps;
        fx::SunFit bake;
        std::string how;
    };
    struct SunJob
    {
        std::atomic<int> state{0};
        std::array<float, 3> sun{};
        std::vector<std::vector<uint8_t>> visibility; // per map
        double ms = 0.0;                                // how long the bake took on its worker threads
        std::shared_ptr<const fx::BounceField> field;   // with a bounce scene: the sun's field
        std::vector<std::vector<uint8_t>> bounce;       // per map, per texel (empty: no bounce scene)
        double bounce_ms = 0.0;
        bool baked_bounce = false;                      // also the bake's sun's bounce (first bake with the scene)
        std::vector<std::vector<uint8_t>> bounce_baked;
    };
    void start_place();
    void start_sun(const std::array<float, 3> &sun);
    void compose_and_upload();
    void destroy_textures();

    bool enabled_ = false;
    bool ready_ = false;
    std::string name_, occluders_name_;
    std::vector<fx::Vec3> positions_;
    std::vector<std::array<float, 2>> uvs_;
    std::vector<uint32_t> indices_, triangle_lightmap_;
    std::vector<render::EffectsLightmap> lightmaps_;
    std::shared_ptr<const fx::Occluders> occluders_;
    std::vector<fx::Vec3> occluder_positions_;
    std::vector<uint32_t> occluder_indices_;
    std::vector<std::array<float, 2>> occluder_uvs_;
    std::vector<fx::Occluders::Cut> occluder_cut_;
    std::shared_ptr<PlaceJob> place_job_;
    std::shared_ptr<SunJob> sun_job_;
    std::vector<Map> maps_;
    std::array<float, 3> sun_baked_{0.0f, 0.0f, 0.0f}; // the direction sun_to was baked for
    std::shared_ptr<const Bounce::Scene> bounce_scene_;
    std::vector<uint32_t> occluder_colors_;
    struct WallJob
    {
        std::atomic<int> state{0};
        fx::TownWallTone wall;
        double ms = 0.0;
    };
    std::shared_ptr<WallJob> wall_job_;
    std::shared_ptr<const fx::VisibilityVolume> wall_volume_; // a copy of the volume the walls are calibrated with
    fx::TownWallTone wall_;
    fx::TownLightTone wall_light_; // the tone of the first relit lightmap
    bool wall_ready_ = false;
    bool sun_present_ = false;
    std::array<float, 3> live_sun_{0.0f, 1.0f, 0.0f};
    float live_sun_luminance_ = 1.0f;
    std::shared_ptr<const fx::BounceField> sun_bounce_;
    fx::Vec3 bake_dir_{0.0f, 1.0f, 0.0f}; // the direction the game's lightmaps were baked with
    float blend_ = 1.0f;                               // sun_from -> sun_to
    unsigned generation_ = 0;                          // sun bakes taken in at this place (log and debug)
    bool debug_ = false;
    bool waiting_reported_ = false; // "no sun yet" said for this place
    std::array<uint32_t, 2> counts_{}, last_counts_{};
    float since_upload_ = 0.0f;
};

} // namespace storm::effects
