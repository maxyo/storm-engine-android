// The atmosphere state of the weather (fx::SkyState: the place-and-hour grade, the hemisphere ambient, the aerial
// perspective) and the uniforms of the emulator's FX_AERIAL / FX_WIND variants. The sky itself is the game's.
#pragma once

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

    // Once per frame. Rebuilds the sky state only when the weather moved enough (about 2 ms of CPU, typically once
    // every few game minutes). `time_seconds` drives the wind sway.
    // `camera_height`: the camera's y above the sea, for the haze layer.
    void Update(const WeatherInput &weather, float time_seconds, float camera_height);
    const SkyState &State() const
    {
        return state_;
    }

    // FX_AERIAL / FX_WIND variants of the fixed-function emulator: set u_fxAerial (needs the scene's
    // world -> view matrix) and u_fxWind for the next draw call.
    void BindAerial(const std::array<float, 16> &view);
    void BindWind();

    void Shutdown();
    void OnDeviceLost();

  private:
    void EnsureResources();

    WeatherInput weather_{};
    SkyState state_{};
    bool state_valid_ = false;
    float time_ = 0.0f;
    float camera_height_ = 0.0f;
    bool resources_ = false;
    bgfx::UniformHandle u_aerial_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_wind_ = BGFX_INVALID_HANDLE;
};

} // namespace storm::fx
