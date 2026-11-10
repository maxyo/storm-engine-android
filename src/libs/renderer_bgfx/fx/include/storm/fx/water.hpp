// Sea surface pass. Geometry comes from one of three sources:
//
//   PrepareMesh()     the vertices the SEA entity already displaces on the CPU (SEA::SeaVertex buffers):
//                     a drop-in replacement for the Sea2 technique, same geometry as the legacy sea;
//   SubmitField()     a camera-centred grid displaced on the GPU from the SEA entity's two height-field
//                     layers: identical surface to SEA::WaveXZ without the per-vertex CPU work;
//   SubmitGerstner()  the same grid displaced by Gerstner waves derived from the Sea2 parameters, for
//                     views without a SEA entity.
//
// Shading is shared: the legacy Sea2 model per pixel with the weather's Sea2 palette (Fresnel reflection of the
// engine's environment cube, or of the analytic sky without one; water colour; the scene behind the surface
// showing through by Sea2.Transparency), two scrolling detail normal layers, sun road, crest foam. Medium
// blends over the scene behind like Sea2 (colour + behind * alpha); WaterQuality::High reads that scene from a
// snapshot instead: the real water depth, refraction, shore foam and soft shore lines.
#pragma once

#include "storm/fx/settings.hpp"
#include "storm/fx/sky.hpp"
#include "storm/fx/water_grid.hpp"
#include "storm/fx/wave_model.hpp"

#include <bgfx/bgfx.h>

#include <array>

namespace storm::fx
{

// Look of the water: the SEA entity's attributes (Sea2.*), colours as the game's 0..1 values.
struct WaterShading
{
    Vec3 water_color{0.04f, 0.22f, 0.39f}; // Sea2.WaterColor
    Vec3 sky_color{1.0f, 1.0f, 1.0f};      // Sea2.SkyColor: tint of the reflection
    float reflection = 0.7f;               // Sea2.Reflection
    float transparency = 0.6f;             // Sea2.Transparency
    float attenuation = 0.2f;              // Sea2.Attenuation
    float fresnel = 0.5f;                  // Sea2.Frenel
    float bump_speed = 1.0f;               // Sea2.BumpSpeed
    bool foam = false;                     // Sea2.FoamEnable
    float foam_v = 3.0f;                   // Sea2.FoamV: height where crest foam starts
    float foam_k = 0.3f;                   // Sea2.FoamK: foam gain per metre above FoamV
    float foam_uv = 1.0f;                  // Sea2.FoamUV
    float sun_road = 1.0f;                 // whf_sun_road_multiply
    Vec3 fog_color{0.5f, 0.6f, 0.7f};      // the game's fog colour (gamma)
    float fog_density = 0.0f;              // Sea2 fog density per metre of view depth (0: no fog)
};

struct WaterFrame
{
    bgfx::ViewId view = 0;     // view with the scene frame buffer (colour + depth) and bgfx::setViewTransform set
    Vec3 camera{0.0f, 0.0f, 0.0f};
    float time = 0.0f;         // seconds
    float crest_height = 4.0f; // wave height that counts as a full crest: Amp1 + Amp2
    float far_plane = 4000.0f;
    bgfx::TextureHandle env_cube = BGFX_INVALID_HANDLE; // optional: the SEA entity's environment cube
};

class Water
{
  public:
    static constexpr int kLayerSize = EngineWaveField::kSize;

    Water() = default;
    ~Water();
    Water(const Water &) = delete;
    Water &operator=(const Water &) = delete;

    // Vertex layout of SEA::SeaVertex for PrepareMesh().
    static const bgfx::VertexLayout &MeshLayout();

    // WaterQuality::High only, once per frame after the opaque scene and before the water: copies the scene
    // colour and view distance at half resolution in `view` (one full-screen pass). `scene_depth` must be a
    // sampleable depth texture (BGFX_TEXTURE_RT, not write-only). High without this call is refused.
    void CaptureScene(bgfx::ViewId view, bgfx::TextureHandle scene_color, bgfx::TextureHandle scene_depth, uint16_t width,
                      uint16_t height, float near_plane, float far_plane);

    // Nothing here falls back to a lesser path. A request that cannot be honoured is refused and logged
    // once (fx/log.hpp): settings.water == Legacy, High without CaptureScene() this frame, SubmitField()
    // without UpdateField(). The Submit* calls then return false and PrepareMesh() an invalid program: draw
    // the legacy sea instead, or fix the caller. Use ConstrainSettings() to keep unsupported levels out.

    // Sets program state for a draw call with SeaVertex geometry and returns the program. The caller binds
    // its vertex / index buffers and calls bgfx::submit(frame.view, program). `world_offset` is what the
    // engine subtracted from the vertex positions (SEA::vWorldOffset x, z).
    bgfx::ProgramHandle PrepareMesh(const Settings &settings, const WaterFrame &frame, const WaterShading &shading, Sky &sky,
                                    std::array<float, 2> world_offset);

    // Uploads the two height-field layers for SubmitField() (128x128 RGBA16F with mips, about 0.1 ms).
    void UpdateField(const EngineWaveField &field);
    bool SubmitField(const Settings &settings, const WaterFrame &frame, const WaterShading &shading, Sky &sky,
                     const EngineSeaState &sea);
    bool SubmitGerstner(const Settings &settings, const WaterFrame &frame, const WaterShading &shading, Sky &sky,
                        const GerstnerSet &waves, float bump_scale);

    void Shutdown();
    void OnDeviceLost();

  private:
    enum class Source
    {
        Mesh,
        Field,
        Gerstner
    };
    void EnsureResources();
    void EnsureGrid(WaterQuality quality);
    bgfx::ProgramHandle Bind(Source source, const Settings &settings, const WaterFrame &frame, const WaterShading &shading, Sky &sky);
    template <typename SetWaveUniforms>
    bool SubmitGrid(Source source, const Settings &settings, const WaterFrame &frame, const WaterShading &shading, Sky &sky,
                    SetWaveUniforms &&set_wave_uniforms);

    bool resources_ = false;
    std::array<bgfx::ProgramHandle, 15> programs_{}; // source * 5 + fragment variant
    bgfx::TextureHandle detail_ = BGFX_INVALID_HANDLE;
    std::array<bgfx::TextureHandle, 2> layers_{};
    bgfx::FrameBufferHandle behind_ = BGFX_INVALID_HANDLE;
    uint16_t behind_width_ = 0;
    uint16_t behind_height_ = 0;
    bool behind_valid_ = false;
    bool reported_legacy_ = false;
    bool reported_snapshot_ = false;
    bool reported_field_ = false;
    bgfx::ProgramHandle capture_program_ = BGFX_INVALID_HANDLE;
    int grid_cells_ = 0;
    bgfx::VertexBufferHandle grid_vertices_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle grid_full_ = BGFX_INVALID_HANDLE;
    std::array<bgfx::IndexBufferHandle, 4> grid_rings_{};
    bgfx::UniformHandle s_detail_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_env_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_behind_color_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_behind_depth_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_layer0_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_layer1_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_scene_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_scene_depth_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_water_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_water_vs_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_wave_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_capture_ = BGFX_INVALID_HANDLE;
};

} // namespace storm::fx
