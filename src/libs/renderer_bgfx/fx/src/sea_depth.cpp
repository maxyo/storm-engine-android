#include "storm/fx/sea_depth.hpp"

#include "gpu_util.hpp"

#include <algorithm>

namespace storm::fx
{
using namespace detail;

SeaDepth::~SeaDepth()
{
    Shutdown();
}

bool SeaDepth::Prepare(int &budget)
{
    if (bgfx::isValid(program_))
        return true;
    if (budget <= 0)
        return false;
    program_ = LoadProgram("vs_fx_screen", "fs_fx_sea_depth");
    --budget;
    return true;
}

void SeaDepth::Capture(bgfx::ViewId view, bgfx::TextureHandle scene_depth, uint16_t width, uint16_t height,
                       float near_plane, float far_plane)
{
    // Per pixel of the scene: a coarser capture gave the pixels next to an object its distance (fringes).
    const uint16_t w = std::max<uint16_t>(width, 1), h = std::max<uint16_t>(height, 1);
    if (!bgfx::isValid(target_) || width_ != w || height_ != h)
    {
        Destroy(target_);
        target_ = bgfx::createFrameBuffer(w, h, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | kClampPoint);
        width_ = w;
        height_ = h;
    }
    if (!bgfx::isValid(program_))
        program_ = LoadProgram("vs_fx_screen", "fs_fx_sea_depth");
    if (!bgfx::isValid(s_depth_))
    {
        s_depth_ = bgfx::createUniform("s_fxSceneDepth", bgfx::UniformType::Sampler);
        u_capture_ = bgfx::createUniform("u_fxCapture", bgfx::UniformType::Vec4);
    }
    SetupView(view, "fx sea depth", target_, w, h);
    const float capture[4] = {near_plane * far_plane, far_plane, far_plane - near_plane, 1.0f / far_plane};
    bgfx::setUniform(u_capture_, capture);
    bgfx::setTexture(1, s_depth_, scene_depth, kClampPoint);
    SubmitScreenTriangle(view, program_, BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
}

bgfx::TextureHandle SeaDepth::Texture() const
{
    if (!bgfx::isValid(target_))
        return BGFX_INVALID_HANDLE;
    return bgfx::getTexture(target_);
}

void SeaDepth::Shutdown()
{
    Destroy(target_);
    Destroy(program_);
    Destroy(s_depth_);
    Destroy(u_capture_);
    width_ = height_ = 0;
}

void SeaDepth::OnDeviceLost()
{
    target_ = BGFX_INVALID_HANDLE;
    program_ = BGFX_INVALID_HANDLE;
    s_depth_ = u_capture_ = BGFX_INVALID_HANDLE;
    width_ = height_ = 0;
}

} // namespace storm::fx
