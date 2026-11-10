// CPU side of the sky: single scattering (Rayleigh + Mie) integrated into a
// small panorama that the sky, water and fog shaders sample with one fetch.
// The panorama is rebuilt only when the weather input changes noticeably.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace storm::fx
{

using Vec3 = std::array<float, 3>;

// What the game's WEATHER entity knows (weather_base.h). Colours are the
// game's 0..1 gamma-encoded values.
struct WeatherInput
{
    Vec3 sun_dir{0.0f, 1.0f, 0.0f};      // unit vector towards the sun (whv_sun_pos normalised), y up
    Vec3 sun_color{1.0f, 1.0f, 1.0f};    // whc_sun_color
    Vec3 ambient_color{0.4f, 0.4f, 0.4f}; // whc_sun_ambient
    Vec3 fog_color{0.5f, 0.6f, 0.7f};    // whc_fog_color
    float fog_density = 0.003f;          // whf_fog_density
    float overcast = 0.0f;               // 0 clear sky .. 1 storm (rain / storm weathers)
    std::array<float, 2> wind{1.0f, 0.0f}; // direction the clouds drift to (x, z), unit
    float wind_speed = 5.0f;             // whf_wind_speed
};

inline constexpr int kSkyLutWidth = 64;  // azimuth relative to the sun: u = (1 - cos) / 2
inline constexpr int kSkyLutHeight = 32; // elevation: v = sqrt(sin(elevation))

struct SkyState
{
    // kSkyLutWidth * kSkyLutHeight RGBA8, row 0 = horizon. RGB = sqrt(linear colour).
    std::vector<uint8_t> lut;
    Vec3 sun_dir{0.0f, 1.0f, 0.0f};
    Vec3 sun_disc{1.0f, 1.0f, 1.0f};   // linear colour of the solar disc (may exceed 1)
    Vec3 sun_light{1.0f, 1.0f, 1.0f};  // linear direct light colour for surfaces
    Vec3 dome{0.3f, 0.45f, 0.7f};      // linear, cosine-weighted average of the sky dome
    Vec3 horizon{0.5f, 0.6f, 0.7f};    // linear, azimuth average of the horizon row: the fog colour
    Vec3 glow{1.0f, 0.9f, 0.7f};       // linear colour of the forward-scattering halo around the sun
    Vec3 cloud_lit{1.0f, 1.0f, 1.0f};
    Vec3 cloud_shade{0.5f, 0.55f, 0.6f};
    float cloud_cover = 0.4f;          // 0..1
    float fog_density = 0.003f;
    float night = 0.0f;                // 0 day .. 1 night
};

SkyState ComputeSky(const WeatherInput &weather);

// True when `b` differs enough from `a` to justify rebuilding the panorama.
bool SkyNeedsRebuild(const WeatherInput &a, const WeatherInput &b);

// CPU reference of the shader's panorama fetch: linear sky colour in a unit direction (no clouds, no disc).
Vec3 SkyColor(const SkyState &sky, Vec3 dir);

// Fog colour for geometry seen in the horizontal direction (x, z): the sky at the horizon there.
Vec3 FogColor(const SkyState &sky, std::array<float, 2> view_xz);

} // namespace storm::fx
