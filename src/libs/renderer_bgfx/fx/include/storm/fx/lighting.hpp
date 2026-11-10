// bgfx side of the per-pixel lighting upgrade: owns the u_fxLight uniform that
// shaders/include/fx_lighting.sh declares. The fixed-function emulator compiles
// its shader variants with LightingDefines(settings.lighting_flags) and calls
// Apply() before submitting a lit draw call.
#pragma once

#include "storm/fx/lighting_model.hpp"

#include <bgfx/bgfx.h>

namespace storm::fx
{

class Lighting
{
  public:
    Lighting() = default;
    ~Lighting();
    Lighting(const Lighting &) = delete;
    Lighting &operator=(const Lighting &) = delete;

    // Sets u_fxLight for the next draw call.
    void Apply(const LightingUniforms &uniforms);

    void Shutdown();
    void OnDeviceLost();

  private:
    bgfx::UniformHandle u_light_ = BGFX_INVALID_HANDLE;
};

} // namespace storm::fx
