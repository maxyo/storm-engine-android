// Post-processing chain: bloom, sun shafts, tone curve, colour grade, FXAA,
// sharpening / upscale. Reads the game's offscreen colour target and writes
// the final image into a frame buffer or the back buffer.
//
//   scene ─┬─────────────────────────────► composite ─► [FXAA] ─► [CAS] ─► target
//          └► bright pass ► down… ► up… ──┘ (bloom, + sun shafts)
//
// With Settings::PostIsIdentity() the chain is a single copy. Passes that are
// switched off cost nothing. Everything runs in gamma-encoded RGBA8 targets.
#pragma once

#include "storm/fx/settings.hpp"

#include <bgfx/bgfx.h>

#include <array>

namespace storm::fx
{

class PostChain
{
  public:
    // Views consumed at most by one Submit(): a contiguous range the caller reserves.
    static constexpr uint16_t kMaxViews = 18;

    struct Frame
    {
        bgfx::TextureHandle scene_color = BGFX_INVALID_HANDLE; // RGBA8, gamma encoded, the game's offscreen target
        uint16_t scene_width = 0;                              // its size: the dynamic-resolution render size
        uint16_t scene_height = 0;
        bgfx::FrameBufferHandle target = BGFX_INVALID_HANDLE;  // invalid = back buffer
        uint16_t target_width = 0;
        uint16_t target_height = 0;
        bgfx::ViewId first_view = 0;
        bool flip_v = false; // mirror the final image vertically (when the caller's own blit would)
        // Sun shafts only: sun position in scene uv (0..1, origin top-left) and 0..1 visibility
        // (0 when the sun is behind the camera or below the horizon).
        std::array<float, 2> sun_uv{0.5f, 0.5f};
        float sun_visibility = 0.0f;
    };

    PostChain() = default;
    ~PostChain();
    PostChain(const PostChain &) = delete;
    PostChain &operator=(const PostChain &) = delete;

    // Submits the passes for this frame and returns the number of views used,
    // starting at frame.first_view. Call once per frame after the scene views.
    uint16_t Submit(const Settings &settings, const Frame &frame);

    // Releases every GPU object (they are re-created by the next Submit).
    void Shutdown();
    // The context is gone: forget the handles without destroying them.
    void OnDeviceLost();

  private:
    struct Target
    {
        bgfx::FrameBufferHandle fb = BGFX_INVALID_HANDLE;
        uint16_t width = 0;
        uint16_t height = 0;
    };
    void EnsureTarget(Target &target, uint16_t width, uint16_t height);
    void EnsureLut(const Settings &settings);
    bgfx::ProgramHandle Program(int index);

    static constexpr int kMaxBloomLevels = 6;
    static constexpr int kProgramCount = 11;
    std::array<bgfx::ProgramHandle, kProgramCount> programs_{};
    bool programs_loaded_ = false;
    std::array<Target, kMaxBloomLevels + 1> bloom_{};
    Target graded_{};      // composite output when another pass follows
    Target antialiased_{}; // FXAA output when sharpening follows
    bgfx::TextureHandle lut_ = BGFX_INVALID_HANDLE;
    GradePreset lut_grade_ = GradePreset::Original;
    float lut_strength_ = -1.0f;
    bgfx::UniformHandle s_scene_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_bloom_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_lut_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bloom_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_shafts_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_tone_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lut_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_texel_ = BGFX_INVALID_HANDLE;
};

} // namespace storm::fx
