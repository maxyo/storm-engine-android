#include "storm/fx/sky.hpp"

#include "gpu_util.hpp"
#include "storm/fx/noise_bake.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{
using namespace detail;

namespace
{
constexpr float kSunDiscCos = 0.99990f; // 0.8 degrees: a little larger than life, as the original sun sprite
constexpr float kCloudScale = 0.085f;
} // namespace

Sky::~Sky()
{
    Shutdown();
}

void Sky::Shutdown()
{
    Destroy(lut_);
    Destroy(clouds_);
    Destroy(program_);
    Destroy(program_clouds_);
    Destroy(s_lut_);
    Destroy(s_clouds_);
    Destroy(u_sky_);
    resources_ = false;
    lut_dirty_ = true;
}

void Sky::OnDeviceLost()
{
    lut_ = BGFX_INVALID_HANDLE;
    clouds_ = BGFX_INVALID_HANDLE;
    program_ = BGFX_INVALID_HANDLE;
    program_clouds_ = BGFX_INVALID_HANDLE;
    s_lut_ = BGFX_INVALID_HANDLE;
    s_clouds_ = BGFX_INVALID_HANDLE;
    u_sky_ = BGFX_INVALID_HANDLE;
    resources_ = false;
    lut_dirty_ = true;
}

void Sky::Update(const WeatherInput &weather, float time_seconds)
{
    time_ = time_seconds;
    if (!state_valid_ || SkyNeedsRebuild(weather_, weather))
    {
        weather_ = weather;
        state_ = ComputeSky(weather);
        state_valid_ = true;
        lut_dirty_ = true;
    }
    else
    {
        // Wind does not affect the panorama.
        weather_.wind = weather.wind;
        weather_.wind_speed = weather.wind_speed;
    }
}

void Sky::EnsureResources()
{
    if (!resources_)
    {
        resources_ = true;
        s_lut_ = bgfx::createUniform("s_fxSkyLut", bgfx::UniformType::Sampler);
        s_clouds_ = bgfx::createUniform("s_fxCloudNoise", bgfx::UniformType::Sampler);
        u_sky_ = bgfx::createUniform("u_fxSky", bgfx::UniformType::Vec4, 6);
        lut_ = bgfx::createTexture2D(kSkyLutWidth, kSkyLutHeight, false, 1, bgfx::TextureFormat::RGBA8, kClampLinear);
        const BakedTexture noise = BakeCloudNoise();
        clouds_ = bgfx::createTexture2D(static_cast<uint16_t>(noise.size), static_cast<uint16_t>(noise.size), true, 1,
                                        bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE,
                                        bgfx::copy(noise.rgba.data(), static_cast<uint32_t>(noise.rgba.size())));
        lut_dirty_ = true;
    }
    if (lut_dirty_ && state_valid_)
    {
        bgfx::updateTexture2D(lut_, 0, 0, 0, 0, kSkyLutWidth, kSkyLutHeight,
                              bgfx::copy(state_.lut.data(), static_cast<uint32_t>(state_.lut.size())));
        lut_dirty_ = false;
    }
}

void Sky::Bind(const Settings &settings)
{
    EnsureResources();
    const SkyState &s = state_;
    const float horizontal = std::hypot(s.sun_dir[0], s.sun_dir[2]);
    const float sx = horizontal > 1e-4f ? s.sun_dir[0] / horizontal : 1.0f;
    const float sz = horizontal > 1e-4f ? s.sun_dir[2] / horizontal : 0.0f;
    // Clouds drift with the wind; the layer is about 1.5 km up, so 1 uv unit is roughly 18 km.
    const float drift = time_ * weather_.wind_speed * 0.00006f;
    const float uniforms[6][4] = {
        {s.sun_dir[0], s.sun_dir[1], s.sun_dir[2], kSunDiscCos},
        {sx, sz, s.fog_density, settings.sky_clouds ? s.cloud_cover : 0.0f},
        {s.glow[0], s.glow[1], s.glow[2], kCloudScale},
        {s.sun_disc[0], s.sun_disc[1], s.sun_disc[2], 0.0f},
        {s.cloud_lit[0], s.cloud_lit[1], s.cloud_lit[2], -weather_.wind[0] * drift},
        {s.cloud_shade[0], s.cloud_shade[1], s.cloud_shade[2], -weather_.wind[1] * drift},
    };
    bgfx::setUniform(u_sky_, uniforms, 6);
    bgfx::setTexture(5, s_lut_, lut_, kClampLinear);
    bgfx::setTexture(6, s_clouds_, clouds_, BGFX_SAMPLER_NONE);
}

void Sky::Submit(bgfx::ViewId view, const Settings &settings, const float *view_matrix, const float *projection)
{
    EnsureResources();
    bgfx::ProgramHandle &program = settings.sky_clouds ? program_clouds_ : program_;
    if (!bgfx::isValid(program))
        program = LoadProgram("vs_fx_sky", settings.sky_clouds ? "fs_fx_sky_clouds" : "fs_fx_sky");

    // World-space view rays through three corners of the view. projection[11] is +1 for a left-handed
    // (D3D-style) projection and -1 for a right-handed one.
    std::array<std::array<float, 3>, 3> rays;
    const float corners[3][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 1.0f}};
    for (int i = 0; i < 3; ++i)
    {
        const float w = projection[11];
        const float eye[3] = {(corners[i][0] * w - projection[8]) / projection[0], (corners[i][1] * w - projection[9]) / projection[5], w};
        for (int axis = 0; axis < 3; ++axis)
            rays[i][axis] = eye[0] * view_matrix[axis * 4 + 0] + eye[1] * view_matrix[axis * 4 + 1] + eye[2] * view_matrix[axis * 4 + 2];
    }
    Bind(settings);
    SubmitScreenTriangle(view, program, BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL, false, &rays);
}

} // namespace storm::fx
