#include "storm/fx/sky.hpp"

#include "gpu_util.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{
using namespace detail;

Sky::~Sky()
{
    Shutdown();
}

void Sky::Shutdown()
{
    Destroy(u_aerial_);
    Destroy(u_wind_);
    resources_ = false;
}

void Sky::OnDeviceLost()
{
    u_aerial_ = BGFX_INVALID_HANDLE;
    u_wind_ = BGFX_INVALID_HANDLE;
    resources_ = false;
}

void Sky::Update(const WeatherInput &weather, float time_seconds, float camera_height)
{
    time_ = time_seconds;
    camera_height_ = camera_height;
    if (!state_valid_ || SkyNeedsRebuild(weather_, weather))
    {
        weather_ = weather;
        state_ = ComputeSky(weather);
        state_valid_ = true;
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
    if (resources_)
        return;
    resources_ = true;
    u_aerial_ = bgfx::createUniform("u_fxAerial", bgfx::UniformType::Vec4, 3);
    u_wind_ = bgfx::createUniform("u_fxWind", bgfx::UniformType::Vec4, 2);
}

void Sky::BindAerial(const std::array<float, 16> &view)
{
    EnsureResources();
    bgfx::setUniform(u_aerial_, ComputeAerial(state_, camera_height_, view).u_fxAerial.data(), 3);
}

void Sky::BindWind()
{
    EnsureResources();
    bgfx::setUniform(u_wind_, ComputeWind(weather_, time_).u_fxWind.data(), 2);
}

} // namespace storm::fx
