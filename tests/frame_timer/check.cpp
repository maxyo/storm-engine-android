#include "timer.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

// Game milliseconds handed out for `frames` frames of `frameMs` each.
static double simulate(double frameMs, int frames, double *exact = nullptr)
{
    TIMER timer;
    uint64_t total = 0;
    double real = 0;
    for (int i = 0; i < frames; ++i)
    {
        const uint32_t delta = timer.Advance(static_cast<float>(frameMs));
        assert(delta >= 1 && delta <= 100);
        assert(timer.rDelta_Time == delta && timer.Delta_Time == delta);
        assert(std::fabs(timer.fDeltaTime - std::fmin(frameMs, 100.0)) < 1e-3);
        total += delta;
        real += frameMs;
    }
    if (exact)
        *exact = real;
    return static_cast<double>(total);
}

int main()
{
    // 10 simulated seconds at common refresh rates: game time tracks real time.
    for (double hz : {30.0, 45.0, 60.0, 90.0, 120.0, 144.0, 240.0})
    {
        double real;
        const double game = simulate(1000.0 / hz, static_cast<int>(hz * 10), &real);
        assert(std::fabs(game - real) <= 1.5);
        // What plain truncation (the previous behaviour) would have produced.
        const double truncated = std::floor(1000.0 / hz) * hz * 10;
        std::printf("%5.0f Hz: real %.0f ms, game %.0f ms (truncation: %.0f ms, %+.1f%%)\n", hz, real, game, truncated,
                    (truncated - real) / real * 100.0);
    }
    // Integer frame times are unchanged.
    assert(simulate(20.0, 500) == 10000.0);
    // Above 1000 fps every frame still gets at least 1 ms (as before); never zero.
    assert(simulate(0.25, 4000) == 4000.0);
    // Stalls are clamped to 100 ms and not repaid afterwards.
    TIMER timer;
    assert(timer.Advance(5000.0f) == 100);
    assert(timer.Advance(16.0f) == 16);
    assert(timer.Advance(250.5f) == 100);
    assert(timer.Advance(16.0f) == 16);
    // Clock anomalies never produce a zero or huge delta.
    assert(timer.Advance(-5.0f) == 1);
    assert(timer.Advance(std::numeric_limits<float>::quiet_NaN()) == 1);
    assert(timer.Advance(std::numeric_limits<float>::infinity()) == 100);
    // Fixed delta mode returns the fixed value and keeps accounting.
    timer.SetDelta(33);
    assert(timer.Advance(16.7f) == 33 && timer.GetDeltaTime() == 33);
    timer.SetDelta(0);
    assert(timer.GetDeltaTime() == timer.Delta_Time);
    // FPS counter: one refresh per simulated second.
    TIMER fps;
    int rings = 0;
    for (int i = 0; i < 600; ++i)
    {
        fps.Advance(1000.0f / 60.0f);
        rings += fps.Ring;
    }
    assert((rings == 10 || rings == 9) && fps.fps >= 59 && fps.fps <= 61); // the 10th second may end one frame later
    std::puts("frame timer: carry/clamp/minimum PASS");
}
