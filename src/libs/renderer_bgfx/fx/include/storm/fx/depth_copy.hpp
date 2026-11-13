// The scene depth carried to another depth target, scaled (nearest): with Settings::render_scale < 1 the 3D scene
// draws at the internal size, and the 3D that the game draws after the scene boundary (Renderer::MakePostProcess:
// the ship foam, particles, coronas, the ship pointer) lands on the display-size picture. That picture's depth must
// hold the scene, as on the original renderer, or those draws show through ships and islands.
#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

namespace storm::fx
{

class DepthCopy
{
  public:
    DepthCopy() = default;
    DepthCopy(const DepthCopy &) = delete;
    DepthCopy &operator=(const DepthCopy &) = delete;
    ~DepthCopy();

    // The program for the settings switch (counts one against `budget` when it has to load).
    bool Prepare(int &budget);
    // Writes `source` (a sampleable depth texture of any size) into the depth of `target` (width x height) in
    // `view`: every pixel, depth test always, colour untouched.
    void Submit(bgfx::ViewId view, bgfx::TextureHandle source, bgfx::FrameBufferHandle target, uint16_t width,
                uint16_t height);
    void Shutdown();
    void OnDeviceLost();

  private:
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_depth_ = BGFX_INVALID_HANDLE;
};

} // namespace storm::fx
