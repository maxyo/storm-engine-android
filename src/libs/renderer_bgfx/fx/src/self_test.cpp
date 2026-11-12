#include "storm/fx/self_test.hpp"

#include "gpu_util.hpp"
#include "storm/fx/color_lut.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace storm::fx
{

namespace
{
using namespace detail;

// Four flat colours in vertical bands, below the bloom threshold, so a working chain only grades them. Their blue
// spans the whole range: the 3D grading table is sampled in slices far apart (a table with only its first slice
// uploaded, as a GLES upload path once produced, cannot pass).
constexpr int kColors = 4;
constexpr uint8_t kPattern[kColors][4] = {{77, 140, 12, 255}, {153, 102, 90, 255}, {60, 120, 170, 255}, {140, 110, 235, 255}};
constexpr uint16_t kProbeX[kColors] = {8, 24, 40, 56};
constexpr uint16_t kProbeY = 32;

std::array<int, 3> Expected(const Settings &settings, const uint8_t *rgb)
{
    const ColorGrade grade = ColorGrade::Lerp(ColorGrade{}, ColorGrade::Preset(settings.grade),
                                              std::clamp(settings.grade_strength, 0.0f, 1.0f));
    const auto out = grade.Apply({rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f});
    return {static_cast<int>(std::lround(out[0] * 255.0f)), static_cast<int>(std::lround(out[1] * 255.0f)),
            static_cast<int>(std::lround(out[2] * 255.0f))};
}

// Largest channel difference at the probes of a `size`-pixel square (the probes scale with it); `text` gets
// "expected/read" for the log.
int Compare(const uint8_t *pixels, uint16_t size, const std::array<int, 3> (&expected)[kColors], std::string &text)
{
    int worst = 0;
    for (int i = 0; i < kColors; ++i)
    {
        const size_t x = kProbeX[i] * size / 64, y = kProbeY * size / 64;
        const uint8_t *p = pixels + (y * size + x) * 4;
        char line[96];
        snprintf(line, sizeof(line), "%s(%d,%d,%d) read (%u,%u,%u)", i ? "; " : "", expected[i][0], expected[i][1],
                 expected[i][2], p[0], p[1], p[2]);
        text += line;
        for (int c = 0; c < 3; ++c)
            worst = std::max(worst, std::abs(expected[i][c] - static_cast<int>(p[c])));
    }
    return worst;
}
} // namespace

SelfTest::~SelfTest()
{
    Shutdown();
}

void SelfTest::Release()
{
    Destroy(pattern_);
    Destroy(scene_);
    Destroy(output_);
    Destroy(readback_scene_);
    Destroy(readback_output_);
    Destroy(s_scene_);
    pending_frame_ = 0;
}

void SelfTest::Shutdown()
{
    post_.Shutdown();
    Release();
}

void SelfTest::OnDeviceLost()
{
    post_.OnDeviceLost();
    pattern_ = BGFX_INVALID_HANDLE;
    scene_ = output_ = BGFX_INVALID_HANDLE;
    readback_scene_ = readback_output_ = BGFX_INVALID_HANDLE;
    s_scene_ = BGFX_INVALID_HANDLE;
    pending_frame_ = 0;
}

bool SelfTest::Submit(const Settings &settings, bgfx::ViewId first_view, const PostChain &chain)
{
    if (pending_frame_ != 0)
        return false;
    settings_ = settings;
    post_.SharePrograms(chain);
    const RendererState state = RendererStateFor(settings);
    // The game's scene target: RGBA8 colour and D24S8 depth, multisampled like it (src/shim/device.cpp).
    const uint64_t msaa = state.msaa_samples >= 4 ? BGFX_TEXTURE_RT_MSAA_X4 : state.msaa_samples == 2 ? BGFX_TEXTURE_RT_MSAA_X2 : 0;
    // The scene at the internal size of the render scale, like the game's (the post chain then upscales it).
    const auto size = static_cast<uint16_t>(std::lround(kSize * std::clamp(state.render_scale, 0.5f, 1.0f)));
    if (!bgfx::isValid(scene_) || scene_flags_ != msaa || scene_size_ != size)
    {
        Destroy(scene_);
        Destroy(readback_scene_);
        const bgfx::TextureHandle attachments[2] = {
            bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | msaa | kClampPoint),
            bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT | msaa | kClampPoint)};
        scene_ = bgfx::createFrameBuffer(2, attachments, true);
        scene_flags_ = msaa;
        scene_size_ = size;
    }
    if (!bgfx::isValid(pattern_))
        pattern_ = bgfx::createTexture2D(kColors, 1, false, 1, bgfx::TextureFormat::RGBA8, kClampPoint,
                                         bgfx::copy(kPattern, sizeof(kPattern)));
    if (!bgfx::isValid(output_))
        output_ = bgfx::createFrameBuffer(kSize, kSize, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | kClampPoint);
    const uint64_t readback = BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK | kClampPoint;
    if (!bgfx::isValid(readback_scene_))
        readback_scene_ = bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::RGBA8, readback);
    if (!bgfx::isValid(readback_output_))
        readback_output_ = bgfx::createTexture2D(kSize, kSize, false, 1, bgfx::TextureFormat::RGBA8, readback);
    if (!bgfx::isValid(s_scene_))
        s_scene_ = bgfx::createUniform("s_fxScene", bgfx::UniformType::Sampler);

    bgfx::ViewId view = first_view;
    SetupView(view, "fx self-test scene", scene_, size, size);
    bgfx::setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
    bgfx::setTexture(0, s_scene_, pattern_, kClampPoint);
    SubmitScreenTriangle(view++, post_.CopyProgram(), BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);

    PostChain::Frame frame;
    frame.scene_color = bgfx::getTexture(scene_, 0);
    frame.scene_depth = bgfx::getTexture(scene_, 1);
    frame.scene_width = frame.scene_height = size;
    frame.target = output_;
    frame.target_width = frame.target_height = kSize;
    frame.first_view = view;
    frame.scene_cut = true;
    view += post_.Submit(settings, frame);

    bgfx::setViewName(view, "fx self-test read-back");
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    bgfx::touch(view);
    const auto copy = [view](bgfx::TextureHandle to, bgfx::TextureHandle from, uint8_t *pixels) {
        bgfx::TextureRegion source, destination;
        source.init(from);
        destination.init(to);
        bgfx::blit(view, destination, source);
        bgfx::TextureRegion region;
        region.init(to);
        return bgfx::read(region, pixels);
    };
    const uint32_t a = copy(readback_scene_, bgfx::getTexture(scene_, 0), scene_pixels_.data());
    const uint32_t b = copy(readback_output_, bgfx::getTexture(output_, 0), output_pixels_.data());
    pending_frame_ = std::max(a, b);
    return true;
}

SelfTest::Result SelfTest::Poll(uint32_t frame)
{
    Result result;
    if (pending_frame_ == 0 || frame < pending_frame_)
        return result;
    pending_frame_ = 0;
    result.done = true;

    // The resolved scene must hold the pattern exactly (point-sampled copy, no blending).
    std::array<int, 3> pattern[kColors];
    for (int i = 0; i < kColors; ++i)
        pattern[i] = {kPattern[i][0], kPattern[i][1], kPattern[i][2]};
    std::string scene_text;
    if (Compare(scene_pixels_.data(), scene_size_, pattern, scene_text) > 2)
    {
        result.failed = Stage::SceneTarget;
        result.detail = "scene target (MSAA " + std::to_string(RendererStateFor(settings_).msaa_samples) +
                        "x, resolve): expected " + scene_text;
        return result;
    }
    // The chain's output: the pattern through the colour grade (no bloom below the threshold, dither ±0.5/255).
    // Looks a CPU mirror does not model (tone curve, scene grade, occlusion) get a wide tolerance: the test
    // then only tells a working chain from garbage.
    std::array<int, 3> expected[kColors];
    for (int i = 0; i < kColors; ++i)
        expected[i] = Expected(settings_, kPattern[i]);
    const bool approximate = settings_.tonemap || settings_.scene_grading || settings_.ssao;
    std::string output_text;
    if (Compare(output_pixels_.data(), kSize, expected, output_text) > (approximate ? 48 : 8))
    {
        result.failed = Stage::PostChain;
        result.detail = "post chain: expected " + output_text;
    }
    return result;
}

} // namespace storm::fx
