// Procedural test content for the fx preview: meshes, a synthetic stand-in for
// the engine's sea height frames and the weather set-ups. Nothing is loaded
// from game data.
#pragma once

#include "storm/fx/light_set.hpp"
#include "storm/fx/sky_model.hpp"
#include "storm/fx/wave_model.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace preview
{

struct Vertex
{
    float pos[3];
    float normal[3];
    uint32_t abgr;               // surface colour: the role the texture plays in the game
    uint32_t baked = 0xffffffffu; // rgb: lighting baked into the vertex colours of static geometry (white = none);
                                  // alpha: 255 * (1 - wind sway weight)
    float uv[2] = {0.0f, 0.0f};   // coordinates of the detail / height texture (the game's second UV set)
};

struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    void Append(const Mesh &other);
};

using Color = std::array<float, 3>; // gamma-encoded 0..1, as the game's vertex colours

struct Ship
{
    Mesh hull;  // hull, deck, masts and spars
    Mesh sails; // thin two-sided canvas
};
Ship MakeShip(std::array<float, 3> origin, float heading);
Mesh MakeIsland(std::array<float, 3> centre, float radius, float height);
Mesh MakeFigure(std::array<float, 3> feet); // a "character": legs, torso, head, arms
Mesh MakeSphere(std::array<float, 3> centre, float radius, Color color, int rings = 24);
Mesh MakeBox(std::array<float, 3> centre, std::array<float, 3> half, Color color);
// Night harbour: a quay with houses, torches on the walls and lanterns on posts. The static geometry carries
// baked lighting from those lights, as location models do after the lighter tool.
struct Harbour
{
    Mesh town;                              // baked geometry
    Mesh glow;                              // flames, lantern glasses, lit windows (unlit, bright)
    std::vector<storm::fx::PointLight> lights; // the location's light sources (baked = true)
};
Harbour MakeHarbour();
// Writes Vertex::baked: night ambient plus the fixed-function sum of `lights`, as the lighter tool would.
void BakeLighting(Mesh &mesh, const std::vector<storm::fx::PointLight> &lights, Color ambient);
// Thin slanted bars, fan lines and a fine checker: content that aliases badly.
Mesh MakeAliasingPattern();

// Vegetation and rigging: what the game draws alpha tested. `foliage` and `ropes` use the textures below;
// their vertices carry a wind sway weight (0 at the trunk, the ground or a rope's ends).
struct Garden
{
    Mesh ground;
    Mesh trunks;  // palm trunks and the two posts
    Mesh foliage; // palm fronds and grass tufts
    Mesh ropes;   // lines between the posts
};
Garden MakeGarden();
// RGBA textures, size * size: a palm frond (leaflets along a rib) and a rope (opaque strand on a clear ground).
std::vector<uint8_t> MakeFrondTexture(int size);
std::vector<uint8_t> MakeRopeTexture(int size);

// Surfaces that carry a height ("bump") texture in the game, each with texture coordinates: a house wall,
// a boulder, a plank deck and a sail.
enum class Relief
{
    Wall,
    Rock,
    Deck,
    Sail
};
Mesh MakeReliefMesh(Relief kind);
// Procedural stand-in for the game's height texture of that surface: size * size grey values.
std::vector<uint8_t> MakeHeightMap(Relief kind, int size);

// Sea state resembling a fresh breeze in the game (two layers, values in the range of the Sea2 weather tables).
storm::fx::EngineSeaState MakeSeaState(float time);
// Fills both layers with tiling synthetic swell; `time` animates it like the engine's frame blend.
void FillWaveField(storm::fx::EngineWaveField &field, float time);

enum class Weather
{
    Day,
    Sunset,
    Storm,
    Night
};
// `sun_turn` rotates the sun around the vertical axis (degrees), e.g. to light a scene from behind the camera.
storm::fx::WeatherInput MakeWeather(Weather weather, float sun_turn = 0.0f);

} // namespace preview
