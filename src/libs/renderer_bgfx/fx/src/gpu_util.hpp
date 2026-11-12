// Internal helpers shared by the bgfx passes of the fx package.
#pragma once

#include <bgfx/bgfx.h>

#include <array>

namespace storm::fx::detail
{

// Program from two embedded shader blobs (names as in shaders/shaders.cmake).
// Returns an invalid handle when a name is unknown.
bgfx::ProgramHandle LoadProgram(const char *vertex, const char *fragment);

// One triangle covering the view. `uv` (0..1 over the view, already oriented for sampling a render target)
// goes to a_texcoord0 unless `corner_data` supplies three values per corner (bottom-left, bottom-right,
// top-left of the view, extrapolated to the triangle). `flip_v` mirrors the image vertically.
void SubmitScreenTriangle(bgfx::ViewId view, bgfx::ProgramHandle program, uint64_t state, bool flip_v = false,
                          const std::array<std::array<float, 3>, 3> *corner_data = nullptr);

template <typename Handle> void Destroy(Handle &handle)
{
    if (bgfx::isValid(handle))
        bgfx::destroy(handle);
    handle = BGFX_INVALID_HANDLE;
}

// Sequential view rendering into `target` (invalid = back buffer) without clearing.
void SetupView(bgfx::ViewId view, const char *name, bgfx::FrameBufferHandle target, uint16_t width, uint16_t height);

// Device capabilities the passes depend on; fx::DetectSupport() reports the same answers.
bool SupportsLut3d();
bool SupportsShadowMap();
bool SupportsAoVolume();
bool SupportsNormalMap();
bool SupportsMsaa();

inline constexpr uint32_t kClampLinear = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
inline constexpr uint32_t kClampPoint = kClampLinear | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;

} // namespace storm::fx::detail
