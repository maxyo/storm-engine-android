// Procedural test content for the fx preview: meshes, a synthetic stand-in for
// the engine's sea height frames and the weather set-ups. Nothing is loaded
// from game data.
#pragma once

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
    uint32_t abgr;
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
Mesh MakeGround(float half, float y, Color color);
// Thin slanted bars, fan lines and a fine checker: content that aliases badly.
Mesh MakeAliasingPattern();

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
