// Host checks for src/libs/renderer_bgfx/src/shim/frame_overlap.hpp (when the game thread runs a frame ahead of the
// render thread) and src/libs/renderer_bgfx/src/shim/swap_interval.hpp: the swap interval of a frame rate on
// mocked display mode sets (60, 90, 120 Hz panels and their combinations).
#include "shim/frame_overlap.hpp"
#include "shim/swap_interval.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

using storm::shim::ChooseSwapInterval;
using storm::shim::DisplayInMode;
using storm::shim::SlowestSwapInterval;
using storm::shim::SwapPaced;
using storm::shim::SwapThreshold;

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

// Frames overlap only while game work + render work do not fit the swap interval.
void check_frame_overlap()
{
    using storm::shim::FrameOverlap;
    const double interval = 16.67;
    {
        // The owner's phone: 5 ms of game work, 3 ms of rendering at 60 fps. Never overlapped.
        FrameOverlap overlap;
        for (int i = 0; i < 1000; ++i)
            expect(!overlap.update(5.0, 3.0, interval) && !overlap.overlapped(), "frames that fit stay serial");
        // One long frame (a load hitch) does not switch.
        overlap.update(60.0, 3.0, interval);
        expect(!overlap.overlapped(), "a single long frame keeps the frames serial");
        overlap.update(5.0, 3.0, interval);
        expect(!overlap.overlapped(), "and so does the frame after it");
    }
    {
        // A slow phone: 12 ms + 9 ms do not fit 16.67 ms, each fits alone.
        FrameOverlap overlap;
        int frames = 0;
        while (!overlap.overlapped() && frames < 100)
        {
            overlap.update(12.0, 9.0, interval);
            ++frames;
        }
        expect(frames == 2, "two frames in a row that do not fit overlap the next ones");
        // The scene gets light: back to serial only after a calm stretch.
        for (int i = 0; i < FrameOverlap::kCalmFrames - 1; ++i)
            overlap.update(4.0, 3.0, interval);
        expect(overlap.overlapped(), "a short calm stretch keeps the overlap");
        for (int i = 0; i < 20; ++i)
            overlap.update(4.0, 3.0, interval);
        expect(!overlap.overlapped(), "a long calm stretch ends the overlap");
    }
    {
        // A load just under the interval (15 of 16.67 ms) still overlaps: no margin for the present.
        FrameOverlap overlap;
        for (int i = 0; i < 60; ++i)
            overlap.update(9.0, 6.0, interval);
        expect(overlap.overlapped(), "a load of 90% of the interval overlaps");
        // Around the leave threshold with peaks: stays overlapped, no flapping.
        int changes = 0;
        for (int i = 0; i < 2000; ++i)
            changes += overlap.update(i % 50 == 0 ? 14.0 : 5.0, 4.0, interval);
        expect(changes == 0 && overlap.overlapped(), "regular peaks above the calm limit keep the overlap");
    }
    {
        // No known interval (desktop): overlapped at once.
        FrameOverlap overlap;
        expect(overlap.update(1.0, 1.0, 0.0) && overlap.overlapped(), "without a frame rate frames overlap");
    }
}

int main()
{
    check_frame_overlap();
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

    // The setting is a cap: never faster than it, also on modes it does not divide.
    expect_choice({hz(60)}, 60, 40, 60, 2, "40 fps on a 60 Hz panel: 30 fps, not 60");
    expect_choice({hz(60)}, 60, 45, 60, 2, "45 fps on a 60 Hz panel: 30 fps");
    expect_choice({hz(90)}, 90, 60, 90, 2, "60 fps on a 90 Hz-only panel: 45 fps");
    expect_choice(phone, 60, 40, 120, 3, "40 fps on 60/90/120 Hz: three 120 Hz refreshes");

    // The Xiaomi 15 Ultra at 60 Hz, 40 fps (perf audit 2026-11-14): it lists a 40 Hz mode and stays at 60 Hz.
    {
        const std::vector<uint64_t> xiaomi = {hz(60), hz(40), hz(90), hz(120)};
        const auto chosen = ChooseSwapInterval(xiaomi, hz(60), hz(40));
        expect(chosen.period == hz(40) && chosen.refreshes == 1, "40 fps: the 40 Hz mode, the slower exact one");
        expect(!DisplayInMode(chosen.period, hz(60)), "the display at 60 Hz is not in the chosen 40 Hz mode");
        expect(DisplayInMode(chosen.period, hz(40.3)) && DisplayInMode(hz(60), hz(59.94)),
               "within 2 % the display is in the mode");
        expect(!DisplayInMode(0, hz(60)) && !DisplayInMode(hz(60), 0), "an unknown period is no match");
        // Chosen again with the display's own period: 30 fps, paced (the old choice left Swappy unpaced: its 33.33 ms
        // interval was above the 25 ms threshold).
        const std::vector<uint64_t> stayed = {hz(60)};
        const auto again = ChooseSwapInterval(stayed, hz(60), hz(40));
        expect(again.period == hz(60) && again.refreshes == 2, "re-chosen at 60 Hz: two refreshes");
        const uint64_t slowest = SlowestSwapInterval(again, hz(30));
        const uint64_t threshold = SwapThreshold(again, slowest);
        expect(SwapPaced(again.interval(), threshold), "the re-chosen interval is paced");
        const uint64_t old_threshold = SlowestSwapInterval(chosen, hz(30)); // before: the threshold was the slowest
        expect(!SwapPaced(again.interval(), old_threshold), "the old threshold left 33.33 ms unpaced");
    }

    // The threshold paces the slowest interval, also with a measured period a little longer than nominal, and not the
    // step Swappy's ladder takes past it.
    {
        const uint64_t measured = hz(120) + 70; // a refresh period measured 70 ns long
        const std::vector<uint64_t> modes = {measured};
        const auto at60 = ChooseSwapInterval(modes, measured, hz(60));
        const uint64_t slowest = SlowestSwapInterval(at60, hz(30));
        const uint64_t threshold = SwapThreshold(at60, slowest);
        expect(slowest == 4 * measured && SwapPaced(4 * measured, threshold), "four 120 Hz refreshes are paced");
        expect(!SwapPaced(slowest, hz(30)), "a bare 33.33 ms threshold would not pace them");
        expect(!SwapPaced(5 * measured, threshold), "the step past the slowest is unpaced (logged so)");
        const auto at30 = ChooseSwapInterval(modes, measured, hz(30));
        expect(SwapPaced(at30.interval(), SwapThreshold(at30, SlowestSwapInterval(at30, hz(30)))) &&
                   !SwapPaced(at30.interval() + measured, SwapThreshold(at30, SlowestSwapInterval(at30, hz(30)))),
               "30 fps: paced, one refresh more is not");
        const std::vector<uint64_t> sixty = {hz(60)};
        const auto at60hz = ChooseSwapInterval(sixty, hz(60), hz(60));
        const uint64_t t60 = SwapThreshold(at60hz, SlowestSwapInterval(at60hz, hz(30)));
        expect(SwapPaced(2 * hz(60), t60) && !SwapPaced(3 * hz(60), t60), "60 Hz: 30 fps paced, 20 fps unpaced");
    }
    std::puts("PASS: frame overlap choice; frame pacing swap interval (60/90/120 Hz display mode sets, the setting as a "
              "cap, a display that stays out of the chosen mode, the pacing threshold)");
    return 0;
}
