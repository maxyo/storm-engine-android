// One falloff for every light of the per-pixel lighting: the location's lamps, torches and candles, the carried and the
// ships' lanterns, the game's D3D point lights on the lit models, and the bakes of their light (the bounce). The game
// describes a light by D3D9 attenuation constants and a range, tuned by eye in gamma space: in linear light most of
// them burn white next to the flame and are gone a few metres away (a candle falls with the fourth power of the
// distance). Each is turned once into a windowed inverse square with a soft core:
//
//     linear light at distance d = colour² · intensity · window(d / radius) / (d² + core²),
//     window(x) = (1 - x⁴)² for x < 1, else 0
//
// where `radius` is `reach` times the distance at which the game's light falls to kGameLightFloor, `intensity` makes
// the new light equal `level` times the game's at kMatchShare of that distance, and `core` (at least the setting's)
// grows until the light next to the flame is at most `level` times the game's there: the light is less hot near the
// source and reaches further, about as strong as the game's in the middle of its reach. Without FX_LIGHT_LINEAR the
// shaders take the square root (gamma space), so one light looks the same either way. Mirrored by fxLightFalloff
// (fx_lighting.sh).
#pragma once

namespace storm::fx
{

// Settings::light_* (options fx_light_reach, fx_light_core, fx_light_level).
struct LightFalloff
{
    float reach = 1.6f;  // the radius over the game light's own reach
    float core = 0.4f;   // metres: the smallest soft core (the size of the flame and its glow)
    float level = 0.85f; // the light in the middle of its reach, and the most next to the flame, over the game's
    bool operator==(const LightFalloff &) const = default;
};

// The game light's luminance per unit colour where it is taken to end (the relight's floor on baked surfaces), and
// the farthest that is (before `reach`).
inline constexpr float kGameLightFloor = 0.12f;
inline constexpr float kGameLightReach = 12.0f;
// Where in that reach the new light equals `level` times the game's, and from how near the game's light counts
// (the flame's own size: nearer, the D3D formula runs off to infinity).
inline constexpr float kMatchShare = 0.4f;
inline constexpr float kGameLightNear = 0.12f;

struct LightShape
{
    float range = 0.0f;     // metres: no light beyond (the window's end)
    float intensity = 0.0f; // linear light per unit colour² at a metre from a point source (before the window)
    float core = 0.0f;      // metres
    bool operator==(const LightShape &) const = default;
};

// The shape of a game light: its D3D range and attenuation constants (1 / (att0 + att1 d + att2 d²)). A light that
// gives nothing (all constants zero or negative) or has no range gets range 0: it lights nothing.
LightShape ShapeOfGameLight(float range, float att0, float att1, float att2, const LightFalloff &falloff);

// Linear light per unit colour² at `distance` (multiply by the colour squared); fxLightFalloff of the shaders.
float LightFalloffAt(const LightShape &shape, float distance);
// The same in the gamma space of the game's own colours (its square root): what the baked colours, the bounce and the
// selection of lights compare with.
float LightFalloffGammaAt(const LightShape &shape, float distance);

} // namespace storm::fx
