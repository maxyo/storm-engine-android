// Owner of the graphics effects package (renderer_bgfx/fx) inside the renderer: the requested and the
// effective settings, what the device supports, the per-frame inputs from the engine and the fx objects.
// The shim asks it what to set up (RendererState) and calls it at the scene boundary.
#pragma once

#include "effects/lighting.hpp"
#include "storm/fx/contact_shadows.hpp"
#include "storm/fx/capabilities.hpp"
#include "storm/fx/post_chain.hpp"
#include "storm/fx/self_test.hpp"
#include "storm/fx/settings.hpp"
#include "storm/fx/sky.hpp"
#include "storm/fx/sky_model.hpp"
#include "storm/fx/water.hpp"
#include "storm/render/effects.hpp"

#include <bgfx/bgfx.h>

namespace storm::effects
{

// GPU time of the frame and of the effect passes, from bgfx view stats (option fx_gpu_profile=1, timer queries).
struct PassTimes
{
    double frame_ms = 0.0;
    double post_ms = 0.0;
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
    const fx::RendererState &renderer_state() const { return renderer_state_; }
    bool post_active() const { return device_ready_ && !settings_.PostIsIdentity(); }
    // An effect samples the scene depth (ambient occlusion, the high water's scene capture).
    bool scene_depth_read() const { return settings_.ssao || settings_.water == fx::WaterQuality::High; }
    // True once after the options changed MSAA or anisotropy on a live device (the shim rebuilds the scene target).
    bool take_renderer_state_change()
    {
        const bool changed = renderer_state_changed_;
        renderer_state_changed_ = false;
        return changed;
    }

    void set_weather(const render::EffectsWeather &weather);
    // Per-pixel lighting and alpha to coverage of the fixed-function emulator (effects/lighting.hpp).
    Lighting &lighting() { return lighting_; }

    // Runs the post chain on `scene` into `target` starting at `first_view`; returns the number of views used.
    struct PostTargets
    {
        bgfx::TextureHandle scene_color = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle scene_depth = BGFX_INVALID_HANDLE; // invalid when not sampleable (MSAA)
        uint16_t width = 0, height = 0;
        bgfx::FrameBufferHandle target = BGFX_INVALID_HANDLE;
        bgfx::ViewId first_view = 0;
        // The scene camera: D3D row-vector matrices of the last perspective draw into the scene.
        std::array<float, 16> view{};
        std::array<float, 16> projection{};
    };
    uint16_t submit_post(const PostTargets &targets);
    // Bookkeeping after bgfx::frame(): `scene_frame` = this presented frame had a 3D scene. A frame without one
    // (loading screen) makes the next scene a cut: its grade is applied at once, not blended in.
    void end_frame(bool scene_frame, uint32_t frame);
    // Runs the GPU self-test of the current settings when they changed (fx::SelfTest); before bgfx::frame().
    void submit_self_test();
    // A one-line notice for the player when the self-test switched a stage off; empty when there is none.
    std::string take_notice()
    {
        std::string notice;
        notice.swap(notice_);
        return notice;
    }

    // World passes (world.cpp): drawn into the shim's current game view, in submission order.
    render::EffectsWorld world() const;
    bool sky_fog_color(std::array<float, 3> &rgb);
    void draw_sky(const render::EffectsMatrix &view);
    void draw_water(const render::EffectsWaterMesh &mesh, const render::EffectsMatrix &view);
    void contact_shadow(const std::array<float, 3> &ground, const render::EffectsMatrix &view);

  private:
    void apply();
    // The sky behind the fx sky and water, the scene grade and the hemisphere ambient, from the last weather.
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
    fx::Settings settings_{};
    fx::RendererState renderer_state_{};
    fx::Support support_{};        // what the settings are constrained against
    fx::Support support_device_{}; // what DetectSupport found
    fx::CapsProfile caps_profile_ = fx::CapsProfile::Device;
    bool msaa_broken_ = false; // the self-test found the multisampled scene target broken on this device
    bool post_broken_ = false; // ... or the post chain
    bool device_ready_ = false;
    bool renderer_state_changed_ = false;
    bool depth_read_ = false;
    fx::SelfTest self_test_;
    bool self_test_pending_ = false;
    std::string notice_; // scene_depth_read() when the scene target was last (re)built
    float device_max_anisotropy_ = 0.0f; // GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
    render::EffectsReport report_{};
    render::EffectsWeather weather_{};
    bool weather_this_frame_ = false; // pushed since the last presented frame
    bool weather_last_frame_ = false;
    bool scene_cut_pending_ = true;
    fx::PostChain post_;
    Lighting lighting_;
    fx::Sky sky_;
    fx::Water water_;
    fx::ContactShadows contact_;
    float camera_height_ = 0.0f; // of the last world pass
    bool sky_weather_logged_ = false;
    bool water_buffers_logged_ = false;
    bool water_capture_logged_ = false;
    bool water_env_logged_ = false;
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
    uint64_t profile_draws_ = 0;
};

} // namespace storm::effects
