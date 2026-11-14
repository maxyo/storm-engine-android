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
    // The deck fight's strength bars show until the touch HUD has its own.
    assert(hudPresenters.declare("land") && !legacyDrawRefused("boarding status"));
    assert(legacyDrawRefused("land interface", hudPresenters.land));
    assert(legacyDrawRefused("not a layer", true)); // unclassified: refused, reported as an error
    // World map under the touch HUD: the game's instruments and fleet are the touch HUD's, by the
    // table alone (no frame needed), so they stay off under a game interface over the map too:
    // the encounter signal's card showed them through when only the frame's coverage hid them.
    assert(!legacyDrawRefused("world map instruments", hudPresenters.worldMap) && !legacyDrawRefused("world map fleet", hudPresenters.worldMap));
    assert(hudPresenters.declare("worldmap"));
    assert(legacyDrawRefused("world map instruments", hudPresenters.worldMap) && legacyDrawRefused("world map fleet", hudPresenters.worldMap));
    // Without the touch HUD every legacy layer draws.
    hudPresenters.lost();
    assert(!legacyDrawRefused("ship bars", hudPresenters.sea) && !legacyDrawRefused("sea interface", hudPresenters.sea));
    // The watchdog counts frame time. A revision changes in the last frame before the app goes to
    // the background for 15 s; the first frame after the resume is still waiting (the UI has had no
    // frame to answer in): the presenter stays, and the UI's answer in the next frame ends the wait.
    assert(hudPresenters.declare("sea"));
    uint32_t tick = 100000;
    assert(!hudPresenters.watch("sea", true, tick));
    tick += 15000;
    assert(!hudPresenters.watch("sea", true, tick) && hudPresenters.sea && legacyDrawRefused("sea interface", hudPresenters.sea));
    assert(!hudPresenters.watch("sea", false, tick += 16) && hudPresenters.sea);
    // Pauses never add up: a revision outstanding over several 15 s backgrounds keeps the presenter.
    for (int pause = 0; pause < 20; ++pause)
        assert(!hudPresenters.watch("sea", true, tick += 15000) && hudPresenters.sea);
    hudPresenters.watch("sea", false, tick);
    // A real stall, frames running and the UI silent: withdrawn after kSilentMs of frame time, at
    // 60 fps and at a slow 5 fps (each long frame counts kFrameMs).
    for (const uint32_t frameMs : {16u, 200u})
    {
        assert(hudPresenters.declare("sea"));
        const uint32_t frames = (HudPresenters::kSilentMs + std::min(frameMs, HudPresenters::kFrameMs) - 1) / std::min(frameMs, HudPresenters::kFrameMs);
        std::optional<std::string> line;
        uint32_t ran = 0;
        for (; !line && ran <= frames + 1; ++ran) line = hudPresenters.watch("sea", true, tick += frameMs);
        assert(line && ran == frames + 1 && !hudPresenters.sea && !legacyDrawRefused("sea interface", hudPresenters.sea));
        assert(line->find("HUD presenter sea withdrawn") != std::string::npos && line->find("frame time") != std::string::npos);
    }
    // After a stall the UI comes back: the presenter returns once it answers and the current
    // revision is answered, loudly; until then the legacy HUD draws.
    assert(!hudPresenters.sea && !hudPresenters.watch("sea", true, tick += 16) && !hudPresenters.sea); // still silent
    assert(!hudPresenters.watch("sea", false, tick += 16) && !hudPresenters.sea); // no revision waits, but no answer either
    hudPresenters.answered("sea");                                                // the UI answers an old revision...
    assert(!hudPresenters.watch("sea", true, tick += 16) && !hudPresenters.sea);  // ...a newer one is unanswered: wait for it
    assert(!hudPresenters.watch("sea", false, tick += 16) && !hudPresenters.sea); // the earlier answer does not count
    hudPresenters.answered("sea");
    {
        const auto back = hudPresenters.watch("sea", false, tick += 16);
        assert(back && back->find("HUD presenter sea back") != std::string::npos && hudPresenters.sea);
        assert(legacyDrawRefused("sea interface", hudPresenters.sea));
    }
    // Steady state: revisions come and are answered a frame later; the presenter never leaves,
    // no line is logged and the legacy HUD never draws, for a minute of frames.
    for (int frame = 0; frame < 3600; ++frame)
    {
        const bool waiting = frame % 3 != 2;
        if (!waiting) hudPresenters.answered("sea");
        assert(!hudPresenters.watch("sea", waiting, tick += 16) && hudPresenters.sea && legacyDrawRefused("sea interface", hudPresenters.sea));
    }
    // Stall again: withdrawn again, back again. A runtime failure is final: no answer brings it back.
    for (uint32_t i = 0; i <= HudPresenters::kSilentMs / 16; ++i) hudPresenters.watch("sea", true, tick += 16);
    assert(!hudPresenters.sea);
    hudPresenters.answered("sea");
    assert(hudPresenters.watch("sea", false, tick += 16) && hudPresenters.sea);
    assert(!hudPresenters.lost().empty() && !hudPresenters.sea);
    hudPresenters.answered("sea");
    assert(!hudPresenters.watch("sea", false, tick += 16) && !hudPresenters.sea);
    // Each HUD has its own watch: an answer at sea does not bring the world map back.
    assert(hudPresenters.declare("worldmap"));
    for (uint32_t i = 0; i <= HudPresenters::kSilentMs / 16; ++i) hudPresenters.watch("worldmap", true, tick += 16);
    assert(!hudPresenters.worldMap);
    hudPresenters.answered("sea");
    assert(!hudPresenters.watch("worldmap", false, tick += 16) && !hudPresenters.worldMap);
    hudPresenters.answered("worldmap");
    assert(hudPresenters.watch("worldmap", false, tick += 16) && hudPresenters.worldMap);
    hudPresenters.lost();
    for (const auto &layer : kLegacyLayers)
        assert(legacyLayer(layer.name) == &layer);
    std::cout << "PASS hud layers: world-space ship bars and pointer shown at sea under the touch HUD, HUD panels refused (world map instruments and fleet too), unclassified refused; the presenter watchdog counts frame time (15 s backgrounds never withdraw, a stall with frames does; the UI's answer brings it back, no flicker in steady state, a runtime failure is final)\n";
}
