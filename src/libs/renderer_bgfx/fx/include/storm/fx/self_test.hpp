// GPU self-test of the effect path a preset uses: a small known picture goes through a scene target like the
// game's (same MSAA, resolved when left) and the post chain of the settings; a few pixels are read back and
// compared with what the CPU expects. Catches driver problems no caps bit reports (an MSAA resolve or a 3D
// texture that does not work as specified) on any phone: the caller switches the failing stage off and reports it.
#pragma once

#include "storm/fx/post_chain.hpp"
#include "storm/fx/settings.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <string>

namespace storm::fx
{

class SelfTest
{
  public:
    // Views used by Submit(): one for the test scene, the post chain's (PostChain::kMaxViews), one for read-backs.
    static constexpr uint16_t kViews = PostChain::kMaxViews + 2;

    enum class Stage : uint8_t
    {
        None,       // passed
        SceneTarget, // the multisampled scene target and its resolve
        PostChain,  // the post chain (grading table, bloom, composite, ...)
    };
    struct Result
    {
        bool done = false;
        Stage failed = Stage::None;
        std::string detail; // what was expected and read, for the log
    };

    SelfTest() = default;
    ~SelfTest();
    SelfTest(const SelfTest &) = delete;
    SelfTest &operator=(const SelfTest &) = delete;

    // Submits the test for `settings` into [first_view, first_view + kViews), with the programs of `chain` (the
    // game's post chain, PostChain::Prepare'd for `settings`: the test links nothing of its own). Returns false
    // when a test is still running (one at a time).
    bool Submit(const Settings &settings, bgfx::ViewId first_view, const PostChain &chain);
    // After each bgfx::frame(): `frame` = its return value. Returns the result once the read-backs arrived.
    Result Poll(uint32_t frame);
    bool Running() const { return pending_frame_ != 0; }

    void Shutdown();
    void OnDeviceLost();

  private:
    static constexpr uint16_t kSize = 64;
    void Release();

    PostChain post_;
    Settings settings_{};
    bgfx::TextureHandle pattern_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle scene_ = BGFX_INVALID_HANDLE;
    uint64_t scene_flags_ = 0;
    uint16_t scene_size_ = 0; // kSize * render_scale: the upscale (FSR 1 EASU) is tested too
    bgfx::FrameBufferHandle output_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle readback_scene_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle readback_output_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_scene_ = BGFX_INVALID_HANDLE;
    std::array<uint8_t, kSize * kSize * 4> scene_pixels_{};
    std::array<uint8_t, kSize * kSize * 4> output_pixels_{};
    uint32_t pending_frame_ = 0;
};

} // namespace storm::fx
