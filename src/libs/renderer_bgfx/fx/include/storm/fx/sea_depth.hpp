// The scene's view distance behind the legacy sea, for its soft edges (Settings::sea_soft_edges): the sea is
// depth-tested against the depth attachment, so it cannot sample it; Capture() packs the distance into an RGB8
// texture of the scene size (one texel per pixel, point-sampled) in a view of its own, before the sea draws.
#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

namespace storm::fx
{

class SeaDepth
{
  public:
    SeaDepth() = default;
    SeaDepth(const SeaDepth &) = delete;
    SeaDepth &operator=(const SeaDepth &) = delete;
    ~SeaDepth();

    // Programs for the settings switch (counts one against `budget` when it has to load).
    bool Prepare(int &budget);
    // `scene_depth`: the depth texture of the target the sea is drawn into (its resolve texture when multisampled).
    void Capture(bgfx::ViewId view, bgfx::TextureHandle scene_depth, uint16_t width, uint16_t height, float near_plane,
                 float far_plane);
    // The packed distances of the last Capture(); its RGB decode to distance / far_plane.
    bgfx::TextureHandle Texture() const;
    void Shutdown();
    void OnDeviceLost();

  private:
    bgfx::FrameBufferHandle target_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_depth_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_capture_ = BGFX_INVALID_HANDLE;
    uint16_t width_ = 0, height_ = 0;
};

} // namespace storm::fx
