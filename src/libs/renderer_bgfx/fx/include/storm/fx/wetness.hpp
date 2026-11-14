// Rain wetness of the outdoor surfaces (Settings::wet_surfaces): how wet the place is, from the rain of the weather.
// The renderer side (src/effects/lighting.cpp, shaders/include/ff_wet.sh) makes the lit and baked surfaces wet by it
// where the rain reaches them (darker, glossier, the sky's reflection); with puddles (Высокая) the relief's hollows on
// flat ground fill with water.
#pragma once

namespace storm::fx
{

// How fast the surfaces follow the rain: dry to fully wet in kWetRiseSeconds under a full rain, fully wet to dry in
// kWetDrySeconds after it stopped (a lighter rain reaches its lower level sooner).
inline constexpr float kWetRiseSeconds = 45.0f;
inline constexpr float kWetDrySeconds = 240.0f;

class Wetness
{
  public:
    // A frame of the weather: `rain` 0..1 (render::EffectsWeather::rain), `seconds` since the previous weather frame.
    // The first weather frame after a gap (Interrupt: a location entered, an interior or a loading screen left) starts
    // at the rain's own level: the rain has been falling there all along.
    void Update(float rain, float seconds);
    // A frame without the weather (interiors, loading screens): nothing outdoors is drawn.
    void Interrupt() { fresh_ = true; }
    float Level() const { return level_; }

  private:
    float level_ = 0.0f;
    bool fresh_ = true;
};

} // namespace storm::fx
