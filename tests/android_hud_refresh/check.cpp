// The land command list's refresh policy (android_hud_refresh.hpp, android_land_commands.hpp).
#include "platform/android_land_commands.hpp"
#include <cassert>
#include <cstdio>

using namespace storm::android;

static LandRefreshSignal standing()
{
    LandRefreshSignal signal;
    signal.ready = true;
    signal.owner = 11;
    signal.player = 22;
    signal.location = 33;
    return signal;
}

int main()
{
    HudRefreshPolicy<LandRefreshSignal> policy{100};
    auto signal = standing();
    // The first poll always asks.
    assert(policy.due(1000, signal));
    // Nothing changed natively: the scripts are asked once per period, not per loop iteration.
    int asks = 0;
    for (uint32_t tick = 1001; tick < 2000; tick += 16)
        asks += policy.due(tick, signal);
    // About one ask per 100 ms (rounded up to the 16 ms loop): 8-10 in a second instead of 63.
    assert(asks >= 8 && asks <= 10);

    // Each native change signal asks in the same iteration, whatever the period says.
    const auto changed = [&](auto edit, uint32_t tick) {
        auto next = signal;
        edit(next);
        policy.due(tick, signal); // settle on the old signal
        const bool asked = policy.due(tick + 1, next);
        signal = next;
        return asked;
    };
    uint32_t t = 3000;
    assert(changed([](auto &s) { s.reload = "reload2_back"; }, t += 200)); // the player stepped into a door
    assert(changed([](auto &s) { s.reload.clear(); }, t += 200));            // and out of it
    assert(changed([](auto &s) { s.menuActive = true; }, t += 200));         // the native menu opened
    assert(changed([](auto &s) { s.ready = false; }, t += 200));             // a dialogue started
    assert(changed([](auto &s) { s.ready = true; }, t += 200));
    assert(changed([](auto &s) { s.location = 34; }, t += 200));             // another location
    assert(changed([](auto &s) { s.player = 23; }, t += 200));               // a loaded save
    assert(changed([](auto &s) { s.owner = 12; }, t += 200));

    // An unchanged signal right after an ask does not ask again.
    assert(policy.due(t += 200, signal));
    assert(!policy.due(t + 1, signal));
    assert(!policy.due(t + 99, signal));
    assert(policy.due(t + 100, signal));
    t += 100;

    // force(): the list was emptied natively (closeLandCommands); the next poll asks once.
    policy.force();
    assert(policy.due(t + 1, signal));
    assert(!policy.due(t + 2, signal));

    // SDL_GetTicks wraps after 49.7 days: unsigned distance keeps the period.
    HudRefreshPolicy<LandRefreshSignal> wrapping{100};
    assert(wrapping.due(0xffffffc0u, signal));
    assert(!wrapping.due(0xfffffff0u, signal));
    assert(wrapping.due(0x00000030u, signal));

    std::puts("android_hud_refresh: ok");
}
