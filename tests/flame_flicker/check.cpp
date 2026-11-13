// The flames' flicker at time scales 1, 2, 4 and 8 and in a pause. A frame takes `real` milliseconds; the game's
// delta is that times the scale (core_impl.cpp), the flicker is stepped by storm::CosmeticDeltaSeconds as the
// ship lanterns (ship_lights.cpp) and the location lights (lights.cpp) step it.
#include "storm/cosmetic_time.hpp"

#include "location/src/flame_flicker.hpp"
#include "ship/src/flame_flicker.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>

namespace
{
int failures = 0;
void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

struct Stats
{
    double mean = 0, deviation = 0, step = 0; // intensity mean, its deviation, mean change per frame
};

// `frame(seconds)` returns the flame's intensity after a frame of `seconds` of flicker time.
Stats run(const std::function<float(float)> &frame, uint32_t real_ms, float scale, bool flicker_on_game_time)
{
    std::srand(1);
    const uint32_t game_ms = static_cast<uint32_t>(static_cast<float>(real_ms) * scale);
    const float seconds = flicker_on_game_time ? static_cast<float>(game_ms) * 0.001f : storm::CosmeticDeltaSeconds(game_ms, real_ms);
    constexpr int kFrames = 20000;
    double sum = 0, squares = 0, steps = 0, last = frame(seconds);
    for (int i = 0; i < kFrames; ++i)
    {
        const double v = frame(seconds);
        sum += v;
        squares += v * v;
        steps += std::fabs(v - last);
        last = v;
    }
    Stats s;
    s.mean = sum / kFrames;
    s.deviation = std::sqrt(std::max(0.0, squares / kFrames - s.mean * s.mean));
    s.step = steps / kFrames;
    return s;
}

bool same(const Stats &a, const Stats &b)
{
    return a.mean == b.mean && a.deviation == b.deviation && a.step == b.step;
}

void flame(const char *name, const std::function<std::function<float(float)>()> &make)
{
    std::printf("%s (60 fps):\n", name);
    const Stats base = run(make(), 16, 1.0f, false);
    for (float scale : {1.0f, 2.0f, 4.0f, 8.0f})
    {
        const Stats s = run(make(), 16, scale, false);
        std::printf("  x%g: mean %.4f deviation %.4f change per frame %.4f\n", scale, s.mean, s.deviation, s.step);
        check(same(s, base), "the flicker is the same at every time scale");
        // What the game time would do: at x8 the flame jumps (the bug this guards against).
        if (scale == 8.0f)
        {
            const Stats game = run(make(), 16, scale, true);
            std::printf("  x8 on game time (for comparison): deviation %.4f change per frame %.4f\n", game.deviation, game.step);
            check(game.step > 2.0 * base.step, "the check tells the game time apart");
        }
    }
    check(base.deviation > 0.01 && base.step > 0.0, "the flame flickers");
    // A pause (time scale 0) stops it.
    const Stats paused = run(make(), 16, 0.0f, false);
    check(paused.deviation == 0.0 && paused.step == 0.0, "a pause stops the flicker");
}
} // namespace

int main()
{
    // The game's default ship lantern (PROGRAM/sea_ai/ShipLights.c: Oscillator1 0.15 at 5 Hz, Oscillator2 0.1 at 1 Hz).
    flame("ship lantern", [] {
        struct Lantern
        {
            storm::ship::FlickerOscillator osc[2];
        };
        auto lantern = std::make_shared<Lantern>();
        const float amp[2] = {0.15f, 0.1f}, freq[2] = {5.0f, 1.0f};
        for (int i = 0; i < 2; ++i)
            lantern->osc[i] = {freq[i], amp[i], 1.0f / amp[i], 0.0f, 0.0f, 0.0f};
        return std::function<float(float)>([lantern](float seconds) { return storm::ship::StepFlameFlicker(lantern->osc, seconds); });
    });
    // A location lamp (fast 0.1 at 10 Hz, slow 0.2 at 1 Hz).
    flame("location lamp", [] {
        auto state = std::make_shared<storm::location::FlameFlicker>();
        const storm::location::FlameFlickerType type{0.1f, 1.0f / 10.0f, 0.2f, 1.0f, 1.0f};
        return std::function<float(float)>(
            [state, type](float seconds) { return storm::location::StepFlameFlicker(*state, type, seconds); });
    });

    // A hitch (one frame of 0.35 s, several fast periods) starts one new value, not one on every following frame.
    {
        storm::location::FlameFlicker state{};
        const storm::location::FlameFlickerType type{0.1f, 1.0f / 10.0f, 0.2f, 1.0f, 1.0f};
        storm::location::StepFlameFlicker(state, type, 0.35f);
        check(state.time <= type.period && state.timeSlow <= type.periodSlow, "a long frame leaves the flame within its periods");
        const float itens = state.itens;
        storm::location::StepFlameFlicker(state, type, 0.016f);
        check(state.itens == itens, "the frame after a hitch keeps the fast value");
    }
    // Only a slow flicker (no fast period): no division by a missing period.
    {
        storm::location::FlameFlicker state{};
        const storm::location::FlameFlickerType type{0.0f, 0.0f, 0.2f, 1.0f, 1.0f};
        for (int i = 0; i < 200; ++i)
            check(std::isfinite(storm::location::StepFlameFlicker(state, type, 0.016f)), "a slow-only flame stays finite");
    }

    if (failures)
        return 1;
    std::printf("PASS flame flicker: same at x1..x8 time scale, still in a pause, robust to a frame hitch\n");
    return 0;
}
