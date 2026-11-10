// Owner of the graphics effects package (renderer_bgfx/fx) inside the renderer: the requested and the
// effective settings, what the device supports, the per-frame inputs from the engine and the fx objects.
// The shim asks it what to set up (RendererState) and calls it at the scene boundary.
#pragma once

#include "storm/fx/post_chain.hpp"
#include "storm/fx/settings.hpp"
#include "storm/fx/sky_model.hpp"
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
    // True once after the options changed MSAA or anisotropy on a live device (the shim rebuilds the scene target).
    bool take_renderer_state_change()
    {
        const bool changed = renderer_state_changed_;
        renderer_state_changed_ = false;
        return changed;
    }

    void set_weather(const render::EffectsWeather &weather);

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
    void end_frame(bool scene_frame);

  private:
    void apply();

    int preset_ = 0;
    std::vector<render::EffectsOption> options_;
    fx::Settings requested_{};
    fx::Settings settings_{};
    fx::RendererState renderer_state_{};
    fx::Support support_{};
    bool device_ready_ = false;
    bool renderer_state_changed_ = false;
    render::EffectsReport report_{};
    render::EffectsWeather weather_{};
    bool weather_this_frame_ = false; // pushed since the last presented frame
    bool weather_last_frame_ = false;
    bool scene_cut_pending_ = true;
    fx::PostChain post_;
    fx::WeatherInput grade_weather_{}; // the sky state behind the scene grade, rebuilt when the weather moves
    fx::SkyState grade_sky_{};
    bool grade_sky_valid_ = false;
    bgfx::ViewId last_post_first_ = 0;
    uint16_t last_post_views_ = 0;
    PassTimes times_{};
    static constexpr int kProfileFrames = 30;
    bool profile_ = false;
    bool profile_unsupported_logged_ = false;
    int profile_count_ = 0;
    double profile_sum_frame_ = 0.0, profile_sum_post_ = 0.0, profile_sum_cpu_ = 0.0;
    uint64_t profile_draws_ = 0;
};

} // namespace storm::effects
