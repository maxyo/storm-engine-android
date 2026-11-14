#include "auto_preset.hpp"

#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace storm::render;

namespace
{
// Present intervals of a rung: the n-th frame's interval in ms.
using Pacing = std::function<float(int rung, size_t frame)>;
// GPU time of a rung, < 0: no timer queries.
using Gpu = std::function<double(int rung)>;

struct Run
{
    AutoPresetBenchmark benchmark;
    double now = 0.0;
    int rung = kAutoPresetFirstRung; // what the renderer draws
    int pending_frames = 0;          // frames the switch stays pending
    std::vector<int> applied;        // every rung the machine asked for
    size_t frame_index = 0;

    // Frames until the machine ends or `limit` seconds pass; `title(now)` says whether the title is shown, `busy(now)`
    // whether held background work is still finishing.
    AutoPresetBenchmark::Step drive(const Pacing &pacing, double limit, const std::function<bool(double)> &title = {},
                                    const Gpu &gpu = {}, const std::function<bool(double)> &busy = {})
    {
        AutoPresetBenchmark::Step step;
        const double end = now + limit;
        while (now < end)
        {
            const float interval = pacing(rung, frame_index++);
            now += interval / 1000.0;
            const std::vector<float> intervals{interval};
            AutoPresetBenchmark::Frame frame;
            frame.now_s = now;
            frame.title = title ? title(now) : true;
            frame.effects_ready = pending_frames == 0;
            frame.background_busy = busy ? busy(now) : false;
            frame.intervals_ms = intervals;
            const double gpu_ms = gpu ? gpu(rung) : -1.0;
            frame.gpu_ms = gpu_ms >= 0.0 ? gpu_ms : -1.0;
            frame.gpu_frames = gpu_ms >= 0.0 ? 1 : 0;
            if (pending_frames > 0)
                --pending_frames;
            step = benchmark.frame(frame);
            if (step.apply_rung >= 0)
            {
                applied.push_back(step.apply_rung);
                if (step.apply_rung != rung)
                    pending_frames = 5; // programs link over a few frames
                rung = step.apply_rung;
            }
            if (step.finished || step.aborted)
                return step;
        }
        return step;
    }
};

// A device whose rung r takes work_ms[r] per frame; presents are whole refreshes of the 60 fps the measurement paces
// at, with ±0.6 ms jitter.
Pacing Device(std::vector<float> work_ms)
{
    return [work_ms](int rung, size_t frame) {
        const double refresh = 1000.0 / kAutoPresetPacingFps;
        const double shown = std::ceil(work_ms[static_cast<size_t>(rung)] / refresh - 1e-6) * refresh;
        return static_cast<float>(shown + ((frame % 7) * 0.2 - 0.6));
    };
}

Gpu Timers(std::vector<double> gpu_ms)
{
    return [gpu_ms](int rung) { return gpu_ms[static_cast<size_t>(rung)]; };
}

// A device that misses a frame (one refresh more) every `miss_every` frames.
Pacing Stutter(float base_ms, size_t miss_every)
{
    return [=](int, size_t frame) { return frame % miss_every == 0 ? base_ms + 1000.0f / 60.0f : base_ms; };
}

// Rungs: 0 Низкая at 720 lines, 1 Низкая, 2 Средняя, 3 Высокая.
void ExpectChoice(const std::vector<float> &work, const Gpu &gpu, int chosen, std::vector<int> measured, const char *what)
{
    Run run;
    const auto step = run.drive(Device(work), 90.0, {}, gpu);
    std::vector<int> rungs;
    for (const auto &r : run.benchmark.results())
        rungs.push_back(r.rung);
    if (!step.finished || run.benchmark.chosen() != chosen || rungs != measured)
    {
        std::cerr << "FAIL " << what << ": finished " << step.finished << " chosen " << run.benchmark.chosen() << "\n"
                  << run.benchmark.report() << "\n";
        std::exit(1);
    }
    // The last thing the machine asks for is the choice; nothing more follows.
    assert(run.applied.back() == chosen);
    assert(run.benchmark.phase() == AutoPresetBenchmark::Phase::Done);
    assert(!run.benchmark.frame({run.now + 0.016, true, true, false, {}, -1.0, 0}).finished);
}
} // namespace

int main()
{
    // Percentiles as frame pacing logs them.
    assert(AutoPresetPercentile({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 50) == 5);
    assert(AutoPresetPercentile({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 95) == 10);
    assert(AutoPresetPercentile({}, 95) == 0);
    assert(std::abs(AutoPresetLimitMs() - (1000.0 / 30.0 + kAutoPresetJitterMs)) < 1e-9);
    assert(std::abs(AutoPresetHeadroomLimitMs() - (1000.0 / 30.0 / kAutoPresetHeadroom + kAutoPresetJitterMs)) < 1e-9);
    assert(kAutoPresetRungs[0].preset == 0 && kAutoPresetRungs[0].render_height == 720 &&
           kAutoPresetRungs[kAutoPresetFirstRung].preset == 0 && kAutoPresetRungs[kAutoPresetFirstRung].render_height == 0);

    // The Test Lab devices (title scene, engine of 2026-11-13, GLES): frame time from the fps, GPU time per rung; the
    // 720 rung's GPU time is Низкая's x 0.64 (the pixels of 720 against 900 lines).
    // S23: Высокая held 29.9 fps (p95 35.0) and was kept; its GPU time x 1.5 (46.5 ms) leaves no headroom: Средняя.
    ExpectChoice({10, 16.7f, 25.0f, 33.4f}, Timers({8.1, 12.7, 21.6, 31.0}), 2, {1, 2, 3}, "Galaxy S23 (Adreno 740)");
    // S21: Низкая's GPU time 23.6 x 1.5 = 35.4 ms over the budget: the 720 rung.
    ExpectChoice({16, 24.9f, 73.5f, 119}, Timers({15.1, 23.6, 75.7, 118}), 0, {1, 0}, "Galaxy S21 (Adreno 660)");
    // A56 (measured during relief generation then): Низкая 22.2 ms GPU x 1.5 just fits; Средняя does not.
    ExpectChoice({18, 27.3f, 49.8f, 78}, Timers({14.2, 22.2, 46.6, 75.7}), 1, {1, 2}, "Galaxy A56 (Xclipse 540)");
    // Without timers the intervals carry the headroom: Pixel 8 Низкая at 35 fps misses it; the 720 rung is kept
    // although it misses too (nothing is lower).
    ExpectChoice({18.3f, 28.6f, 48.3f, 55.6f}, {}, 0, {1, 0}, "Pixel 8 (Mali-G715, no timers)");
    ExpectChoice({8, 10, 14, 30}, {}, 2, {1, 2, 3}, "no timers: Высокая at 30 fps has no headroom");
    ExpectChoice({6, 8, 12, 15}, {}, 3, {1, 2, 3}, "no timers: 60 fps on every rung");
    ExpectChoice({6, 8, 12, 15}, Timers({4, 6, 9, 12}), 3, {1, 2, 3}, "fast phone with timers");
    ExpectChoice({10, 14, 25, 20}, Timers({6, 9, 25, 12}), 1, {1, 2}, "a cheaper-looking Высокая after a failing Средняя is not taken");
    ExpectChoice({30, 45, 60, 90}, Timers({25, 40, 60, 90}), 0, {1, 0}, "nothing holds: the 720 rung, the lowest");
    // The CPU side counts with timers too: a light GPU at 22 fps fails.
    ExpectChoice({30, 45, 60, 90}, Timers({4, 5, 6, 7}), 0, {1, 0}, "GPU idle, frames late: p95 decides");

    // Misses with timers: 1 in 25 frames late is within p95, 1 in 10 is not.
    {
        Run run;
        assert(run.drive(Stutter(33.3f, 25), 90.0, {}, Timers({5, 5, 5, 5})).finished && run.benchmark.chosen() == 3);
        Run late;
        assert(late.drive(Stutter(33.3f, 10), 90.0, {}, Timers({5, 5, 5, 5})).finished && late.benchmark.chosen() == 0);
        assert(!late.benchmark.results()[0].passes && late.benchmark.results()[0].p95_ms > AutoPresetLimitMs());
    }

    // A rung slower than 1 fps cannot fill a measurement: it fails, whatever its few intervals say.
    {
        const auto result = MeasureAutoPreset(1, {30.0f, 30.0f, 30.0f}, kAutoPresetMaxMeasureSeconds, -1.0);
        assert(!result.passes && result.frames == 3);
        Run run;
        const auto step = run.drive([](int, size_t) { return 900.0f; }, 120.0);
        assert(step.aborted || (step.finished && run.benchmark.chosen() == 0));
    }

    // The measurement waits for kAutoPresetTitleSeconds of a steady title; a flicker of another screen restarts it.
    {
        Run run;
        const auto title = [](double now) { return now < 2.0 || now > 2.5; };
        run.drive(Device({8, 8, 8, 8}), 5.0, title);
        assert(run.applied.empty()); // 2.5 + 3 s not reached yet
        run.drive(Device({8, 8, 8, 8}), 1.0, title);
        assert(run.applied.size() == 1 && run.applied[0] == kAutoPresetFirstRung);
    }

    // Background work still finishing (a relief map started before the hold) delays the start; the hold covers the
    // wait on the title and the measurement, not other screens.
    {
        Run run;
        assert(run.benchmark.holds_background(true) && !run.benchmark.holds_background(false));
        run.drive(Device({8, 8, 8, 8}), 6.0, {}, {}, [](double now) { return now < 4.0; });
        assert(run.applied.empty()); // busy until 4 s, then 3 s of title
        run.drive(Device({8, 8, 8, 8}), 1.5, {}, {}, [](double now) { return now < 4.0; });
        assert(run.applied.size() == 1 && run.benchmark.holds_background(false));
        run.drive(Device({8, 8, 8, 8}), 90.0);
        assert(run.benchmark.phase() == AutoPresetBenchmark::Phase::Done && !run.benchmark.holds_background(true));
    }

    // Leaving the title during the measurement aborts it; nothing is chosen.
    {
        Run run;
        const auto step = run.drive(Device({8, 8, 8, 8}), 60.0, [](double now) { return now < 6.0; });
        assert(step.aborted && run.benchmark.reason() == "the title screen was left");
        assert(run.benchmark.phase() == AutoPresetBenchmark::Phase::Aborted && run.benchmark.results().empty());
        assert(run.benchmark.frame({run.now + 0.02, true, true, false, {}, -1.0, 0}).apply_rung == -1);
        assert(!run.benchmark.holds_background(true));
    }

    // A gap between frames (the app in the background) aborts it.
    {
        Run run;
        run.drive(Device({8, 8, 8, 8}), 4.0);
        assert(run.benchmark.running());
        const float gap[] = {16.7f};
        const auto step = run.benchmark.frame({run.now + 1.5, true, true, false, gap, -1.0, 0});
        assert(step.aborted && run.benchmark.reason().find("gap") != std::string::npos);
    }

    // The player applies settings: aborted from outside, and only once.
    {
        Run run;
        run.drive(Device({8, 8, 8, 8}), 4.0);
        assert(run.benchmark.running());
        assert(run.benchmark.abort("the player applied settings").aborted);
        assert(!run.benchmark.abort("again").aborted && run.benchmark.reason() == "the player applied settings");
        AutoPresetBenchmark idle;
        assert(idle.abort("nothing runs yet").aborted); // armed but not started: it does not start any more
        assert(idle.frame({10.0, true, true, false, {}, -1.0, 0}).apply_rung == -1);
    }

    // The settle frames of a switch are not counted; GPU times are averaged; the report has every number.
    {
        Run run;
        const auto step = run.drive(Device({8, 10, 20, 40}), 90.0, {}, [](int) { return 7.5; });
        assert(step.finished && run.benchmark.chosen() == 2);
        const auto &results = run.benchmark.results();
        assert(results.size() == 3 && results[0].passes && results[1].passes && !results[2].passes);
        for (const auto &r : results)
            assert(r.frames >= kAutoPresetMinFrames && r.seconds >= kAutoPresetMeasureSeconds && std::abs(r.gpu_ms - 7.5) < 1e-9);
        const std::string report = run.benchmark.report();
        for (const char *part : {"\"version\":2", "\"chosen\":1", "\"render_height\":0", "\"target_fps\":30", "\"headroom\":1.50",
                                 "\"preset\":2", "\"gpu_ms\":7.50", "\"passes\":false", "\"p95_ms\":", "\"why\":\"p95 present"})
            if (report.find(part) == std::string::npos)
            {
                std::cerr << "FAIL report lacks " << part << ": " << report << "\n";
                return 1;
            }
        Run lowest;
        lowest.drive(Device({30, 45, 60, 90}), 90.0);
        const std::string low = lowest.benchmark.report();
        assert(low.find("\"chosen\":0,\"render_height\":720") != std::string::npos && low.find("\"gpu_ms\":null") != std::string::npos);
    }
    std::cout << "PASS automatic graphics preset: Test Lab devices with headroom, the 720 rung, background hold, misses, slow rungs, title wait, aborts, report\n";
}
