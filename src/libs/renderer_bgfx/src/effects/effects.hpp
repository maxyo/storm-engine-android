// Owner of the graphics effects package (renderer_bgfx/fx) inside the renderer: the requested and the
// effective settings, what the device supports, the per-frame inputs from the engine and the fx objects.
// The shim asks it what to set up (RendererState) and calls it at the scene boundary.
#pragma once

#include "effects/ao_volume.hpp"
#include "effects/lighting.hpp"
#include "storm/fx/contact_shadows.hpp"
#include "storm/fx/capabilities.hpp"
#include "storm/fx/post_chain.hpp"
#include "storm/fx/self_test.hpp"
#include "storm/fx/settings.hpp"
#include "storm/fx/sky.hpp"
#include "storm/fx/sky_model.hpp"
#include "storm/fx/sea_depth.hpp"
#include "storm/render/effects.hpp"
#include "storm/render/program_cache.hpp"

#include <bgfx/bgfx.h>

#include <chrono>
#include <vector>

namespace storm::effects
{

// GPU time of the frame and of the effect passes, from bgfx view stats (option fx_gpu_profile=1, timer queries).
struct PassTimes
{
    double frame_ms = 0.0;
    double post_ms = 0.0;
    double smaa_ms = 0.0; // the three SMAA passes
    double easu_ms = 0.0; // FSR 1 upscale
    double rcas_ms = 0.0; // FSR 1 sharpening
    uint16_t post_views = 0;
};

class Host
{
  public:
    static Host &instance();

    // After bgfx::init: detects what the device supports and constrains the request (logged once).
    void on_device_created();
    void on_device_lost();
    void shutdown();

    render::EffectsReport configure(int preset, const std::vector<render::EffectsOption> &options);
    const render::EffectsReport &report() const { return report_; }

    const fx::Settings &settings() const { return settings_; }
    const fx::Support &support() const { return support_; } // what settings() were constrained against
    const fx::RendererState &renderer_state() const { return renderer_state_; }
    // The post chain runs: an effect is on, or the game's underwater wobble (on every preset) is.
    bool post_active() const { return device_ready_ && (!settings_.PostIsIdentity() || wobble_.on); }
    void set_wobble(const render::EffectsWobble &wobble) { wobble_ = wobble; }
    // An effect samples the scene depth (ambient occlusion, the sea's soft edges).
    bool scene_depth_read() const { return settings_.ssao || settings_.sea_soft_edges; }
    // True once after a settings switch made MSAA, anisotropy or the scene depth read change (the shim rebuilds
    // what of the scene target differs, between frames).
    bool take_renderer_state_change()
    {
        const bool changed = renderer_state_changed_;
        renderer_state_changed_ = false;
        return changed;
    }

    void set_weather(const render::EffectsWeather &weather);
    void set_sun_glare(float glare, float ghosts)
    {
        sun_glare_ = glare;
        sun_ghosts_ = ghosts;
    }
    // Per-pixel lighting and alpha to coverage of the fixed-function emulator (effects/lighting.hpp).
    Lighting &lighting() { return lighting_; }
    AoVolumes &ao_volumes() { return ao_; }
    // The interior's AO volume for the draws of this frame (null outdoors, before it is ready, or when off).
    const AoVolumes::Bound *ao_volume() const { return ao_bound_; }

    // Runs the post chain on `scene` into `target` starting at `first_view`; returns the number of views used.
    struct PostTargets
    {
        bgfx::TextureHandle scene_color = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle scene_depth = BGFX_INVALID_HANDLE; // invalid when not sampleable (MSAA)
        uint16_t width = 0, height = 0;               // the scene: the internal size with Settings::render_height
        bgfx::FrameBufferHandle target = BGFX_INVALID_HANDLE;
        uint16_t target_width = 0, target_height = 0; // the display-size picture the interfaces draw on
        bgfx::ViewId first_view = 0;
        // The scene camera: D3D row-vector matrices of the last perspective draw into the scene.
        std::array<float, 16> view{};
        std::array<float, 16> projection{};
    };
    uint16_t submit_post(const PostTargets &targets);
    // Bookkeeping after bgfx::frame(): `scene_frame` = this presented frame had a 3D scene. A frame without one
    // (loading screen) makes the next scene a cut: its grade is applied at once, not blended in.
    void end_frame(bool scene_frame, uint32_t frame);
    // Before bgfx::frame(), while a settings change is pending: prepares the programs of the new settings
    // (kProgramsPerFrame per frame), then runs their GPU self-test (fx::SelfTest, once per settings set), then
    // makes them the active ones. Until then the current settings keep drawing: a change never shows a frame
    // that waits for shader links or a picture of half-built targets.
    void advance_switch();
    // A requested settings change is not active yet (advance_switch).
    bool switching() const { return switching_; }
    // A one-line notice for the player when the self-test switched a stage off; empty when there is none.
    std::string take_notice()
    {
        std::string notice;
        notice.swap(notice_);
        return notice;
    }

    // World passes (world.cpp): drawn into the shim's current game view, in submission order.
    render::EffectsWorld world() const;
    // Before each draw with the game's Sea2 pixel shader (shaders/ports/fs_port_sea2.sc): sets its u_seaFx and,
    // for the soft edges, captures the scene distance behind the sea once per frame and target and binds it.
    void prepare_sea(shim::Device &device);
    void contact_shadow(const std::array<float, 3> &ground, const render::EffectsMatrix &view);

  private:
    static constexpr int kProgramsPerFrame = 1;
    static constexpr int kCachedProgramsPerFrame = 4; // binaries from the program cache load without a link
    void apply();
    // Makes `settings` the ones that draw; the shim follows a renderer-state change at the next Present.
    void activate(const fx::Settings &settings, const fx::Support &support);
    static bool needs_self_test(const fx::Settings &settings);
    // The sky behind the fx sky, the scene grade and the hemisphere ambient, from the last weather.
    const fx::SkyState &sky_state();
    fx::WeatherInput weather_input() const;
    bool has_weather() const { return weather_this_frame_ || weather_last_frame_; }
    // The current render target's view with the current D3D camera as its bgfx view transform.
    struct WorldPass
    {
        bgfx::ViewId view = 0;
        std::array<float, 16> view_mtx{}, projection{}; // projection: backend clip space
        fx::Vec3 camera{};
        float near_plane = 0.1f, far_plane = 4000.0f;
    };
    bool begin_world_pass(WorldPass &pass, const render::EffectsMatrix &view, bool fresh = false);
    void update_sky(float camera_height);

    int preset_ = 0;
    std::vector<render::EffectsOption> options_;
    fx::Settings requested_{};
    fx::Settings settings_{}; // the active ones: what draws
    fx::Settings target_{};   // the request constrained to the device: active once prepared (advance_switch)
    bool switching_ = false;
    std::chrono::steady_clock::time_point switch_started_{};
    uint32_t switch_frames_ = 0;
    uint32_t switch_programs_ = 0;
    render::ProgramCacheStats switch_cache_{}; // at the previous advance_switch
    fx::RendererState renderer_state_{};
    // What the active settings (settings_) were constrained against, and what the requested ones (target_) are:
    // a settings switch makes both active together (activate), so what draws is always checked against the support
    // it was made for. A narrower support (fx_caps_profile, a failed self-test) waits for its settings like them.
    fx::Support support_{};
    fx::Support target_support_{};
    fx::Support support_device_{}; // what DetectSupport found
    fx::CapsProfile caps_profile_ = fx::CapsProfile::Device;
    bool msaa_broken_ = false; // the self-test found the multisampled scene target broken on this device
    bool post_broken_ = false; // ... or the post chain
    bool device_ready_ = false;
    bool renderer_state_changed_ = false;
    bool depth_read_ = false;
    fx::SelfTest self_test_;
    fx::Settings tested_{};               // the settings of the running self-test
    std::vector<fx::Settings> verified_; // settings whose self-test passed on this device
    std::string notice_; // scene_depth_read() when the scene target was last (re)built
    float device_max_anisotropy_ = 0.0f; // GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
    render::EffectsReport report_{};
    render::EffectsWeather weather_{};
    render::EffectsWobble wobble_{}; // set by the renderer every frame (Render.SeaEffect)
    bool weather_this_frame_ = false; // pushed since the last presented frame
    float sun_glare_ = 0.0f;          // SetEffectsSunGlare since the last presented frame
    float sun_ghosts_ = 0.0f;
    bool weather_last_frame_ = false;
    bool scene_cut_pending_ = true;
    fx::PostChain post_;
    Lighting lighting_;
    fx::Sky sky_;
    fx::SeaDepth sea_depth_;
    fx::ContactShadows contact_;
    AoVolumes ao_;
    const AoVolumes::Bound *ao_bound_ = nullptr;
    float camera_height_ = 0.0f; // of the last world pass
    bgfx::UniformHandle u_sea_fx_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_sea_depth_ = BGFX_INVALID_HANDLE;
    // The capture of this frame: the target it was taken of and the view the sea continues in after it.
    uint32_t sea_depth_frame_ = UINT32_MAX;
    const void *sea_depth_target_ = nullptr;
    bool sea_depth_logged_ = false;
    // The fx view transform last set on a shim view this frame: another camera in the same view needs a new view.
    bgfx::ViewId transform_view_ = UINT16_MAX;
    std::array<float, 32> transform_{};
    bgfx::ViewId last_post_first_ = 0;
    uint16_t last_post_views_ = 0;
    PassTimes times_{};
    static constexpr int kProfileFrames = 30;
    bool profile_ = false;
    int profile_count_ = 0;
    int profile_gpu_frames_ = 0;
    double profile_sum_frame_ = 0.0, profile_sum_post_ = 0.0, profile_sum_cpu_ = 0.0;
    double profile_sum_smaa_ = 0.0, profile_sum_easu_ = 0.0, profile_sum_rcas_ = 0.0;
    std::array<uint16_t, 4> last_post_size_{}; // scene width, height, target width, height of the last post chain
    uint64_t profile_draws_ = 0;
};

} // namespace storm::effects

