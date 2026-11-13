#include "storm/fx/depth_copy.hpp"

#include "gpu_util.hpp"

namespace storm::fx
{
using namespace detail;

DepthCopy::~DepthCopy()
{
    Shutdown();
}

bool DepthCopy::Prepare(int &budget)
{
    if (bgfx::isValid(program_))
        return true;
    if (budget <= 0)
        return false;
    program_ = LoadProgram("vs_fx_screen", "fs_fx_depth_copy");
    --budget;
    return true;
}

void DepthCopy::Submit(bgfx::ViewId view, bgfx::TextureHandle source, bgfx::FrameBufferHandle target, uint16_t width,
                       uint16_t height)
{
    if (!bgfx::isValid(program_))
        program_ = LoadProgram("vs_fx_screen", "fs_fx_depth_copy");
    if (!bgfx::isValid(s_depth_))
        s_depth_ = bgfx::createUniform("s_fxSceneDepth", bgfx::UniformType::Sampler);
    SetupView(view, "fx depth copy", target, width, height);
    bgfx::setTexture(1, s_depth_, source, kClampPoint);
    SubmitScreenTriangle(view, program_, BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_ALWAYS);
}

void DepthCopy::Shutdown()
{
    Destroy(program_);
    Destroy(s_depth_);
}

void DepthCopy::OnDeviceLost()
{
    program_ = BGFX_INVALID_HANDLE;
    s_depth_ = BGFX_INVALID_HANDLE;
}

} // namespace storm::fx
