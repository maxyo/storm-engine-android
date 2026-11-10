#include "storm/fx/post_chain.hpp"

#include "gpu_util.hpp"
#include "storm/fx/color_lut.hpp"

#include <algorithm>

namespace storm::fx
{
namespace
{
using namespace detail;

enum ProgramIndex
{
    kCopy,
    kBloomPrefilter,
    kBloomDown,
    kBloomUp,
    kShafts,
    kComposite, // + 1 bloom, + 2 tone curve
    kFxaa = kComposite + 4,
    kCas,
};

constexpr const char *kFragmentNames[] = {
    "fs_fx_copy",        "fs_fx_bloom_prefilter", "fs_fx_bloom_down",  "fs_fx_bloom_up", "fs_fx_shafts",
    "fs_fx_composite",   "fs_fx_composite_b",     "fs_fx_composite_t", "fs_fx_composite_bt",
    "fs_fx_fxaa",        "fs_fx_cas",
};
static_assert(std::size(kFragmentNames) == 11);

constexpr uint64_t kWriteColor = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
} // namespace

PostChain::~PostChain()
{
    Shutdown();
}

void PostChain::Shutdown()
{
    if (programs_loaded_)
        for (bgfx::ProgramHandle &program : programs_)
            Destroy(program);
    programs_loaded_ = false;
    for (Target &target : bloom_)
        Destroy(target.fb);
    Destroy(graded_.fb);
    Destroy(antialiased_.fb);
    Destroy(lut_);
    for (bgfx::UniformHandle *uniform : {&s_scene_, &s_bloom_, &s_lut_, &u_bloom_, &u_shafts_, &u_tone_, &u_lut_, &u_texel_})
        Destroy(*uniform);
    lut_strength_ = -1.0f;
}

void PostChain::OnDeviceLost()
{
    programs_loaded_ = false;
    for (Target &target : bloom_)
        target = {};
    graded_ = {};
    antialiased_ = {};
    lut_ = BGFX_INVALID_HANDLE;
    for (bgfx::UniformHandle *uniform : {&s_scene_, &s_bloom_, &s_lut_, &u_bloom_, &u_shafts_, &u_tone_, &u_lut_, &u_texel_})
        *uniform = BGFX_INVALID_HANDLE;
    lut_strength_ = -1.0f;
}

bgfx::ProgramHandle PostChain::Program(int index)
{
    if (!programs_loaded_)
    {
        programs_.fill(BGFX_INVALID_HANDLE);
        programs_loaded_ = true;
        s_scene_ = bgfx::createUniform("s_fxScene", bgfx::UniformType::Sampler);
        s_bloom_ = bgfx::createUniform("s_fxBloomTex", bgfx::UniformType::Sampler);
        s_lut_ = bgfx::createUniform("s_fxLut", bgfx::UniformType::Sampler);
        u_bloom_ = bgfx::createUniform("u_fxBloom", bgfx::UniformType::Vec4);
        u_shafts_ = bgfx::createUniform("u_fxShafts", bgfx::UniformType::Vec4);
        u_tone_ = bgfx::createUniform("u_fxTone", bgfx::UniformType::Vec4);
        u_lut_ = bgfx::createUniform("u_fxLut", bgfx::UniformType::Vec4);
        u_texel_ = bgfx::createUniform("u_fxTexel", bgfx::UniformType::Vec4);
    }
    if (!bgfx::isValid(programs_[index]))
        programs_[index] = LoadProgram("vs_fx_screen", kFragmentNames[index]);
    return programs_[index];
}

void PostChain::EnsureTarget(Target &target, uint16_t width, uint16_t height)
{
    width = std::max<uint16_t>(width, 1);
    height = std::max<uint16_t>(height, 1);
    if (bgfx::isValid(target.fb) && target.width == width && target.height == height)
        return;
    Destroy(target.fb);
    target.fb = bgfx::createFrameBuffer(width, height, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | kClampLinear);
    target.width = width;
    target.height = height;
}

void PostChain::EnsureLut(const Settings &settings)
{
    const float strength = std::clamp(settings.grade_strength, 0.0f, 1.0f);
    if (bgfx::isValid(lut_) && lut_grade_ == settings.grade && lut_strength_ == strength)
        return;
    const ColorGrade grade = ColorGrade::Lerp(ColorGrade{}, ColorGrade::Preset(settings.grade), strength);
    const std::vector<uint8_t> texels = BuildLutRgba8(grade, kDefaultLutSize);
    const bgfx::Memory *memory = bgfx::copy(texels.data(), static_cast<uint32_t>(texels.size()));
    if (!bgfx::isValid(lut_))
        lut_ = bgfx::createTexture3D(kDefaultLutSize, kDefaultLutSize, kDefaultLutSize, false, bgfx::TextureFormat::RGBA8, kClampLinear);
    bgfx::updateTexture3D(lut_, 0, 0, 0, 0, kDefaultLutSize, kDefaultLutSize, kDefaultLutSize, memory);
    lut_grade_ = settings.grade;
    lut_strength_ = strength;
}

uint16_t PostChain::Submit(const Settings &settings, const Frame &frame)
{
    if (!bgfx::isValid(frame.scene_color) || frame.scene_width == 0 || frame.scene_height == 0)
        return 0;
    bgfx::ViewId view = frame.first_view;
    const bool same_size = frame.scene_width == frame.target_width && frame.scene_height == frame.target_height;

    if (settings.PostIsIdentity())
    {
        SetupView(view, "fx copy", frame.target, frame.target_width, frame.target_height);
        const bgfx::ProgramHandle copy = Program(kCopy);
        bgfx::setTexture(0, s_scene_, frame.scene_color, same_size ? kClampPoint : kClampLinear);
        SubmitScreenTriangle(view, copy, kWriteColor, frame.flip_v);
        return 1;
    }

    // ---- bloom ----
    int levels = 0;
    if (settings.bloom)
    {
        const uint16_t divisor = settings.bloom_downscale >= 4 ? 4 : 2;
        uint16_t width = std::max<uint16_t>(frame.scene_width / divisor, 1);
        uint16_t height = std::max<uint16_t>(frame.scene_height / divisor, 1);
        // Sun shafts are injected one level below the top, so they need two levels.
        levels = std::clamp<int>(settings.bloom_levels, settings.sun_shafts ? 2 : 1, kMaxBloomLevels);
        while (levels > 1 && (std::min(width, height) >> levels) < 4)
            --levels;
        for (int i = 0; i <= levels; ++i)
            EnsureTarget(bloom_[i], width >> i, height >> i);

        SetupView(view, "fx bloom bright", bloom_[0].fb, bloom_[0].width, bloom_[0].height);
        const float threshold = std::clamp(settings.bloom_threshold, 0.0f, 0.98f);
        const float prefilter[4] = {1.0f / frame.scene_width, 1.0f / frame.scene_height, threshold,
                                    std::clamp(settings.bloom_knee, 0.0f, 1.0f) * threshold};
        const bgfx::ProgramHandle bright = Program(kBloomPrefilter);
        bgfx::setUniform(u_bloom_, prefilter);
        bgfx::setTexture(0, s_scene_, frame.scene_color, kClampLinear);
        SubmitScreenTriangle(view++, bright, kWriteColor);

        for (int i = 1; i <= levels; ++i)
        {
            SetupView(view, "fx bloom down", bloom_[i].fb, bloom_[i].width, bloom_[i].height);
            const float half[4] = {0.5f / bloom_[i - 1].width, 0.5f / bloom_[i - 1].height, 0.0f, 0.0f};
            const bgfx::ProgramHandle down = Program(kBloomDown);
            bgfx::setUniform(u_bloom_, half);
            bgfx::setTexture(0, s_scene_, bgfx::getTexture(bloom_[i - 1].fb), kClampLinear);
            SubmitScreenTriangle(view++, down, kWriteColor);
        }
        for (int i = levels; i >= 1; --i)
        {
            SetupView(view, "fx bloom up", bloom_[i - 1].fb, bloom_[i - 1].width, bloom_[i - 1].height);
            const float half[4] = {0.5f / bloom_[i].width, 0.5f / bloom_[i].height, 0.0f, 0.0f};
            const bgfx::ProgramHandle up = Program(kBloomUp);
            bgfx::setUniform(u_bloom_, half);
            bgfx::setTexture(0, s_scene_, bgfx::getTexture(bloom_[i].fb), kClampLinear);
            SubmitScreenTriangle(view++, up, kWriteColor);

            if (i == 2 && settings.sun_shafts && frame.sun_visibility > 0.0f)
            {
                // Level 2 now holds a wide blur of the bright pass: smear it towards the sun into level 1.
                SetupView(view, "fx sun shafts", bloom_[1].fb, bloom_[1].width, bloom_[1].height);
                const float sun_v = bgfx::getCaps()->originBottomLeft ? 1.0f - frame.sun_uv[1] : frame.sun_uv[1];
                const float shafts[4] = {frame.sun_uv[0], sun_v, settings.sun_shafts_intensity * frame.sun_visibility, 0.85f};
                const bgfx::ProgramHandle program = Program(kShafts);
                bgfx::setUniform(u_shafts_, shafts);
                bgfx::setTexture(0, s_scene_, bgfx::getTexture(bloom_[2].fb), kClampLinear);
                SubmitScreenTriangle(view++, program, kWriteColor | BGFX_STATE_BLEND_ADD);
            }
        }
    }

    // ---- composite: bloom, tone curve, grade ----
    const bool then_fxaa = settings.fxaa;
    const bool then_sharpen = settings.sharpen;
    const bool composite_is_final = !then_fxaa && !then_sharpen;
    if (!composite_is_final)
        EnsureTarget(graded_, frame.scene_width, frame.scene_height);
    EnsureLut(settings);
    {
        const bgfx::FrameBufferHandle out = composite_is_final ? frame.target : graded_.fb;
        SetupView(view, "fx composite", out, composite_is_final ? frame.target_width : frame.scene_width,
                  composite_is_final ? frame.target_height : frame.scene_height);
        const bgfx::ProgramHandle composite = Program(kComposite + (levels > 0 ? 1 : 0) + (settings.tonemap ? 2 : 0));
        const float tone[4] = {settings.exposure * TonemapExposureBias(), std::clamp(settings.tonemap_strength, 0.0f, 1.0f),
                               settings.bloom_intensity, settings.exposure};
        const float lut[4] = {static_cast<float>(kDefaultLutSize - 1) / kDefaultLutSize, 0.5f / kDefaultLutSize, 0.0f, 0.0f};
        bgfx::setUniform(u_tone_, tone);
        bgfx::setUniform(u_lut_, lut);
        bgfx::setTexture(0, s_scene_, frame.scene_color, same_size || !composite_is_final ? kClampPoint : kClampLinear);
        if (levels > 0)
            bgfx::setTexture(1, s_bloom_, bgfx::getTexture(bloom_[0].fb), kClampLinear);
        bgfx::setTexture(2, s_lut_, lut_, kClampLinear);
        // The intermediate target keeps luma in alpha for FXAA; a final target must stay opaque.
        SubmitScreenTriangle(view++, composite, composite_is_final ? BGFX_STATE_WRITE_RGB : kWriteColor,
                             composite_is_final && frame.flip_v);
    }

    // ---- anti-aliasing ----
    bgfx::TextureHandle current = composite_is_final ? frame.scene_color : bgfx::getTexture(graded_.fb);
    const float texel[4] = {1.0f / frame.scene_width, 1.0f / frame.scene_height, std::clamp(settings.sharpness, 0.0f, 1.0f), 0.0f};
    if (then_fxaa)
    {
        if (then_sharpen)
            EnsureTarget(antialiased_, frame.scene_width, frame.scene_height);
        const bgfx::FrameBufferHandle out = then_sharpen ? antialiased_.fb : frame.target;
        SetupView(view, "fx fxaa", out, then_sharpen ? frame.scene_width : frame.target_width,
                  then_sharpen ? frame.scene_height : frame.target_height);
        const bgfx::ProgramHandle fxaa = Program(kFxaa);
        bgfx::setUniform(u_texel_, texel);
        bgfx::setTexture(0, s_scene_, current, kClampLinear);
        SubmitScreenTriangle(view++, fxaa, kWriteColor, !then_sharpen && frame.flip_v);
        if (then_sharpen)
            current = bgfx::getTexture(antialiased_.fb);
    }

    // ---- sharpen / upscale ----
    if (then_sharpen)
    {
        SetupView(view, "fx sharpen", frame.target, frame.target_width, frame.target_height);
        const bgfx::ProgramHandle cas = Program(kCas);
        bgfx::setUniform(u_texel_, texel);
        bgfx::setTexture(0, s_scene_, current, kClampLinear);
        SubmitScreenTriangle(view++, cas, kWriteColor, frame.flip_v);
    }
    return static_cast<uint16_t>(view - frame.first_view);
}

} // namespace storm::fx
