// GPU side of the PBR package: programs, the BRDF table, environment cubes and the uniforms of a PBR draw.
// bgfx only, no engine types: the shim (renderer_bgfx/src/pbr) and the host render check use it the same way.
//
// Per frame: SetFrame() once before the first PBR draw (the u_pbr* uniforms belong to these programs only and keep
// their values between draws). The view must be ViewMode::Sequential (as the shim's game views are): in a sorted view
// a later PBR draw could run before the draw the frame uniforms were recorded with. Per draw: BindDraw() after the caller has set the vertex / index buffers, the
// transform and the fixed-function uniforms (u_ffProj, u_ffVs, u_ffClipPlane, u_ffFs) and bound units 0 (albedo)
// and 1 (ambient occlusion, or White()), then bgfx::submit with Program(variant).
#pragma once

#include "storm/pbr/environment.hpp"
#include "storm/pbr/material.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <span>
#include <string>

namespace storm::pbr
{

struct GpuEnvironment
{
    bgfx::TextureHandle radiance = BGFX_INVALID_HANDLE; // RGBA16F cube, mips = roughness
    float max_mip = 0.0f;
    Sh9 irradiance;
    bool valid() const { return bgfx::isValid(radiance); }
};

struct FrameConstants
{
    const GpuEnvironment *environment = nullptr; // null: no image-based light this frame (reported by the caller)
    Vec3 sun_view{0.0f, 0.0f, 1.0f};             // view space, towards the sun
    bool sun = false;
    Vec3 sun_color{};
    Vec3 ambient{};
    float exposure = 1.0f;
    float emission = 0.0f;
    std::array<float, 9> view_to_world{1, 0, 0, 0, 1, 0, 0, 0, 1}; // row-major rotation
    Grade grade;
};

struct PointLight
{
    Vec3 position_view{};
    float range = 0.0f;
    Vec3 color{};
    std::array<float, 3> attenuation{1.0f, 0.0f, 0.0f};
};
inline constexpr int kMaxPointLights = 4;

struct DrawConstants
{
    static std::array<bgfx::TextureHandle, kUnitCount> Invalid()
    {
        std::array<bgfx::TextureHandle, kUnitCount> all;
        all.fill(BGFX_INVALID_HANDLE);
        return all;
    }
    const MaterialDesc *material = nullptr;
    // Textures of the material's units (index = unit); invalid entries are not bound. Units 0 and 1 are the caller's.
    std::array<bgfx::TextureHandle, kUnitCount> textures = Invalid();
    uint32_t sampler_flags = UINT32_MAX; // for the material textures (UINT32_MAX: as created)
    bool has_ao_map = false;             // unit 1 holds the model's ambient occlusion map
    float alpha_factor = 1.0f;
    std::span<const PointLight> points;  // at most kMaxPointLights are used
};

class Renderer
{
  public:
    // After bgfx::init. False with `error` when the device lacks what the programs need (then nothing is drawn
    // with PBR this session; the caller reports it).
    bool Init(std::string &error);
    void Shutdown();
    bool ready() const { return ready_; }

    // `skinned`: GEOS::AVERTEX0 vertices with the bone palette at unit 13 (not with Variant::Layer, whose textures use it).
    bgfx::ProgramHandle Program(Variant variant, bool skinned = false) const
    {
        return (skinned ? skinned_programs_ : programs_)[size_t(variant)];
    }
    bgfx::TextureHandle White() const { return white_; }

    bool Upload(const Environment &environment, GpuEnvironment &out, std::string &error) const;
    static void Release(GpuEnvironment &environment);

    void SetFrame(const FrameConstants &frame);
    void BindDraw(const DrawConstants &draw);

  private:
    bool ready_ = false;
    using Programs = std::array<bgfx::ProgramHandle, size_t(Variant::Count)>;
    static Programs NoPrograms()
    {
        Programs all;
        all.fill(BGFX_INVALID_HANDLE);
        return all;
    }
    Programs programs_ = NoPrograms();
    Programs skinned_programs_ = NoPrograms(); // Variant::Layer stays invalid
    bgfx::TextureHandle brdf_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle white_ = BGFX_INVALID_HANDLE;
    const GpuEnvironment *environment_ = nullptr;
    struct Uniforms
    {
        static constexpr bgfx::UniformHandle kInvalid = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle sh = kInvalid, sun = kInvalid, sun_color = kInvalid, ambient = kInvalid, frame = kInvalid,
                            view_to_world = kInvalid, material = kInvalid, material2 = kInvalid, tiling = kInvalid,
                            point_pos = kInvalid, point_color = kInvalid, point_att = kInvalid, grade = kInvalid,
                            grade2 = kInvalid, lift = kInvalid, gamma = kInvalid, gain = kInvalid;
        std::array<bgfx::UniformHandle, kUnitCount> samplers = [] {
            std::array<bgfx::UniformHandle, kUnitCount> all;
            all.fill(BGFX_INVALID_HANDLE);
            return all;
        }();
    } u_;
    float point_count_ = 0.0f;
    std::array<float, 4> frame_{};
};

} // namespace storm::pbr
