// Caribbean Legend only: pushes the current scene's lighting preset (Resource/Lighting/*.json, chosen by the
// presets' locationId lists) at the game hour to the PBR renderer, once per frame (WEATHER::PushEffectsWeather).
#pragma once

namespace storm::weather
{
void PushPbrLighting(float hour);
}
