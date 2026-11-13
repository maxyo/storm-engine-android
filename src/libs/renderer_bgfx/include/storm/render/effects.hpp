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

// Renderer, whenever its camera moves: the game's Renderer keeps the device camera-relative (the D3D view has no
// translation; every world matrix and D3D light is shifted by minus the camera position, for float precision far
// from the origin). `origin` is that camera position: absolute world = device world + origin. Every effect that meets
// absolute world data (the location's geometry, lamps, dynamic lights, lightmaps) converts with it.
void SetEffectsWorldOrigin(const std::array<float, 3> &origin);

// Point lights of this frame that no baked vertex colours contain (lanterns and torches carried by characters), world
// space, D3DLIGHT9 attenuation. Static location geometry, drawn with its baked colours, is lit only by these; the
// nearest to the camera cast shadows (fx_lamp_moving). A frame without the call has none.
struct EffectsPointLight
{
    std::array<float, 3> position{};
    std::array<float, 3> color{};
    float range = 10.0f;
    float att0 = 0.0f, att1 = 0.0f, att2 = 1.0f;
};
void SetEffectsDynamicLights(std::span<const EffectsPointLight> lights);

// Location, every frame it draws: its static lamps (the lights of lights.ini at its locators, colour with this
// frame's flicker). The ones around the camera relight the location per pixel, each with its shadow
// (fx_lamp_shadows).
void SetEffectsLamps(std::span<const EffectsPointLight> lamps);

// Location, once it is loaded: its still geometry (world space, triangle list), for everything the effects cast or
// bake from it: the lamps' shadows, the room light, and in an interior the ambient-occlusion volume (read from the
// cache or baked on a worker while the settings ask for it: fx_ao_volume). When leaving: an empty one.
struct EffectsLocationGeometry
{
    std::string_view name; // names it in the log
    std::span<const std::array<float, 3>> positions;
    // One D3DCOLOR per position: the baked lighting (in an interior it is the light its lit models stand in).
    std::span<const uint32_t> colors;
    // The first texture coordinates per position, and the runs of `indices` drawn with one texture: where a texture's
    // alpha cuts holes (foliage, nets, gratings) the shadows and the occlusion have them too. Indices outside every
    // run count as solid.
    std::span<const std::array<float, 2>> uvs;
    std::span<const uint32_t> indices;
    struct Surface
    {
        uint32_t first_index = 0, index_count = 0;
        shim::BaseTexture *texture = nullptr; // alive until the next SetEffectsLocationGeometry
    };
    std::span<const Surface> surfaces;
    // The weather's sun and ambient are not this place's light; room_lo..room_hi is then the box one walks in, with
    // room for the walls and the ceiling: the room light and the AO volume cover it (the geometry may reach far
    // beyond and only occludes there).
    bool interior = false;
    std::array<float, 3> room_lo{}, room_hi{};
};
void SetEffectsLocationGeometry(const EffectsLocationGeometry &geometry);

// SHIP_LIGHTS, every frame at sea, after the ships have moved and before the first of them is drawn: the ships that
// carry lit lanterns and those lanterns (world space, where the ships are drawn in this frame, with this frame's
// flicker; not the flashes of the cannons). The nearest to the camera take the lamp slots of the lights that
// move (fx_lamp_moving). A lantern stands still on its ship, so the shadows of the ship itself (hull, masts, deck)
// are drawn once into faces in the ship's own space and stay while the ship sails; the crew, the sails and other
// ships cast every frame. A frame without the call has none.
struct EffectsShip
{
    uint64_t id = 0;               // names the ship while it lives (not 0)
    std::array<float, 16> world{}; // ship space -> absolute world (row vectors), rigid
    uint64_t signature = 0;        // changes when the ship's still geometry does (a mast falls, the lanterns change)
};
struct EffectsShipLight
{
    EffectsPointLight light;
    uint64_t ship = 0;             // an EffectsShip::id of the same call
    std::array<float, 3> local{};  // the lantern in the ship's space
};
void SetEffectsShipLights(std::span<const EffectsShip> ships, std::span<const EffectsShipLight> lights);
// True when one of the ship's lanterns holds a slot and the effects do not have the ship's still geometry of this
// signature: the ship then reports it (SetEffectsShipGeometry) before the frame draws.
bool EffectsShipGeometryWanted(uint64_t ship, uint64_t signature);
// The ship's still geometry in its own space (triangle list; uvs and surfaces as in EffectsLocationGeometry, the
// textures alive while the ship's lanterns are reported). Empty: it could not be read; the ship casts nothing still.
struct EffectsShipGeometry
{
    std::span<const std::array<float, 3>> positions;
    std::span<const std::array<float, 2>> uvs;
    std::span<const uint32_t> indices;
    std::span<const EffectsLocationGeometry::Surface> surfaces;
};
void SetEffectsShipGeometry(uint64_t ship, uint64_t signature, const EffectsShipGeometry &geometry);
// While one lives, the draws are the still geometry of this ship (what SetEffectsShipGeometry reports): they are in
// the cached faces of the ship's own lanterns and cast every frame only for other lights.
class EffectsShipDraw
{
  public:
    explicit EffectsShipDraw(uint64_t ship);
    ~EffectsShipDraw();
    EffectsShipDraw(const EffectsShipDraw &) = delete;
    EffectsShipDraw &operator=(const EffectsShipDraw &) = delete;
};


// Location, once an outdoor location is loaded: its lightmapped triangles (the still models with a shadow.tga
// lightmap: world positions, lightmap texture coordinates, triangle list, the lightmap of each triangle) and those
// lightmaps (one mip level of each, as stored: DXT1/3/5, and the game's texture of it). On Средняя / Высокая our own
// lightmaps take their place in the draws: the sun's shadows of the hour and the sky's occlusion from the place's
// still geometry (SetEffectsLocationGeometry), in the game lightmaps' tone (effects/town_light.hpp). Interiors and when
// leaving: empty.
struct EffectsLightmap
{
    uint32_t width = 0, height = 0;
    std::array<char, 4> format{}; // the .tx format tag: 'D','X','T','1' / '3' / '5'
    std::vector<uint8_t> data;
    shim::BaseTexture *texture = nullptr; // the game's texture of this lightmap (alive while the location is)
};
void SetEffectsLightmaps(std::string_view name, std::span<const std::array<float, 3>> positions,
                         std::span<const std::array<float, 2>> uvs, std::span<const uint32_t> indices,
                         std::span<const uint32_t> triangle_lightmap, std::span<const EffectsLightmap> lightmaps);

// The texture's name and the game file it was loaded from (Renderer::TextureLoad, after its levels are filled). Stone,
// wood, plaster and ground textures and grey height ("bump") textures get a normal map for the per-pixel lighting's
// relief (fx_light_normal_map, fx/normal_map.hpp, src/effects/relief_generator.hpp).
void SetEffectsTextureSource(shim::BaseTexture *texture, const char *path, const std::filesystem::path &file);

// Relief maps (fx/relief_store.hpp): made by DeepBump in the game process the first time a texture needs one, and the
// height maps the game derives; kept in `directory`. Before the first texture is loaded.
struct ReliefConfig
{
    std::filesystem::path directory;
    std::filesystem::path textures; // the game's RESOURCE/Textures (the generator reads the texture files)
    // Reads the DeepBump model (src/libs/relief/model). Null: no maps are made (logged); textures without one get no
    // relief.
    std::function<std::vector<uint8_t>()> model;
};
void ConfigureRelief(const ReliefConfig &config);
// Before the renderer goes: stops the generator (the texture in progress is abandoned, its map made next time).
void ShutdownRelief();
// Main thread, at start: removes the stores of normal maps earlier builds kept elsewhere (the files directory's
// "relief" before the relief maps moved to the cache, the "NormalMaps" cache before the relief directory), each if it
// is still there (logged with what it held). Never `live` (the relief directory in use), anything inside it or anything
// that holds it: such a location is refused and logged as an error.
void RemoveOldReliefStores(const std::vector<std::filesystem::path> &directories, const std::filesystem::path &live);

// ---- World passes: contact shadows ----
// The consumer entities (shadow, SUNGLOW) ask which parts are on and then either call the fx pass below
// or keep their legacy drawing. Changes only at the Present that switches settings (EffectsReport::pending);
// everything false in «Низкая».
struct EffectsWorld
{
    bool contact_shadows = false; // a soft blob under each character
    bool lens_flare = false;      // the post chain draws the sun glare: SUNGLOW leaves its glow, overflow and flare
                                  // sprites of the sun out and reports the sun's visibility (SetEffectsSunGlare)
    // An interior with lamp shadows: the characters' shadows come from the room's lamps; the shadow entity leaves
    // its projected shadow (cast along the outdoor sun) out.
    bool lamp_character_shadows = false;
};
EffectsWorld CurrentEffectsWorld();

// The world passes below draw into the current render target with the current D3D projection, in submission
// order. `view` is the world -> view matrix as the engine sees it (VRenderer::GetTransform(D3DTS_VIEW)): the
// device itself holds a camera-relative view, because Renderer moves the camera translation into the world matrix.
using EffectsMatrix = std::array<float, 16>;

// A contact shadow on the floor at `ground` (a character's feet), drawn at once. Call it after the floor is drawn.
// Only while CurrentEffectsWorld().contact_shadows.
void EffectsContactShadow(const std::array<float, 3> &ground, const EffectsMatrix &view);

} // namespace storm::render
