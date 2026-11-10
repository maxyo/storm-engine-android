// Analytic sky pass and the atmosphere state shared with the water (and with
// any shader that includes fx_atmosphere.sh).
#pragma once

#include "storm/fx/settings.hpp"
#include "storm/fx/sky_model.hpp"

#include <bgfx/bgfx.h>

namespace storm::fx
{

class Sky
{
  public:
    Sky() = default;
    ~Sky();
    Sky(const Sky &) = delete;
    Sky &operator=(const Sky &) = delete;

    // Once per frame, before Bind()/Submit(). Rebuilds the sky panorama only when the weather moved enough
    // (about 2 ms of CPU, typically once every few game minutes). `time_seconds` drives the cloud drift.
    void Update(const WeatherInput &weather, float time_seconds);
    const SkyState &State() const
    {
        return state_;
    }

    // Sets the fx_atmosphere.sh uniforms and textures (sampler stages 5 and 6) for the next draw call.
    void Bind(const Settings &settings);

    // Draws the sky on the far plane of `view` (depth test LEQUAL, no depth write): submit it after the
    // opaque geometry to save fill rate, or first. `view_matrix` / `projection` are the 4x4 matrices given
    // to bgfx::setViewTransform. The game's own sun glow, flares, stars and moon are drawn on top as before.
    void Submit(bgfx::ViewId view, const Settings &settings, const float *view_matrix, const float *projection);

    void Shutdown();
    void OnDeviceLost();

  private:
    void EnsureResources();

    WeatherInput weather_{};
    SkyState state_{};
    bool state_valid_ = false;
    bool lut_dirty_ = true;
    float time_ = 0.0f;
    bool resources_ = false;
    bgfx::TextureHandle lut_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle clouds_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_clouds_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_lut_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_clouds_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_sky_ = BGFX_INVALID_HANDLE;
};

} // namespace storm::fx
