#include "storm/pbr/renderer.hpp"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "storm_pbr_shaders/includes.inc"

namespace storm::pbr
{
namespace
{
struct Blob
{
    const char *name;
    const uint8_t *data;
    uint32_t size;
};
#define STORM_SHADER(name) {#name, name, sizeof(name)},
constexpr Blob kBlobs[] = {
#include "storm_pbr_shaders/table.inc"
};
#undef STORM_SHADER

bgfx::ShaderHandle LoadShader(const char *name)
{
    for (const Blob &blob : kBlobs)
        if (std::strcmp(blob.name, name) == 0)
            return bgfx::createShader(bgfx::makeRef(blob.data, blob.size));
    return BGFX_INVALID_HANDLE;
}

template <typename Handle> void Destroy(Handle &handle)
{
    if (bgfx::isValid(handle))
        bgfx::destroy(handle);
    handle = BGFX_INVALID_HANDLE;
}

constexpr uint32_t kBrdfSize = 64;
constexpr const char *kFragmentNames[size_t(Variant::Count)] = {"fs_pbr", "fs_pbr_flat", "fs_pbr_emissive", "fs_pbr_layer"};
// Units 0 and 1 use the fixed-function emulator's sampler names (the shim binds them in its texture loop).
constexpr const char *kSamplerNames[kUnitCount] = {
    "s_tex0", "s_tex1", "s_pbrRma", "s_pbrNormal", "s_pbrUnit4", "s_pbrRadiance", "s_pbrBrdf", "s_pbrMask",
    "s_pbrLayerAlbedoG", "s_pbrLayerAlbedoB", "s_pbrLayerRmaR", "s_pbrLayerRmaG", "s_pbrLayerRmaB",
    "s_pbrLayerNormalR", "s_pbrLayerNormalG", "s_pbrLayerNormalB"};

bool Supports(bgfx::TextureFormat::Enum format, uint32_t caps)
{
    return (bgfx::getCaps()->formats[format] & caps) == caps;
}
} // namespace

bool Renderer::Init(std::string &error)
{
    Shutdown();
    if (!Supports(bgfx::TextureFormat::RGBA16F, BGFX_CAPS_FORMAT_TEXTURE_CUBE) ||
        !Supports(bgfx::TextureFormat::RG16F, BGFX_CAPS_FORMAT_TEXTURE_2D))
    {
        error = "the device has no RGBA16F cube or RG16F 2D textures";
        return false;
    }
    if (bgfx::getCaps()->limits.maxTextureSamplers < kUnitCount)
    {
        error = "the device has fewer than 16 texture samplers";
        return false;
    }
    const std::vector<float> table = BrdfLut(kBrdfSize);
    const bgfx::Memory *lut = bgfx::alloc(kBrdfSize * kBrdfSize * 4);
    uint16_t *half = reinterpret_cast<uint16_t *>(lut->data);
    for (size_t i = 0; i < table.size(); ++i)
        half[i] = bx::halfFromFloat(table[i]);
    brdf_ = bgfx::createTexture2D(kBrdfSize, kBrdfSize, false, 1, bgfx::TextureFormat::RG16F,
                                  BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, lut);
    const uint32_t white = 0xffffffffu;
    white_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(&white, 4));

    u_.sh = bgfx::createUniform("u_pbrSh", bgfx::UniformType::Vec4, 9);
    u_.sun = bgfx::createUniform("u_pbrSun", bgfx::UniformType::Vec4);
    u_.sun_color = bgfx::createUniform("u_pbrSunColor", bgfx::UniformType::Vec4);
    u_.ambient = bgfx::createUniform("u_pbrAmbient", bgfx::UniformType::Vec4);
    u_.frame = bgfx::createUniform("u_pbrFrame", bgfx::UniformType::Vec4);
    u_.view_to_world = bgfx::createUniform("u_pbrViewToWorld", bgfx::UniformType::Vec4, 3);
    u_.material = bgfx::createUniform("u_pbrMaterial", bgfx::UniformType::Vec4);
    u_.material2 = bgfx::createUniform("u_pbrMaterial2", bgfx::UniformType::Vec4);
    u_.tiling = bgfx::createUniform("u_pbrTiling", bgfx::UniformType::Vec4);
    u_.point_pos = bgfx::createUniform("u_pbrPointPos", bgfx::UniformType::Vec4, kMaxPointLights);
    u_.point_color = bgfx::createUniform("u_pbrPointColor", bgfx::UniformType::Vec4, kMaxPointLights);
    u_.point_att = bgfx::createUniform("u_pbrPointAtt", bgfx::UniformType::Vec4, kMaxPointLights);
    u_.grade = bgfx::createUniform("u_pbrGrade", bgfx::UniformType::Vec4);
    u_.grade2 = bgfx::createUniform("u_pbrGrade2", bgfx::UniformType::Vec4);
    u_.lift = bgfx::createUniform("u_pbrLift", bgfx::UniformType::Vec4);
    u_.gamma = bgfx::createUniform("u_pbrGamma", bgfx::UniformType::Vec4);
    u_.gain = bgfx::createUniform("u_pbrGain", bgfx::UniformType::Vec4);
    for (size_t i = 0; i < kUnitCount; ++i)
        u_.samplers[i] = bgfx::createUniform(kSamplerNames[i], bgfx::UniformType::Sampler);
    // Programs after the uniforms: the GL backend binds a program's uniforms by name when it creates it.
    // Each program holds its own reference to the shared vertex shader; the creation references are dropped here.
    const bgfx::ShaderHandle vs = LoadShader("vs_pbr");
    bool ok = bgfx::isValid(vs);
    for (size_t i = 0; i < programs_.size() && ok; ++i)
    {
        const bgfx::ShaderHandle fs = LoadShader(kFragmentNames[i]);
        if (bgfx::isValid(fs))
        {
            programs_[i] = bgfx::createProgram(vs, fs, false);
            bgfx::destroy(fs);
        }
        ok = bgfx::isValid(programs_[i]);
        if (!ok)
            error = std::string("cannot create the program vs_pbr + ") + kFragmentNames[i];
    }
    if (bgfx::isValid(vs))
        bgfx::destroy(vs);
    if (!ok)
    {
        Shutdown();
        return false;
    }
    ready_ = true;
    return true;
}

void Renderer::Shutdown()
{
    for (auto &program : programs_)
        Destroy(program);
    Destroy(brdf_);
    Destroy(white_);
    for (bgfx::UniformHandle *handle : {&u_.sh, &u_.sun, &u_.sun_color, &u_.ambient, &u_.frame, &u_.view_to_world, &u_.material,
                                        &u_.material2, &u_.tiling, &u_.point_pos, &u_.point_color, &u_.point_att, &u_.grade,
                                        &u_.grade2, &u_.lift, &u_.gamma, &u_.gain})
        Destroy(*handle);
    for (auto &sampler : u_.samplers)
        Destroy(sampler);
    environment_ = nullptr;
    ready_ = false;
}

bool Renderer::Upload(const Environment &environment, GpuEnvironment &out, std::string &error) const
{
    const Cube &cube = environment.radiance;
    if (cube.size == 0 || cube.mips == 0 || cube.rgba.size() != cube.MipOffset(cube.mips, 0))
    {
        error = "empty or inconsistent radiance cube";
        return false;
    }
    // bgfx expects, for a cube with mips: for each face, every mip of that face.
    const size_t texels = cube.rgba.size() / 4;
    const bgfx::Memory *memory = bgfx::alloc(uint32_t(texels * 8));
    uint16_t *dst = reinterpret_cast<uint16_t *>(memory->data);
    for (uint32_t face = 0; face < 6; ++face)
        for (uint32_t mip = 0; mip < cube.mips; ++mip)
        {
            const uint32_t n = cube.MipSize(mip);
            const float *src = &cube.rgba[cube.MipOffset(mip, face)];
            for (size_t i = 0; i < size_t(n) * n * 4; ++i)
                *dst++ = bx::halfFromFloat(src[i]);
        }
    out.radiance = bgfx::createTextureCube(uint16_t(cube.size), cube.mips > 1, 1, bgfx::TextureFormat::RGBA16F,
                                           BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP, memory);
    if (!bgfx::isValid(out.radiance))
    {
        error = "cannot create the radiance cube texture";
        return false;
    }
    out.max_mip = float(cube.mips - 1);
    out.irradiance = environment.irradiance;
    return true;
}

void Renderer::Release(GpuEnvironment &environment)
{
    Destroy(environment.radiance);
}

void Renderer::SetFrame(const FrameConstants &frame)
{
    environment_ = frame.environment && frame.environment->valid() ? frame.environment : nullptr;
    std::array<float, 36> sh{};
    if (environment_)
        for (size_t k = 0; k < 9; ++k)
            for (size_t c = 0; c < 3; ++c)
                sh[k * 4 + c] = environment_->irradiance.c[k][c];
    bgfx::setUniform(u_.sh, sh.data(), 9);
    const float sun[4] = {frame.sun_view[0], frame.sun_view[1], frame.sun_view[2], frame.sun ? 1.0f : 0.0f};
    bgfx::setUniform(u_.sun, sun);
    const float sun_color[4] = {frame.sun_color[0], frame.sun_color[1], frame.sun_color[2], 0.0f};
    bgfx::setUniform(u_.sun_color, sun_color);
    const float ambient[4] = {frame.ambient[0], frame.ambient[1], frame.ambient[2], environment_ ? environment_->max_mip : 0.0f};
    bgfx::setUniform(u_.ambient, ambient);
    frame_ = {frame.exposure, frame.emission, environment_ ? 1.0f : 0.0f, 0.0f};
    const auto &m = frame.view_to_world;
    const float rows[12] = {m[0], m[1], m[2], 0.0f, m[3], m[4], m[5], 0.0f, m[6], m[7], m[8], 0.0f};
    bgfx::setUniform(u_.view_to_world, rows, 3);
    const Grade &g = frame.grade;
    const float grade[4] = {g.scale[0], g.scale[1], g.scale[2], g.contrast};
    bgfx::setUniform(u_.grade, grade);
    const float grade2[4] = {g.saturation, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(u_.grade2, grade2);
    const float lift[4] = {g.lift[0], g.lift[1], g.lift[2], 0.0f}, gamma[4] = {g.gamma[0], g.gamma[1], g.gamma[2], 0.0f},
                gain[4] = {g.gain[0], g.gain[1], g.gain[2], 0.0f};
    bgfx::setUniform(u_.lift, lift);
    bgfx::setUniform(u_.gamma, gamma);
    bgfx::setUniform(u_.gain, gain);
}


void Renderer::BindDraw(const DrawConstants &draw)
{
    const MaterialDesc &m = *draw.material;
    const float material[4] = {m.roughness, m.metallic, m.emission, draw.has_ao_map ? 1.0f : 0.0f};
    bgfx::setUniform(u_.material, material);
    const float material2[4] = {m.normal_strength, draw.alpha_factor, 0.0f, 0.0f};
    bgfx::setUniform(u_.material2, material2);
    bgfx::setUniform(u_.tiling, m.tiling.data());
    const size_t count = std::min<size_t>(draw.points.size(), kMaxPointLights);
    std::array<float, 4 * kMaxPointLights> pos{}, color{}, att{};
    for (size_t i = 0; i < count; ++i)
    {
        const PointLight &p = draw.points[i];
        pos[i * 4 + 0] = p.position_view[0], pos[i * 4 + 1] = p.position_view[1], pos[i * 4 + 2] = p.position_view[2];
        pos[i * 4 + 3] = p.range;
        color[i * 4 + 0] = p.color[0], color[i * 4 + 1] = p.color[1], color[i * 4 + 2] = p.color[2];
        att[i * 4 + 0] = p.attenuation[0], att[i * 4 + 1] = p.attenuation[1], att[i * 4 + 2] = p.attenuation[2];
    }
    if (count)
    {
        bgfx::setUniform(u_.point_pos, pos.data(), uint16_t(count));
        bgfx::setUniform(u_.point_color, color.data(), uint16_t(count));
        bgfx::setUniform(u_.point_att, att.data(), uint16_t(count));
    }
    frame_[3] = float(count);
    bgfx::setUniform(u_.frame, frame_.data());

    for (uint8_t unit = 2; unit < kUnitCount; ++unit)
        if (bgfx::isValid(draw.textures[unit]))
            bgfx::setTexture(unit, u_.samplers[unit], draw.textures[unit], draw.sampler_flags);
    if (m.needs_white)
        bgfx::setTexture(kUnitRma, u_.samplers[kUnitRma], white_, draw.sampler_flags);
    if (environment_)
        bgfx::setTexture(kUnitRadiance, u_.samplers[kUnitRadiance], environment_->radiance);
    else
        bgfx::setTexture(kUnitRadiance, u_.samplers[kUnitRadiance], BGFX_INVALID_HANDLE);
    bgfx::setTexture(kUnitBrdf, u_.samplers[kUnitBrdf], brdf_);
}

} // namespace storm::pbr
