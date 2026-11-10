// Graphics effects (renderer_bgfx/fx) as seen by the engine: options, the scene boundary and the per-frame
// inputs the renderer cannot observe through the D3D9-shaped shim. Game thread only.
//
// Presets: «Оригинал» (0, parity with the legacy image: no effect runs and the frame is composed exactly as
// before), «Среднее» (1), «Высокое» (2). Individual options override the preset. What the device cannot do is
// switched off once, when the options are applied, and reported (log + EffectsReport::changes); nothing is
// lowered silently later.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace storm::render
{

// One option as the engine reads it: from engine.ini (host) or the app's graphics preferences (Android),
// always an integer. Unknown names are an error in the report.
struct EffectsOption
{
    std::string name;
    int value = 0;
};

// Every option name with its meaning; `preset_default` = -1 means "use the preset's value".
struct EffectsOptionInfo
{
    const char *name;
    const char *values; // e.g. "0 off, 1 on" or "1, 2, 4"
};
const std::vector<EffectsOptionInfo> &EffectsOptionNames();

struct EffectsReport
{
    int preset = 0;                    // 0 Оригинал, 1 Среднее, 2 Высокое
    std::string summary;               // the effective settings in one line
    std::string support;               // what the device supports (fx::Support::Describe)
    std::vector<std::string> changes;  // options the device cannot honour, switched off (one line each)
    std::vector<std::string> errors;   // unknown option names / values
    std::vector<std::string> disabled; // option names the settings screen must grey out on this device
};

// Applies `preset` and `options` (overrides). Safe before the device exists: the request is kept and
// constrained when the device is created. Returns the report of the request as applied.
EffectsReport ConfigureEffects(int preset, const std::vector<EffectsOption> &options);
EffectsReport CurrentEffects();

// The 3D scene of this frame is complete; what follows is the legacy interface (DX9RENDER::MakePostProcess,
// called by every interface before it draws). The post chain runs here, on the scene only.
void EffectsSceneComplete();

// The weather of this frame, pushed by the WEATHER entity every frame it runs. A frame without it has no sky
// (interiors): sun effects are off and the scene grade is the interior one. The scene camera is taken from the
// draws themselves (the last perspective draw into the game picture).
struct EffectsWeather
{
    std::array<float, 3> sun_dir{0.0f, 1.0f, 0.0f}; // towards the sun (or the moon at night), world, y up
    bool sun_present = false;                         // the sun, not the moon, is above the horizon
    std::array<float, 3> sun_color{1.0f, 1.0f, 1.0f}; // 0..1
    std::array<float, 3> ambient{0.4f, 0.4f, 0.4f};
    std::array<float, 3> fog_color{0.5f, 0.6f, 0.7f};
    float fog_density = 0.003f;
    float overcast = 0.0f; // 0 clear .. 1 storm
    std::array<float, 2> wind_dir{1.0f, 0.0f};
    float wind_speed = 5.0f;
};
void SetEffectsWeather(const EffectsWeather &weather);

// The technique block CTechnique executes (lower case), nullptr when its passes are closed. The per-pixel lighting
// picks the material from it (characters, sails, ...).
void SetEffectsTechnique(const char *name);

// Point lights of this frame that no baked vertex colours contain (lanterns carried by characters), world space,
// D3DLIGHT9 attenuation. Static location geometry, drawn with its baked colours, is lit only by these. A frame
// without the call has none.
struct EffectsPointLight
{
    std::array<float, 3> position{};
    std::array<float, 3> color{};
    float range = 10.0f;
    float att0 = 0.0f, att1 = 0.0f, att2 = 1.0f;
};
void SetEffectsDynamicLights(std::span<const EffectsPointLight> lights);

} // namespace storm::render
