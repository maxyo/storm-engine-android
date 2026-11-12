// Host checks for src/libs/renderer_bgfx/src/shim/swap_interval.hpp: the swap interval of a frame rate on
// mocked display mode sets (60, 90, 120 Hz panels and their combinations).
#include "shim/swap_interval.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

using storm::shim::ChooseSwapInterval;
using storm::shim::SlowestSwapInterval;

namespace
{
constexpr uint64_t hz(double rate)
{
    return static_cast<uint64_t>(1e9 / rate + 0.5);
}

void expect(bool condition, const char *what)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}

void expect_choice(std::vector<uint64_t> modes, double current, double fps, double mode, uint64_t refreshes, const char *what)
{
    const auto choice = ChooseSwapInterval(modes, hz(current), hz(fps));
    if (choice.period != hz(mode) || choice.refreshes != refreshes)
    {
        std::fprintf(stderr, "FAIL: %s: got %.2f ms x %llu\n", what, choice.period / 1e6,
                     static_cast<unsigned long long>(choice.refreshes));
        std::exit(1);
    }
}
} // namespace

int main()
{
    const std::vector<uint64_t> phone = {hz(60), hz(90), hz(120)}; // e.g. Xiaomi 15 Ultra
    expect_choice(phone, 120, 60, 120, 2, "60 fps on a 120 Hz panel in its 120 Hz mode: every second refresh, no switch");
    expect_choice(phone, 60, 60, 60, 1, "60 fps in the 60 Hz mode stays there");
    expect_choice(phone, 90, 60, 60, 1, "60 fps from the 90 Hz mode: the exact 60 Hz mode, the slower of the exact ones");
    expect_choice(phone, 120, 120, 120, 1, "120 fps: the 120 Hz mode");
    expect_choice(phone, 60, 120, 120, 1, "120 fps from the 60 Hz mode: switches to 120 Hz");
    expect_choice(phone, 120, 90, 90, 1, "90 fps: the 90 Hz mode");
    expect_choice(phone, 120, 30, 120, 4, "30 fps on 120 Hz: every fourth refresh, no switch");
    expect_choice({hz(60)}, 60, 30, 60, 2, "30 fps on a 60 Hz panel (the emulator): every second refresh");
    expect_choice({hz(60)}, 60, 60, 60, 1, "60 fps on a 60 Hz panel");
    expect_choice({hz(90)}, 90, 30, 90, 3, "30 fps on a 90 Hz-only panel");
    expect_choice({hz(59.94)}, 59.94, 60, 59.94, 1, "a 59.94 Hz panel counts as 60");
    expect(ChooseSwapInterval(std::vector<uint64_t>{0}, 0, hz(60)).period == 0, "no usable period: no choice");

    const auto at60 = ChooseSwapInterval(phone, hz(120), hz(60));
    expect(SlowestSwapInterval(at60, hz(30)) == 4 * hz(120), "60 fps falls back to 30 fps in whole 120 Hz refreshes");
    const auto at30 = ChooseSwapInterval(phone, hz(120), hz(30));
    expect(SlowestSwapInterval(at30, hz(30)) == at30.interval(), "30 fps has no slower fallback");
    std::puts("PASS: frame pacing swap interval (60/90/120 Hz display mode sets)");
    return 0;
}
