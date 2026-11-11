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
#include <functional>
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

// The game file a texture was loaded from (Renderer::TextureLoad, after its levels are filled). Grey height
// ("bump") textures get a normal map for the per-pixel lighting's relief (fx_light_normal_map).
void SetEffectsTextureSource(shim::BaseTexture *texture, const char *path);

// Renderer: the device texture behind a renderer texture id (EffectsWaterMesh::env_map), null for an invalid id.
void SetEffectsTextureResolver(std::function<shim::BaseTexture *(int32_t)> resolver);

// ---- World passes: fx sky, fx water, contact shadows ----
// The consumer entities (SKY, SEA, shadow, SUNGLOW) ask which parts are on and then either call the fx pass below
// or keep their legacy drawing. Changes only at the Present that switches settings (EffectsReport::pending);
// everything false in «Низкая».
struct EffectsWorld
{
    bool sky = false;             // fx sky replaces the textured sky box and the fog sphere of SKY
    bool water = false;           // fx water replaces the Sea2 / Sea3 passes of SEA (and its reflection renders)
    bool contact_shadows = false; // a soft blob under each character
    bool lens_flare = false;      // the post chain draws the lens flare: SUNGLOW leaves its flare sprites out
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

// The SEA entity's surface for the fx water: its CPU-displaced vertices (SEA::SeaVertex: position relative to
// `world_offset`, normal, uv) and triangle list, the look from its Sea2 attributes and its environment cube (the
// scene reflected in the sea, rendered this frame by SEA::EnvMap_Render). Only while CurrentEffectsWorld().water.
struct EffectsWaterShading
{
    std::array<float, 3> water_color{0.04f, 0.22f, 0.39f}; // Sea2.WaterColor
    std::array<float, 3> sky_color{1.0f, 1.0f, 1.0f};      // Sea2.SkyColor
    float reflection = 0.7f;                               // Sea2.Reflection
    float transparency = 0.6f;                             // Sea2.Transparency
    float attenuation = 0.2f;                              // Sea2.Attenuation
    float fresnel = 0.5f;                                  // Sea2.Frenel
    float bump_speed = 1.0f;                               // Sea2.BumpSpeed
    bool foam = false;                                     // Sea2.FoamEnable and the foam option
    float foam_v = 3.0f, foam_k = 0.3f, foam_uv = 1.0f;    // Sea2.FoamV / FoamK / FoamUV
    std::array<float, 3> fog_color{0.5f, 0.6f, 0.7f};      // D3DRS_FOGCOLOR
    float fog_density = 0.0f;                              // Sea.Fog.SeaDensity while Sea.Fog.Enable, else 0
};
struct EffectsWaterMesh
{
    const void *vertices = nullptr; // vertex_count * 32 bytes (SEA::SeaVertex)
    uint32_t vertex_count = 0;
    const uint16_t *indices = nullptr;
    uint32_t index_count = 0;
    std::array<float, 2> world_offset{0.0f, 0.0f}; // SEA::vWorldOffset x, z
    float crest_height = 4.0f;                     // Amp1 + Amp2
    EffectsWaterShading shading;
    int32_t env_map = -1; // required: renderer texture id of the cube the water reflects
};
void EffectsDrawWater(const EffectsWaterMesh &mesh, const EffectsMatrix &view);

// A contact shadow on the floor at `ground` (a character's feet), drawn at once. Call it after the floor is drawn.
// Only while CurrentEffectsWorld().contact_shadows.
void EffectsContactShadow(const std::array<float, 3> &ground, const EffectsMatrix &view);

} // namespace storm::render
