#include "platform/android_hud_presenter.hpp"
#include <cassert>
#include <iostream>
using namespace storm::android;

int main()
{
    // Sea under the touch HUD: the bars over the ships and the target pointer are part of the
    // scene and show; the sea HUD's own panels are the touch HUD's.
    assert(hudPresenters.declare("sea"));
    assert(!legacyDrawRefused("ship bars", hudPresenters.sea));
    assert(!legacyDrawRefused("ship pointer", hudPresenters.sea));
    assert(legacyDrawRefused("sea interface", hudPresenters.sea) && legacyDrawRefused("sea navigator", hudPresenters.sea) &&
           legacyDrawRefused("sea ship cards", hudPresenters.sea) && legacyDrawRefused("active perk icons"));
    assert(legacyDrawRefused("not a layer", true)); // unclassified: refused, reported as an error
    // Without the touch HUD every legacy layer draws.
    hudPresenters.lost();
    assert(!legacyDrawRefused("ship bars", hudPresenters.sea) && !legacyDrawRefused("sea interface", hudPresenters.sea));
    for (const auto &layer : kLegacyLayers)
        assert(legacyLayer(layer.name) == &layer);
    std::cout << "PASS hud layers: world-space ship bars and pointer shown at sea under the touch HUD, HUD panels refused, unclassified refused\n";
}
