// Graphics effects (renderer_bgfx/fx) as seen by the engine: options, the scene boundary and the per-frame
// inputs the renderer cannot observe through the D3D9-shaped shim. Game thread only.
//
// Presets: «Низкая» (0, the legacy image: no effect runs and the frame is composed exactly as
// before), «Средняя» (1), «Высокая» (2). The fx_* options are developer overrides (engine.ini), not player settings. What the device cannot do is
// switched off once, when the options are applied, and reported (log + EffectsReport::changes); nothing is
// lowered silently later.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace storm::shim
{
struct BaseTexture;
}

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
    int preset = 0;                    // 0 Низкая, 1 Средняя, 2 Высокая
    std::string summary;               // the effective settings in one line
    std::string support;               // what the device supports (fx::Support::Describe)
    std::vector<std::string> changes;  // options the device cannot honour, switched off (one line each)
    std::vector<std::string> errors;   // unknown option names / values
    std::vector<std::string> disabled; // option names the settings screen must grey out on this device
    // The request does not draw yet: its shader programs are being linked (a few per frame) and its GPU self-test
    // run while the previous settings keep drawing. Becomes false at the Present that switches.
    bool pending = false;
};

// Applies `preset` and `options` (overrides). Safe before the device exists: the request is kept and
// constrained when the device is created. On a live device the previous settings keep drawing until the new ones
// are prepared (EffectsReport::pending); the device itself starts on «Низкая» and switches the same way.
// Returns the report of the request.
EffectsReport ConfigureEffects(int preset, const std::vector<EffectsOption> &options);
EffectsReport CurrentEffects();
// A one-line notice for the player (Russian) when the GPU self-test switched a stage off on this device; empty
// when there is none. Each notice is returned once.
std::string TakeEffectsNotice();

// The 3D scene of this frame is complete; what follows is the legacy interface (Renderer::MakePostProcess,
// called by every interface before it draws). The post chain runs here, on the scene only.
void EffectsSceneComplete();

// The game's underwater screen wobble of this frame (script Render.SeaEffect, SeaEffectSize, SeaEffectSpeed), set by
// the renderer at the start of every frame. It is the legacy look and runs on every preset.
struct EffectsWobble
{
    bool on = false;
    float amplitude = 0.0f; // wave height, fraction of the picture size
    float phase = 0.0f;     // radians, advanced by the renderer at SeaEffectSpeed radians per second
};
void SetEffectsWobble(const EffectsWobble &wobble);

// The weather of this frame, pushed by the WEATHER entity every frame it runs. A frame without it has no sky
// (interiors): sun effects are off and the scene grade is the interior one. The scene camera is taken from the
// draws themselves (the last perspective draw into the game picture).
struct EffectsWeather
{
    std::array<float, 3> sun_dir{0.0f, 1.0f, 0.0f}; // towards the sun (or the moon at night), world, y up
    bool sun_present = false;                         // the sun, not the moon, is above the horizon
    // Towards the sun also while it is below the horizon (straight down at night): the analytic sky and the scene
    // grade take the day / night state from it, never from the moon.
    std::array<float, 3> sky_sun_dir{0.0f, -1.0f, 0.0f};
    std::array<float, 3> sun_color{1.0f, 1.0f, 1.0f}; // 0..1
    std::array<float, 3> ambient{0.4f, 0.4f, 0.4f};
    std::array<float, 3> fog_color{0.5f, 0.6f, 0.7f};
    float fog_density = 0.003f;
    float overcast = 0.0f; // 0 clear .. 1 storm
    std::array<float, 2> wind_dir{1.0f, 0.0f};
    float wind_speed = 5.0f;
};
void SetEffectsWeather(const EffectsWeather &weather);

// While CurrentEffectsWorld().lens_flare: how much of the sun the camera sees this frame (0..1: SUNGLOW's traced
// visibility past terrain, buildings and sails, with its fade at the horizon), and how strong the flare ghosts are
// (0..1: the weather's Sun.Flare and SUNGLOW's fade as the sun leaves the view). The fx sun glare is drawn with
// them; a frame without the call has no glare.
void SetEffectsSunGlare(float glare, float ghosts);

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

// Location, every frame it draws: its static lamps (the lights of lights.ini at its locators, colour with this
// frame's flicker). In interiors the strongest ones cast shadows (fx_lamp_shadows); outdoors: empty.
void SetEffectsLamps(std::span<const EffectsPointLight> lamps);

// Location, once an interior is loaded: the static geometry its ambient-occlusion volume is baked from (world space
// positions, triangle list; `name` names it in the log) and the box that gets the volume (where one walks, with a
// margin: the geometry outside it, such as the backdrop behind the windows, only occludes). Outdoors and when
// leaving: empty spans. The volume is read from the cache or baked on a worker while the settings ask for it
// (fx_ao_volume, Средняя / Высокая).
void SetEffectsAoGeometry(std::string_view name, std::span<const std::array<float, 3>> positions,
                          std::span<const uint32_t> indices, const std::array<float, 3> &bounds_lo = {},
                          const std::array<float, 3> &bounds_hi = {});

// The game file a texture was loaded from (Renderer::TextureLoad, after its levels are filled). Stone, wood, plaster
// and ground textures and grey height ("bump") textures get a normal map for the per-pixel lighting's relief
// (fx_light_normal_map, fx/normal_map.hpp).
void SetEffectsTextureSource(shim::BaseTexture *texture, const char *path);

// Where the effects keep what they derive from the game's files (normal maps), at most `max_bytes` (least recently
// used files are evicted): the app's cache directory on Android. Before the first texture is loaded.
void ConfigureEffectsCache(const std::filesystem::path &directory, uint64_t max_bytes);

// ---- World passes: fx sky, contact shadows ----
// The consumer entities (SKY, SEA, shadow, SUNGLOW) ask which parts are on and then either call the fx pass below
// or keep their legacy drawing. Changes only at the Present that switches settings (EffectsReport::pending);
// everything false in «Низкая».
struct EffectsWorld
{
    bool sky = false;             // fx sky replaces the textured sky box and the fog sphere of SKY
    bool contact_shadows = false; // a soft blob under each character
    bool lens_flare = false;      // the post chain draws the sun glare: SUNGLOW leaves its glow, overflow and flare
                                  // sprites of the sun out and reports the sun's visibility (SetEffectsSunGlare)
    // An interior with lamp shadows: the characters' shadows come from the room's lamps; the shadow entity leaves
    // its projected shadow (cast along the outdoor sun) out.
    bool lamp_character_shadows = false;
};
EffectsWorld CurrentEffectsWorld();

// The fog colour that matches the fx sky's horizon (0..1, gamma-encoded) while the fx sky is on and the weather of
// this frame is known; false otherwise (keep the game's fog colour).
bool EffectsSkyFogColor(std::array<float, 3> &rgb);

// The world passes below draw into the current render target with the current D3D projection, in submission
// order. `view` is the world -> view matrix as the engine sees it (VRenderer::GetTransform(D3DTS_VIEW)): the
// device itself holds a camera-relative view, because Renderer moves the camera translation into the world matrix.
using EffectsMatrix = std::array<float, 16>;

// Draws the analytic sky on the far plane (depth test only). Only while CurrentEffectsWorld().sky.
void EffectsDrawSky(const EffectsMatrix &view);

// A contact shadow on the floor at `ground` (a character's feet), drawn at once. Call it after the floor is drawn.
// Only while CurrentEffectsWorld().contact_shadows.
void EffectsContactShadow(const std::array<float, 3> &ground, const EffectsMatrix &view);

} // namespace storm::render
