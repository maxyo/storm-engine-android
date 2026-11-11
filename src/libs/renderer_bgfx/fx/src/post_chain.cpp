#include "storm/fx/post_chain.hpp"

#include "gpu_util.hpp"
#include "storm/fx/color_lut.hpp"
#include "storm/fx/log.hpp"

#include <algorithm>
#include <cmath>

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
    kSsao,
    kComposite, // + 1 bloom, + 2 tone curve, + 4 ambient occlusion
    kFxaa = kComposite + 8,
    kCas,
};

constexpr const char *kFragmentNames[] = {
    "fs_fx_copy",        "fs_fx_bloom_prefilter", "fs_fx_bloom_down",  "fs_fx_bloom_up", "fs_fx_shafts",
    "fs_fx_ssao",
    "fs_fx_composite",   "fs_fx_composite_b",     "fs_fx_composite_t", "fs_fx_composite_bt",
    "fs_fx_composite_a", "fs_fx_composite_ab",    "fs_fx_composite_at", "fs_fx_composite_abt",
    "fs_fx_fxaa",        "fs_fx_cas",
};
static_assert(std::size(kFragmentNames) == kCas + 1);

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
    Destroy(occlusion_.fb);
    Destroy(lut_);
    Destroy(flare_);
    Destroy(u_finish_);
    for (bgfx::UniformHandle *uniform :
         {&s_scene_, &s_bloom_, &s_lut_, &u_bloom_, &u_bloom_range_, &u_shafts_, &u_tone_, &u_lut_, &u_texel_, &s_ao_, &s_depth_,
          &u_ao_})
        Destroy(*uniform);
    lut_strength_ = -1.0f;
    pending_slice_ = -1;
}

void PostChain::OnDeviceLost()
{
    programs_loaded_ = false;
    for (Target &target : bloom_)
        target = {};
    graded_ = {};
    antialiased_ = {};
    occlusion_ = {};
    lut_ = BGFX_INVALID_HANDLE;
    flare_ = BGFX_INVALID_HANDLE;
    u_finish_ = BGFX_INVALID_HANDLE;
    for (bgfx::UniformHandle *uniform :
         {&s_scene_, &s_bloom_, &s_lut_, &u_bloom_, &u_bloom_range_, &u_shafts_, &u_tone_, &u_lut_, &u_texel_, &s_ao_, &s_depth_,
          &u_ao_})
        *uniform = BGFX_INVALID_HANDLE;
    lut_strength_ = -1.0f;
    pending_slice_ = -1;
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
        u_bloom_range_ = bgfx::createUniform("u_fxBloomRange", bgfx::UniformType::Vec4);
        u_lut_ = bgfx::createUniform("u_fxLut", bgfx::UniformType::Vec4);
        u_texel_ = bgfx::createUniform("u_fxTexel", bgfx::UniformType::Vec4);
        s_ao_ = bgfx::createUniform("s_fxAoTex", bgfx::UniformType::Sampler);
        s_depth_ = bgfx::createUniform("s_fxSceneDepth", bgfx::UniformType::Sampler);
        u_ao_ = bgfx::createUniform("u_fxAo", bgfx::UniformType::Vec4, 3);
        u_finish_ = bgfx::createUniform("u_fxFinish", bgfx::UniformType::Vec4);
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

void PostChain::EnsureLut(const Settings &settings, const ColorGrade &scene_grade, bool scene_cut)
{
    const float strength = std::clamp(settings.grade_strength, 0.0f, 1.0f);
    const ColorGrade scene = settings.scene_grading ? scene_grade : ColorGrade{};
    const ColorGrade grade = ColorGrade::Lerp(ColorGrade{}, ColorGrade::Preset(settings.grade), strength);
    constexpr int size = kDefaultLutSize;
    const auto upload = [&](const std::vector<uint8_t> &texels) {
        const bgfx::Memory *memory = bgfx::copy(texels.data(), static_cast<uint32_t>(texels.size()));
        if (!bgfx::isValid(lut_))
            lut_ = bgfx::createTexture3D(size, size, size, false, bgfx::TextureFormat::RGBA8, kClampLinear);
        bgfx::updateTexture3D(lut_, 0, 0, 0, 0, size, size, size, memory);
    };

    // The player's choice changed, a cut, or first use: the whole table now.
    if (!bgfx::isValid(lut_) || lut_grade_ != settings.grade || lut_strength_ != strength || (scene_cut && !(lut_scene_ == scene)))
    {
        upload(BuildLutRgba8(grade, size, scene));
        lut_grade_ = settings.grade;
        lut_strength_ = strength;
        lut_scene_ = scene;
        pending_slice_ = -1;
        return;
    }
    // The scene grade drifts with the hour and the weather. A full table costs ~13 ms on a desktop CPU, so
    // it is rebuilt only once the drift shows in 8-bit output, a few slices per frame, then swapped in.
    if (pending_slice_ < 0 && !(lut_scene_ == scene) && GradeDifference(lut_scene_, scene) > 0.5f / 255.0f)
    {
        pending_scene_ = scene;
        pending_texels_.resize(static_cast<size_t>(size) * size * size * 4);
        pending_slice_ = 0;
    }
    if (pending_slice_ < 0)
        return;
    const int count = std::min(kLutSlicesPerFrame, size - pending_slice_);
    BuildLutSlices(grade, size, pending_scene_, pending_slice_, count, pending_texels_.data());
    pending_slice_ += count;
    if (pending_slice_ == size)
    {
        upload(pending_texels_);
        lut_scene_ = pending_scene_;
        pending_slice_ = -1;
    }
}

uint16_t PostChain::Submit(const Settings &settings, const Frame &frame)
{
    if (!bgfx::isValid(frame.scene_color) || frame.scene_width == 0 || frame.scene_height == 0)
        return 0;
    bgfx::ViewId view = frame.first_view;
    const bool same_size = frame.scene_width == frame.target_width && frame.scene_height == frame.target_height;

    // Everything past a plain copy goes through the 3D grading table.
    const bool usable = SupportsLut3d();
    if (!usable && !settings.PostIsIdentity())
        STORM_FX_ERROR_ONCE(reported_lut_, "post effects are on but the device has no 3D textures (Support::lut_3d): frame copied unprocessed");
    if (settings.PostIsIdentity() || !usable)
    {
        SetupView(view, "fx copy", frame.target, frame.target_width, frame.target_height);
        const bgfx::ProgramHandle copy = Program(kCopy);
        bgfx::setTexture(0, s_scene_, frame.scene_color, same_size ? kClampPoint : kClampLinear);
        SubmitScreenTriangle(view, copy, kWriteColor, frame.flip_v);
        return 1;
    }

    // ---- ambient occlusion (half resolution) ----
    const bool occlusion = settings.ssao && bgfx::isValid(frame.scene_depth);
    if (settings.ssao && !occlusion)
        STORM_FX_ERROR_ONCE(reported_depth_, "ambient occlusion is on but PostChain::Frame::scene_depth is not set");
    if (settings.sun_shafts && !settings.bloom)
        STORM_FX_ERROR_ONCE(reported_shafts_, "sun shafts are on but bloom, which carries them, is off");
    float ao[3][4] = {};
    if (occlusion)
    {
        EnsureTarget(occlusion_, frame.scene_width / 2, frame.scene_height / 2);
        ao[0][0] = frame.near_plane * frame.far_plane;
        ao[0][1] = frame.far_plane;
        ao[0][2] = frame.far_plane - frame.near_plane;
        ao[0][3] = settings.ssao_radius;
        ao[1][0] = 1.0f / frame.projection_scale[0];
        ao[1][1] = 1.0f / frame.projection_scale[1];
        ao[1][2] = settings.ssao_intensity;
        ao[1][3] = 1.0f / frame.far_plane;
        ao[2][0] = 1.0f / frame.scene_width;
        ao[2][1] = 1.0f / frame.scene_height;
        ao[2][2] = 1.0f / occlusion_.width;
        ao[2][3] = 1.0f / occlusion_.height;
        SetupView(view, "fx ambient occlusion", occlusion_.fb, occlusion_.width, occlusion_.height);
        const bgfx::ProgramHandle program = Program(kSsao);
        bgfx::setUniform(u_ao_, ao, 3);
        bgfx::setTexture(4, s_depth_, frame.scene_depth, kClampPoint);
        SubmitScreenTriangle(view++, program, kWriteColor);
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
        const float range[4] = {settings.tonemap ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
        bgfx::setUniform(u_bloom_range_, range);
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
    EnsureLut(settings, frame.scene_grade, frame.scene_cut);
    {
        const bgfx::FrameBufferHandle out = composite_is_final ? frame.target : graded_.fb;
        SetupView(view, "fx composite", out, composite_is_final ? frame.target_width : frame.scene_width,
                  composite_is_final ? frame.target_height : frame.scene_height);
        const bgfx::ProgramHandle composite =
            Program(kComposite + (levels > 0 ? 1 : 0) + (settings.tonemap ? 2 : 0) + (occlusion ? 4 : 0));
        const float tone[4] = {settings.exposure * TonemapExposureBias(), std::clamp(settings.tonemap_strength, 0.0f, 1.0f),
                               settings.bloom_intensity, settings.exposure};
        const float lut[4] = {static_cast<float>(kDefaultLutSize - 1) / kDefaultLutSize, 0.5f / kDefaultLutSize, 0.0f, 0.0f};
        bgfx::setUniform(u_tone_, tone);
        bgfx::setUniform(u_lut_, lut);
        const float finish[4] = {settings.dither ? 1.0f / 255.0f : 0.0f, std::clamp(settings.vignette, 0.0f, 1.0f),
                                 std::clamp(settings.film_grain, 0.0f, 1.0f), static_cast<float>((++frame_counter_ * 37u) % 251u)};
        bgfx::setUniform(u_finish_, finish);
        bgfx::setTexture(0, s_scene_, frame.scene_color, same_size || !composite_is_final ? kClampPoint : kClampLinear);
        if (levels > 0)
            bgfx::setTexture(1, s_bloom_, bgfx::getTexture(bloom_[0].fb), kClampLinear);
        bgfx::setTexture(2, s_lut_, lut_, kClampLinear);
        if (occlusion)
        {
            bgfx::setUniform(u_ao_, ao, 3);
            bgfx::setTexture(3, s_ao_, bgfx::getTexture(occlusion_.fb), kClampLinear);
            bgfx::setTexture(4, s_depth_, frame.scene_depth, kClampPoint);
        }
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
    // ---- lens flare: procedural sprites added on top of the finished image ----
    if (settings.lens_flare && frame.sun_visibility > 0.0f)
    {
        struct Element
        {
            float along;      // position on the line sun -> screen centre -> beyond (0 = at the sun)
            float size;       // half height in clip units
            float stretch;    // width / height
            float shape;      // fs_fx_sprite.sc: 0 glare, 1 ghost, 2 streak
            float r, g, b;
        };
        static constexpr Element kElements[] = {
            {0.0f, 0.62f, 1.0f, 0.0f, 0.42f, 0.36f, 0.26f},  {0.0f, 0.10f, 14.0f, 2.0f, 0.20f, 0.26f, 0.36f},
            {0.42f, 0.050f, 1.0f, 1.0f, 0.05f, 0.09f, 0.05f}, {0.70f, 0.110f, 1.0f, 1.0f, 0.08f, 0.045f, 0.03f},
            {1.22f, 0.080f, 1.0f, 1.0f, 0.04f, 0.05f, 0.09f}, {1.60f, 0.190f, 1.0f, 1.0f, 0.045f, 0.04f, 0.025f},
            {2.05f, 0.045f, 1.0f, 1.0f, 0.06f, 0.07f, 0.04f},
        };
        const float sx = frame.sun_uv[0] * 2.0f - 1.0f;
        const float sy = (1.0f - frame.sun_uv[1] * 2.0f) * (frame.flip_v ? -1.0f : 1.0f);
        // Fades out as the sun leaves the screen.
        const float off_screen = std::max(std::fabs(sx), std::fabs(sy));
        const float strength = settings.lens_flare_intensity * frame.sun_visibility * std::clamp((1.25f - off_screen) / 0.35f, 0.0f, 1.0f);
        struct Vertex
        {
            float pos[3], color[3], uv[3];
        };
        static const bgfx::VertexLayout layout = [] {
            bgfx::VertexLayout l;
            l.begin()
                .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::TexCoord0, 3, bgfx::AttribType::Float)
                .end();
            return l;
        }();
        const uint32_t count = static_cast<uint32_t>(std::size(kElements)) * 6;
        if (strength > 0.0f && bgfx::getAvailTransientVertexBuffer(count, layout) >= count)
        {
            bgfx::TransientVertexBuffer buffer;
            bgfx::allocTransientVertexBuffer(&buffer, count, layout);
            auto *v = reinterpret_cast<Vertex *>(buffer.data);
            const float aspect = static_cast<float>(frame.target_height) / static_cast<float>(frame.target_width);
            for (const Element &e : kElements)
            {
                const float cx = sx * (1.0f - e.along), cy = sy * (1.0f - e.along);
                const float corners[6][2] = {{-1, -1}, {1, -1}, {-1, 1}, {-1, 1}, {1, -1}, {1, 1}};
                for (const auto &corner : corners)
                    *v++ = {{cx + corner[0] * e.size * e.stretch * aspect, cy + corner[1] * e.size, 0.0f},
                            {e.r * frame.sun_color[0] * strength, e.g * frame.sun_color[1] * strength, e.b * frame.sun_color[2] * strength},
                            {corner[0], corner[1], e.shape}};
            }
            if (!bgfx::isValid(flare_))
                flare_ = LoadProgram("vs_fx_sprite", "fs_fx_sprite");
            SetupView(view, "fx lens flare", frame.target, frame.target_width, frame.target_height);
            bgfx::setVertexBuffer(0, &buffer);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ADD);
            bgfx::submit(view++, flare_);
        }
    }
    return static_cast<uint16_t>(view - frame.first_view);
}

} // namespace storm::fx
