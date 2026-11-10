#include "storm/fx/water.hpp"

#include "gpu_util.hpp"
#include "storm/fx/log.hpp"
#include "storm/fx/noise_bake.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{
using namespace detail;

namespace
{
constexpr const char *kVertexNames[] = {"vs_fx_water_mesh", "vs_fx_water_field", "vs_fx_water_gerstner"};
// Fragment variants: low, medium, medium + cube, high, high + cube.
constexpr const char *kFragmentNames[] = {"fs_fx_water_low", "fs_fx_water_med", "fs_fx_water_med_cube", "fs_fx_water_high",
                                          "fs_fx_water_high_cube"};
constexpr float kBaseStep = 0.5f; // metres per cell of the finest grid level

Vec3 Decode(Vec3 c)
{
    return {c[0] * c[0], c[1] * c[1], c[2] * c[2]};
}

int GridCells(WaterQuality quality)
{
    switch (quality)
    {
    case WaterQuality::High:
        return 96;
    case WaterQuality::Medium:
        return 64;
    default:
        return 48;
    }
}
} // namespace

Water::~Water()
{
    Shutdown();
}

const bgfx::VertexLayout &Water::MeshLayout()
{
    static const bgfx::VertexLayout layout = [] {
        bgfx::VertexLayout l;
        l.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        return l;
    }();
    return layout;
}

void Water::Shutdown()
{
    if (resources_)
        for (bgfx::ProgramHandle &program : programs_)
            Destroy(program);
    Destroy(detail_);
    if (resources_)
        for (bgfx::TextureHandle &layer : layers_)
            Destroy(layer);
    Destroy(behind_);
    Destroy(capture_program_);
    Destroy(grid_vertices_);
    Destroy(grid_full_);
    if (grid_cells_)
        for (bgfx::IndexBufferHandle &ring : grid_rings_)
            Destroy(ring);
    for (bgfx::UniformHandle *uniform : {&s_detail_, &s_env_, &s_behind_color_, &s_behind_depth_, &s_layer0_, &s_layer1_, &s_scene_,
                                         &s_scene_depth_, &u_water_, &u_water_vs_, &u_wave_, &u_capture_})
        Destroy(*uniform);
    resources_ = false;
    grid_cells_ = 0;
    behind_valid_ = false;
}

void Water::OnDeviceLost()
{
    detail_ = BGFX_INVALID_HANDLE;
    behind_ = BGFX_INVALID_HANDLE;
    capture_program_ = BGFX_INVALID_HANDLE;
    grid_vertices_ = BGFX_INVALID_HANDLE;
    grid_full_ = BGFX_INVALID_HANDLE;
    for (bgfx::UniformHandle *uniform : {&s_detail_, &s_env_, &s_behind_color_, &s_behind_depth_, &s_layer0_, &s_layer1_, &s_scene_,
                                         &s_scene_depth_, &u_water_, &u_water_vs_, &u_wave_, &u_capture_})
        *uniform = BGFX_INVALID_HANDLE;
    resources_ = false;
    grid_cells_ = 0;
    behind_valid_ = false;
}

void Water::EnsureResources()
{
    if (resources_)
        return;
    resources_ = true;
    programs_.fill(BGFX_INVALID_HANDLE);
    layers_.fill(BGFX_INVALID_HANDLE);
    grid_rings_.fill(BGFX_INVALID_HANDLE);
    s_detail_ = bgfx::createUniform("s_fxDetail", bgfx::UniformType::Sampler);
    s_env_ = bgfx::createUniform("s_fxEnvCube", bgfx::UniformType::Sampler);
    s_behind_color_ = bgfx::createUniform("s_fxBehindColor", bgfx::UniformType::Sampler);
    s_behind_depth_ = bgfx::createUniform("s_fxBehindDepth", bgfx::UniformType::Sampler);
    s_layer0_ = bgfx::createUniform("s_fxLayer0", bgfx::UniformType::Sampler);
    s_layer1_ = bgfx::createUniform("s_fxLayer1", bgfx::UniformType::Sampler);
    s_scene_ = bgfx::createUniform("s_fxScene", bgfx::UniformType::Sampler);
    s_scene_depth_ = bgfx::createUniform("s_fxSceneDepth", bgfx::UniformType::Sampler);
    u_water_ = bgfx::createUniform("u_fxWater", bgfx::UniformType::Vec4, 9);
    u_water_vs_ = bgfx::createUniform("u_fxWaterVs", bgfx::UniformType::Vec4);
    u_wave_ = bgfx::createUniform("u_fxWave", bgfx::UniformType::Vec4, 13);
    u_capture_ = bgfx::createUniform("u_fxCapture", bgfx::UniformType::Vec4);
    const BakedTexture detail = BakeWaterDetail();
    detail_ = bgfx::createTexture2D(static_cast<uint16_t>(detail.size), static_cast<uint16_t>(detail.size), true, 1,
                                    bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_MIN_ANISOTROPIC,
                                    bgfx::copy(detail.rgba.data(), static_cast<uint32_t>(detail.rgba.size())));
}

void Water::EnsureGrid(WaterQuality quality)
{
    const int cells = GridCells(quality);
    if (grid_cells_ == cells)
        return;
    Destroy(grid_vertices_);
    Destroy(grid_full_);
    for (bgfx::IndexBufferHandle &ring : grid_rings_)
        Destroy(ring);
    const WaterGridMesh mesh = BuildWaterGrid(cells);
    bgfx::VertexLayout layout;
    layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    grid_vertices_ = bgfx::createVertexBuffer(
        bgfx::copy(mesh.vertices.data(), static_cast<uint32_t>(mesh.vertices.size() * sizeof(float))), layout);
    const auto indices = [](const std::vector<uint16_t> &v) {
        return bgfx::createIndexBuffer(bgfx::copy(v.data(), static_cast<uint32_t>(v.size() * sizeof(uint16_t))));
    };
    grid_full_ = indices(mesh.full);
    for (int i = 0; i < 4; ++i)
        grid_rings_[i] = indices(mesh.rings[i]);
    grid_cells_ = cells;
}

void Water::CaptureScene(bgfx::ViewId view, bgfx::TextureHandle scene_color, bgfx::TextureHandle scene_depth, uint16_t width,
                         uint16_t height, float near_plane, float far_plane)
{
    if (!SupportsSceneSnapshot())
    {
        STORM_FX_ERROR_ONCE(reported_snapshot_, "Water::CaptureScene(): the device has no multiple render targets (Support::scene_snapshot)");
        return;
    }
    EnsureResources();
    const uint16_t w = std::max<uint16_t>(width / 2, 1), h = std::max<uint16_t>(height / 2, 1);
    if (!bgfx::isValid(behind_) || behind_width_ != w || behind_height_ != h)
    {
        Destroy(behind_);
        const bgfx::TextureHandle attachments[2] = {
            bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | kClampLinear),
            bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | kClampPoint)};
        behind_ = bgfx::createFrameBuffer(2, attachments, true);
        behind_width_ = w;
        behind_height_ = h;
    }
    if (!bgfx::isValid(capture_program_))
        capture_program_ = LoadProgram("vs_fx_screen", "fs_fx_water_capture");
    SetupView(view, "fx water capture", behind_, w, h);
    const float capture[4] = {near_plane * far_plane, far_plane, far_plane - near_plane, 1.0f / far_plane};
    bgfx::setUniform(u_capture_, capture);
    bgfx::setTexture(0, s_scene_, scene_color, kClampLinear);
    bgfx::setTexture(1, s_scene_depth_, scene_depth, kClampPoint);
    SubmitScreenTriangle(view, capture_program_, BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    behind_valid_ = true;
}

bgfx::ProgramHandle Water::Bind(Source source, const Settings &settings, const WaterFrame &frame, const WaterShading &shading, Sky &sky)
{
    EnsureResources();
    const WaterQuality quality = settings.water;
    if (quality == WaterQuality::Legacy)
    {
        STORM_FX_ERROR_ONCE(reported_legacy_, "Water called with WaterQuality::Legacy: the legacy sea must be drawn instead");
        return BGFX_INVALID_HANDLE;
    }
    if (quality != WaterQuality::High)
    {
        behind_valid_ = false; // a later switch back to High must not show a stale snapshot
    }
    else if (!behind_valid_)
    {
        STORM_FX_ERROR_ONCE(reported_snapshot_, "WaterQuality::High needs Water::CaptureScene() before the sea is drawn");
        return BGFX_INVALID_HANDLE;
    }
    const bool cube = bgfx::isValid(frame.env_cube) && quality != WaterQuality::Low;
    const int fragment = quality == WaterQuality::Low ? 0 : (quality == WaterQuality::Medium ? 1 : 3) + (cube ? 1 : 0);
    bgfx::ProgramHandle &program = programs_[static_cast<int>(source) * 5 + fragment];
    if (!bgfx::isValid(program))
        program = LoadProgram(kVertexNames[static_cast<int>(source)], kFragmentNames[fragment]);

    const SkyState &state = sky.State();
    const Vec3 deep = Decode(shading.water_color);
    const Vec3 tint = Decode(shading.sky_color);
    // Light scattered back out of a crest: the body colour pulled towards turquoise.
    const Vec3 scatter{0.4f * deep[0] + 0.03f, 0.5f * deep[1] + 0.22f, 0.5f * deep[2] + 0.20f};
    const float transparency = std::clamp(shading.transparency, 0.0f, 1.0f);
    const float uniforms[9][4] = {
        {frame.camera[0], frame.camera[1], frame.camera[2], frame.time * shading.bump_speed},
        {state.sun_light[0], state.sun_light[1], state.sun_light[2], 260.0f},
        {deep[0], deep[1], deep[2], std::clamp(shading.reflection, 0.0f, 1.0f) * 1.25f},
        {scatter[0], scatter[1], scatter[2], 0.02f + 0.05f * std::clamp(shading.fresnel, 0.0f, 1.0f)},
        {0.5f, 1.0f / 350.0f, 1.8f * shading.sun_road, std::max(frame.crest_height, 0.5f)},
        {shading.foam_v, shading.foam ? shading.foam_k : 0.0f, 0.35f * shading.foam_uv, 0.6f},
        {tint[0], tint[1], tint[2], frame.far_plane},
        {0.035f, 0.42f - 0.36f * transparency, settings.water_refraction ? 1.0f : 0.0f, 0.0f},
        {state.dome[0], state.dome[1], state.dome[2], 0.0f},
    };
    bgfx::setUniform(u_water_, uniforms, 9);
    bgfx::setTexture(0, s_detail_, detail_);
    if (cube)
        bgfx::setTexture(1, s_env_, frame.env_cube, kClampLinear);
    uint64_t state_flags = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
    if (quality == WaterQuality::High)
    {
        bgfx::setTexture(2, s_behind_color_, bgfx::getTexture(behind_, 0), kClampLinear);
        bgfx::setTexture(3, s_behind_depth_, bgfx::getTexture(behind_, 1), kClampPoint);
        // Soft shore line: the surface fades out where it meets the ground.
        state_flags |= BGFX_STATE_BLEND_ALPHA;
    }
    sky.Bind(settings);
    bgfx::setState(state_flags);
    return program;
}

bgfx::ProgramHandle Water::PrepareMesh(const Settings &settings, const WaterFrame &frame, const WaterShading &shading, Sky &sky,
                                       std::array<float, 2> world_offset)
{
    const bgfx::ProgramHandle program = Bind(Source::Mesh, settings, frame, shading, sky);
    if (!bgfx::isValid(program))
        return program;
    const float vs[4] = {world_offset[0], world_offset[1], 0.0f, 0.0f};
    bgfx::setUniform(u_water_vs_, vs);
    return program;
}

void Water::UpdateField(const EngineWaveField &field)
{
    if (!SupportsWaveField())
    {
        STORM_FX_ERROR_ONCE(reported_field_, "Water::UpdateField(): no RGBA16F vertex textures on this device (Support::wave_field)");
        return;
    }
    EnsureResources();
    for (int layer = 0; layer < 2; ++layer)
    {
        if (!bgfx::isValid(layers_[layer]))
            layers_[layer] = bgfx::createTexture2D(kLayerSize, kLayerSize, true, 1, bgfx::TextureFormat::RGBA16F, BGFX_SAMPLER_NONE);
        const std::vector<uint16_t> texels = field.PackLayerHalf(layer);
        size_t offset = 0;
        for (int mip = 0, size = kLayerSize; mip < EngineWaveField::kMipCount; ++mip, size /= 2)
        {
            const size_t count = static_cast<size_t>(size) * size * 4;
            bgfx::updateTexture2D(layers_[layer], 0, static_cast<uint8_t>(mip), 0, 0, static_cast<uint16_t>(size),
                                  static_cast<uint16_t>(size),
                                  bgfx::copy(texels.data() + offset, static_cast<uint32_t>(count * sizeof(uint16_t))));
            offset += count;
        }
    }
}

template <typename SetWaveUniforms>
bool Water::SubmitGrid(Source source, const Settings &settings, const WaterFrame &frame, const WaterShading &shading, Sky &sky,
                       SetWaveUniforms &&set_wave_uniforms)
{
    EnsureResources();
    EnsureGrid(settings.water);
    // Enough levels to reach the far plane.
    const float reach = std::max(frame.far_plane, 100.0f);
    int level_count = 1;
    while (level_count < 12 && kBaseStep * static_cast<float>(grid_cells_ / 2) * std::exp2(static_cast<float>(level_count - 1)) < reach)
        ++level_count;
    const std::vector<WaterGridLevel> levels = PlaceWaterGrid(frame.camera[0], frame.camera[2], kBaseStep, level_count);
    for (const WaterGridLevel &level : levels)
    {
        const bgfx::ProgramHandle program = Bind(source, settings, frame, shading, sky);
        if (!bgfx::isValid(program))
            return false;
        const float vs[4] = {level.origin[0], level.origin[1], level.step, 0.0f};
        bgfx::setUniform(u_water_vs_, vs);
        set_wave_uniforms(level);
        bgfx::setVertexBuffer(0, grid_vertices_);
        bgfx::setIndexBuffer(level.ring < 0 ? grid_full_ : grid_rings_[level.ring]);
        bgfx::submit(frame.view, program);
    }
    return true;
}

bool Water::SubmitField(const Settings &settings, const WaterFrame &frame, const WaterShading &shading, Sky &sky,
                        const EngineSeaState &sea)
{
    if (!bgfx::isValid(layers_[0]))
    {
        STORM_FX_ERROR_ONCE(reported_field_, "Water::SubmitField() needs Water::UpdateField() first");
        return false;
    }
    return SubmitGrid(Source::Field, settings, frame, shading, sky, [&](const WaterGridLevel &level) {
        float wave[4][4] = {};
        for (int i = 0; i < 2; ++i)
        {
            const EngineSeaLayer &l = sea.layers[i];
            wave[i][0] = l.scale;
            wave[i][1] = l.amp;
            wave[i][2] = l.move[0];
            wave[i][3] = l.move[1];
            // Mip whose texel is about one grid cell, for this level and for the next coarser one.
            wave[3][i] = std::max(std::log2(level.step * l.scale), 0.0f);
            wave[3][2 + i] = std::max(std::log2(2.0f * level.step * l.scale), 0.0f);
        }
        wave[2][0] = 3.0f * sea.pos_shift;
        wave[2][1] = sea.bump_scale;
        bgfx::setUniform(u_wave_, wave, 4);
        bgfx::setTexture(8, s_layer0_, layers_[0], BGFX_SAMPLER_NONE);
        bgfx::setTexture(9, s_layer1_, layers_[1], BGFX_SAMPLER_NONE);
    });
}

bool Water::SubmitGerstner(const Settings &settings, const WaterFrame &frame, const WaterShading &shading, Sky &sky,
                           const GerstnerSet &waves, float bump_scale)
{
    float wave[13][4] = {};
    for (int i = 0; i < kGerstnerWaveCount; ++i)
    {
        const GerstnerWave &w = waves.waves[i];
        wave[2 * i][0] = w.dir[0];
        wave[2 * i][1] = w.dir[1];
        wave[2 * i][2] = w.wave_number;
        wave[2 * i][3] = w.amplitude;
        wave[2 * i + 1][0] = w.horizontal;
        // Keep the argument small: float precision of sin() degrades after hours of play.
        wave[2 * i + 1][1] = std::fmod(w.phase - w.omega * frame.time, 6.28318530718f);
    }
    wave[12][0] = waves.mean_level;
    wave[12][1] = bump_scale;
    return SubmitGrid(Source::Gerstner, settings, frame, shading, sky,
               [&](const WaterGridLevel &) { bgfx::setUniform(u_wave_, wave, 13); });
}

} // namespace storm::fx
