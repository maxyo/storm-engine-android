#include "storm/fx/sky_model.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{
namespace
{
constexpr float kPi = 3.14159265358979f;
constexpr float kPlanetRadius = 6360e3f;
constexpr float kAtmosphereRadius = 6420e3f;
constexpr float kRayleighHeight = 8000.0f;
constexpr float kMieHeight = 1200.0f;
constexpr Vec3 kRayleigh{5.8e-6f, 13.5e-6f, 33.1e-6f};
constexpr float kMie = 21e-6f;
constexpr float kMieG = 0.76f;
constexpr float kSunIntensity = 22.0f;
constexpr int kViewSteps = 12;
constexpr int kLightSteps = 5;

Vec3 operator+(Vec3 a, Vec3 b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3 operator*(Vec3 a, Vec3 b) { return {a[0] * b[0], a[1] * b[1], a[2] * b[2]}; }
Vec3 operator*(Vec3 a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float Dot(Vec3 a, Vec3 b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 Mix(Vec3 a, Vec3 b, float t) { return a * (1.0f - t) + b * t; }
float Luma(Vec3 c) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }
float Saturate(float v) { return std::clamp(v, 0.0f, 1.0f); }
Vec3 Decode(Vec3 gamma) { return {gamma[0] * gamma[0], gamma[1] * gamma[1], gamma[2] * gamma[2]}; }
float Smoothstep(float a, float b, float x)
{
    const float t = Saturate((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

// Distance from `origin` along `dir` to the sphere of radius `radius` centred on the planet.
float ExitDistance(Vec3 origin, Vec3 dir, float radius)
{
    const float b = Dot(origin, dir);
    const float c = Dot(origin, origin) - radius * radius;
    return -b + std::sqrt(std::max(b * b - c, 0.0f));
}

struct Atmosphere
{
    float mie_scale; // turbidity
};

// Fraction of sunlight that reaches the ground from direction `sun`.
void SunTransmittance(const Atmosphere &atm, Vec3 sun, Vec3 &out)
{
    const Vec3 origin{0.0f, kPlanetRadius + 30.0f, 0.0f};
    // Below the horizon the path is clamped: twilight fades through the `night` factor instead.
    const Vec3 dir{sun[0], std::max(sun[1], 0.0f), sun[2]};
    const float inv = 1.0f / std::sqrt(Dot(dir, dir));
    const Vec3 unit = dir * inv;
    const float length = ExitDistance(origin, unit, kAtmosphereRadius);
    float depth_r = 0.0f, depth_m = 0.0f, travelled = 0.0f;
    constexpr int kSteps = 24;
    for (int i = 0; i < kSteps; ++i)
    {
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(kSteps);
        const float end = length * t1 * t1 * t1;
        const Vec3 p = origin + unit * (0.5f * (travelled + end));
        const float height = std::sqrt(Dot(p, p)) - kPlanetRadius;
        depth_r += std::exp(-height / kRayleighHeight) * (end - travelled);
        depth_m += std::exp(-height / kMieHeight) * (end - travelled);
        travelled = end;
    }
    for (int c = 0; c < 3; ++c)
        out[c] = std::exp(-(kRayleigh[c] * depth_r + 1.1f * kMie * atm.mie_scale * depth_m));
}

// Radiance scattered towards the eye along `dir` (y up), sun at `sun`; `sun_extinction` is SunTransmittance().
Vec3 Scatter(const Atmosphere &atm, Vec3 dir, Vec3 sun, Vec3 sun_extinction)
{
    const Vec3 origin{0.0f, kPlanetRadius + 30.0f, 0.0f};
    const float length = ExitDistance(origin, dir, kAtmosphereRadius);
    const float mu = Dot(dir, sun);
    const float phase_r = 3.0f / (16.0f * kPi) * (1.0f + mu * mu);
    const float g2 = kMieG * kMieG;
    const float phase_m =
        3.0f / (8.0f * kPi) * ((1.0f - g2) * (1.0f + mu * mu)) / ((2.0f + g2) * std::pow(1.0f + g2 - 2.0f * kMieG * mu, 1.5f));
    const float mie = kMie * atm.mie_scale;
    float depth_r = 0.0f, depth_m = 0.0f;
    Vec3 sum_r{0.0f, 0.0f, 0.0f}, sum_m{0.0f, 0.0f, 0.0f};
    float travelled = 0.0f;
    for (int i = 0; i < kViewSteps; ++i)
    {
        // Cubic spacing: rays along the horizon are hundreds of kilometres long, yet most of what they
        // gather comes from the first few kilometres of dense air.
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(kViewSteps);
        const float end = length * t1 * t1 * t1;
        const float step = end - travelled;
        const Vec3 p = origin + dir * (travelled + 0.5f * step);
        travelled = end;
        const float height = std::sqrt(Dot(p, p)) - kPlanetRadius;
        const float hr = std::exp(-height / kRayleighHeight) * step;
        const float hm = std::exp(-height / kMieHeight) * step;
        depth_r += hr;
        depth_m += hm;
        const float light_step = ExitDistance(p, sun, kAtmosphereRadius) / static_cast<float>(kLightSteps);
        float light_r = 0.0f, light_m = 0.0f;
        for (int j = 0; j < kLightSteps; ++j)
        {
            const Vec3 q = p + sun * (light_step * (static_cast<float>(j) + 0.5f));
            // Below the surface the planet blocks the sun; a very dense medium approximates that smoothly.
            const float h = std::max(std::sqrt(Dot(q, q)) - kPlanetRadius, -20000.0f);
            light_r += std::exp(-h / kRayleighHeight) * light_step;
            light_m += std::exp(-h / kMieHeight) * light_step;
        }
        // Air scattering sees less reddened sunlight than the direct path suggests: light scattered more than
        // once (and ozone) keeps the twilight zenith blue.
        Vec3 attenuation_r, attenuation_m;
        for (int c = 0; c < 3; ++c)
        {
            attenuation_r[c] = std::exp(-(kRayleigh[c] * (depth_r + 0.4f * light_r) + 1.1f * mie * (depth_m + light_m)));
            attenuation_m[c] = std::exp(-(kRayleigh[c] * (depth_r + light_r) + 1.1f * mie * (depth_m + light_m)));
        }
        sum_r = sum_r + attenuation_r * hr;
        sum_m = sum_m + attenuation_m * hm;
    }
    // The panorama is too coarse for the forward peak of the haze: keep a third of it here, the shader adds
    // the sharp halo analytically (fxSunHalo).
    Vec3 single = (sum_r * kRayleigh * phase_r + sum_m * (mie * phase_m * 0.33f)) * kSunIntensity;
    // Light scattered more than once also fills in what single scattering leaves dark: a pale veil that
    // grows with the optical depth of the view ray (the bright horizon of a clear day).
    const float sun_level = Luma(sun_extinction) * Smoothstep(-0.12f, 0.02f, sun[1]);
    const Vec3 veil = Mix(Vec3{0.50f, 0.68f, 1.0f}, Vec3{1.0f, 1.0f, 1.0f}, 0.45f * sun_level);
    for (int c = 0; c < 3; ++c)
    {
        const float opacity = 1.0f - std::exp(-(kRayleigh[c] * depth_r + 1.1f * mie * depth_m));
        single[c] += opacity * veil[c] * 0.22f * sun_level;
    }
    return single;
}

Vec3 DirFromLut(float u, float v, Vec3 sun)
{
    const float sin_elev = v * v;
    const float cos_elev = std::sqrt(std::max(1.0f - sin_elev * sin_elev, 0.0f));
    const float cos_az = 1.0f - 2.0f * u;
    const float sin_az = std::sqrt(std::max(1.0f - cos_az * cos_az, 0.0f));
    float sx = sun[0], sz = sun[2];
    const float len = std::hypot(sx, sz);
    if (len < 1e-4f)
        sx = 1.0f, sz = 0.0f;
    else
        sx /= len, sz /= len;
    // Rotate the sun's horizontal direction by the azimuth difference.
    return {cos_elev * (sx * cos_az - sz * sin_az), sin_elev, cos_elev * (sz * cos_az + sx * sin_az)};
}
} // namespace

SkyState ComputeSky(const WeatherInput &weather)
{
    SkyState sky;
    sky.sun_dir = weather.sun_dir;
    sky.fog_density = weather.fog_density;
    const float overcast = Saturate(weather.overcast);
    const float sun_height = weather.sun_dir[1];
    sky.night = 1.0f - Smoothstep(-0.14f, 0.05f, sun_height);

    // Haze: the game's fog density drives turbidity and how far the horizon is pulled to the fog colour.
    const float haze = Saturate(weather.fog_density / 0.008f);
    const Atmosphere atm{0.6f + 3.0f * haze + 6.0f * overcast};
    const Vec3 sun_tint = Decode(weather.sun_color);
    const Vec3 fog = Decode(weather.fog_color);
    const Vec3 night_zenith{0.007f, 0.013f, 0.036f};
    const Vec3 night_horizon{0.020f, 0.034f, 0.066f};
    constexpr float kExposure = 1.9f; // display exposure of the scattered radiance

    // Overcast dome: grey that follows the daylight level and the game's fog colour.
    Vec3 sun_transmittance;
    SunTransmittance(atm, weather.sun_dir, sun_transmittance);
    const float daylight = Smoothstep(-0.10f, 0.25f, sun_height);
    const Vec3 fog_grey = Mix(fog, Vec3{1.0f, 1.0f, 1.0f} * Luma(fog), 0.5f);
    const Vec3 dome = Mix(Vec3{0.30f, 0.33f, 0.37f} * (0.08f + 0.92f * daylight), fog_grey, 0.4f);

    sky.lut.resize(static_cast<size_t>(kSkyLutWidth) * kSkyLutHeight * 4);
    Vec3 horizon_sum{0.0f, 0.0f, 0.0f}, upper_sum{0.0f, 0.0f, 0.0f};
    float upper_weight = 0.0f;
    for (int y = 0; y < kSkyLutHeight; ++y)
        for (int x = 0; x < kSkyLutWidth; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(kSkyLutWidth - 1);
            const float v = static_cast<float>(y) / static_cast<float>(kSkyLutHeight - 1);
            const Vec3 dir = DirFromLut(u, v, weather.sun_dir);
            Vec3 c = Scatter(atm, dir, weather.sun_dir, sun_transmittance) * sun_tint;
            for (float &channel : c)
                channel = 1.0f - std::exp(-channel * kExposure);
            // The shoulder above washes colour out; give some of it back.
            const float grey = Luma(c);
            for (float &channel : c)
                channel = std::max(grey + (channel - grey) * 1.3f, 0.0f);
            const float horizon_weight = std::pow(1.0f - dir[1], 6.0f);
            c = c + Mix(night_zenith, night_horizon, horizon_weight) * sky.night;
            // Pull the horizon band to the game's fog colour, the whole dome to grey when overcast.
            c = Mix(c, fog, horizon_weight * (0.25f + 0.6f * haze) * (1.0f - 0.5f * sky.night));
            c = Mix(c, Mix(dome, dome * 0.8f, dir[1]), overcast * 0.9f);
            const size_t o = (static_cast<size_t>(y) * kSkyLutWidth + x) * 4;
            for (int i = 0; i < 3; ++i)
                sky.lut[o + i] = static_cast<uint8_t>(std::lround(std::sqrt(Saturate(c[i])) * 255.0f));
            sky.lut[o + 3] = 255;
            if (y == 0)
                horizon_sum = horizon_sum + c;
            upper_sum = upper_sum + c * dir[1];
            upper_weight += dir[1];
        }
    sky.horizon = horizon_sum * (1.0f / static_cast<float>(kSkyLutWidth));
    const Vec3 dome_average = upper_sum * (1.0f / upper_weight);

    sky.dome = dome_average;
    // Light for surfaces. The game's sun colour stays in charge of brightness; the atmosphere only tints it.
    const float transmittance_luma = std::max(Luma(sun_transmittance), 1e-4f);
    const Vec3 sun_hue = Mix(Vec3{1.0f, 1.0f, 1.0f}, sun_transmittance * (1.0f / transmittance_luma), 0.6f);
    sky.sun_light = sun_tint * sun_hue * (1.0f - 0.7f * overcast) * Smoothstep(-0.05f, 0.08f, sun_height);
    return sky;
}

bool SkyNeedsRebuild(const WeatherInput &a, const WeatherInput &b)
{
    const auto far = [](Vec3 p, Vec3 q, float eps) {
        return std::fabs(p[0] - q[0]) > eps || std::fabs(p[1] - q[1]) > eps || std::fabs(p[2] - q[2]) > eps;
    };
    return far(a.sun_dir, b.sun_dir, 0.004f) || far(a.sun_color, b.sun_color, 0.01f) ||
           far(a.ambient_color, b.ambient_color, 0.01f) || far(a.fog_color, b.fog_color, 0.01f) ||
           std::fabs(a.fog_density - b.fog_density) > 1e-4f || std::fabs(a.overcast - b.overcast) > 0.01f;
}

Vec3 SkyColor(const SkyState &sky, Vec3 dir)
{
    const float horizontal = std::hypot(dir[0], dir[2]);
    const float sun_horizontal = std::hypot(sky.sun_dir[0], sky.sun_dir[2]);
    float cos_az = 1.0f;
    if (horizontal > 1e-5f && sun_horizontal > 1e-5f)
        cos_az = (dir[0] * sky.sun_dir[0] + dir[2] * sky.sun_dir[2]) / (horizontal * sun_horizontal);
    const float u = 0.5f - 0.5f * cos_az;
    const float v = std::sqrt(Saturate(dir[1]));
    const float px = u * static_cast<float>(kSkyLutWidth - 1), py = v * static_cast<float>(kSkyLutHeight - 1);
    const int x0 = std::min(static_cast<int>(px), kSkyLutWidth - 2), y0 = std::min(static_cast<int>(py), kSkyLutHeight - 2);
    const float fx = px - static_cast<float>(x0), fy = py - static_cast<float>(y0);
    Vec3 out{0.0f, 0.0f, 0.0f};
    for (int corner = 0; corner < 4; ++corner)
    {
        const int dx = corner & 1, dy = corner >> 1;
        const float w = (dx ? fx : 1.0f - fx) * (dy ? fy : 1.0f - fy);
        const size_t o = (static_cast<size_t>(y0 + dy) * kSkyLutWidth + (x0 + dx)) * 4;
        for (int i = 0; i < 3; ++i)
            out[i] += w * static_cast<float>(sky.lut[o + i]) / 255.0f;
    }
    return Decode(out);
}

AerialUniforms ComputeAerial(const SkyState &sky, float camera_height, const std::array<float, 16> &view)
{
    AerialUniforms u;
    for (int i = 0; i < 3; ++i)
        u.u_fxAerial[0][i] = std::sqrt(Saturate(sky.horizon[i])); // the emulator's colours are gamma encoded
    u.u_fxAerial[0][3] = sky.fog_density;
    u.u_fxAerial[1] = {sky.fog_density * kHazeDensityFactor * std::exp(-std::max(camera_height, 0.0f) / kHazeLayerHeight),
                       1.0f / kHazeLayerHeight, 0.55f, 0.94f};
    u.u_fxAerial[2] = {view[4], view[5], view[6], 0.0f}; // world up (0, 1, 0) in view space
    return u;
}

std::array<float, 2> ComputeAerialFogHaze(float camera_height)
{
    // A sixth of the fx sky's haze: the emulated geometry keeps the game's fog, the layer only lifts it a little off
    // what rises above the eye.
    return {kHazeDensityFactor / 6.0f * std::exp(-std::max(camera_height, 0.0f) / kHazeLayerHeight), 1.0f / kHazeLayerHeight};
}

WindUniforms ComputeWind(const WeatherInput &weather, float time_seconds)
{
    WindUniforms u;
    const float strength = Saturate(weather.wind_speed / 18.0f);
    u.u_fxWind[0] = {weather.wind[0], 0.0f, weather.wind[1], time_seconds};
    u.u_fxWind[1] = {0.05f + 0.45f * strength, 1.1f + 1.6f * strength, 0.35f, 0.012f + 0.05f * strength};
    return u;
}

} // namespace storm::fx
