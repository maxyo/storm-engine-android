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
// Present intervals of a preset: the n-th frame's interval in ms.
using Pacing = std::function<float(int preset, size_t frame)>;

struct Run
{
    AutoPresetBenchmark benchmark;
    double now = 0.0;
    int preset = 0;           // what the renderer draws
    int pending_frames = 0;   // frames the switch stays pending
    std::vector<int> applied; // every preset the machine asked for
    size_t frame_index = 0;

    // Frames until the machine ends or `limit` seconds pass; `title(now)` says whether the title is shown.
    AutoPresetBenchmark::Step drive(const Pacing &pacing, double limit, const std::function<bool(double)> &title = {},
                                    double gpu_ms = -1.0)
    {
        AutoPresetBenchmark::Step step;
        const double end = now + limit;
        while (now < end)
        {
            const float interval = pacing(preset, frame_index++);
            now += interval / 1000.0;
            const std::vector<float> intervals{interval};
            AutoPresetBenchmark::Frame frame;
            frame.now_s = now;
            frame.title = title ? title(now) : true;
            frame.effects_ready = pending_frames == 0;
            frame.intervals_ms = intervals;
            frame.gpu_ms = gpu_ms >= 0.0 ? gpu_ms : -1.0;
            frame.gpu_frames = gpu_ms >= 0.0 ? 1 : 0;
            if (pending_frames > 0)
                --pending_frames;
            step = benchmark.frame(frame);
            if (step.apply_preset >= 0)
            {
                applied.push_back(step.apply_preset);
                if (step.apply_preset != preset)
                    pending_frames = 5; // programs link over a few frames
                preset = step.apply_preset;
            }
            if (step.finished || step.aborted)
                return step;
        }
        return step;
    }
};

// A device whose preset p takes these frame times; presents are whole 60 Hz refreshes with ±0.6 ms jitter.
Pacing Device(std::vector<float> work_ms)
{
    return [work_ms](int preset, size_t frame) {
        const double refresh = 1000.0 / 60.0;
        const double shown = std::ceil(work_ms[preset] / refresh - 1e-6) * refresh;
        return static_cast<float>(shown + ((frame % 7) * 0.2 - 0.6));
    };
}

// A device whose preset p misses a frame (one refresh more) every `miss_every` frames.
Pacing Stutter(float base_ms, size_t miss_every)
{
    return [=](int, size_t frame) { return frame % miss_every == 0 ? base_ms + 1000.0f / 60.0f : base_ms; };
}

void ExpectChoice(const std::vector<float> &work, int chosen, size_t measured, const char *what)
{
    Run run;
    const auto step = run.drive(Device(work), 60.0);
    if (!step.finished || run.benchmark.chosen() != chosen || run.benchmark.results().size() != measured)
    {
        std::cerr << "FAIL " << what << ": finished " << step.finished << " chosen " << run.benchmark.chosen() << " measured "
                  << run.benchmark.results().size() << "\n"
                  << run.benchmark.report() << "\n";
        std::exit(1);
    }
    // The last thing the machine asks for is the choice; nothing more follows.
    assert(run.applied.back() == chosen);
    assert(run.benchmark.phase() == AutoPresetBenchmark::Phase::Done);
    assert(!run.benchmark.frame({run.now + 0.016, true, true, {}, -1.0, 0}).finished);
}
} // namespace

int main()
{
    // Percentiles as frame pacing logs them.
    assert(AutoPresetPercentile({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 50) == 5);
    assert(AutoPresetPercentile({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 95) == 10);
    assert(AutoPresetPercentile({}, 95) == 0);
    assert(std::abs(AutoPresetLimitMs() - (1000.0 / 30.0 + kAutoPresetJitterMs)) < 1e-9);

    // The Test Lab devices (title scene, engine of 2026-11-13; work time per preset).
    ExpectChoice({12.7f, 21.6f, 31.0f}, 2, 3, "Galaxy S23 (Adreno 740): every preset holds 30 fps");
    ExpectChoice({22.2f, 46.6f, 75.7f}, 0, 2, "Galaxy A56 (Xclipse 540): Средняя misses, Высокая is not measured");
    ExpectChoice({23.6f, 75.7f, 118.2f}, 0, 2, "Galaxy S21 (Adreno 660)");
    ExpectChoice({25.3f, 45.9f, 25.5f}, 0, 2, "Pixel 8: a cheaper-looking Высокая after a failing Средняя is not taken");
    ExpectChoice({10.0f, 14.0f, 30.0f}, 2, 3, "fast phone");
    ExpectChoice({10.0f, 30.0f, 40.0f}, 1, 3, "Средняя holds, Высокая misses");
    ExpectChoice({40.0f, 60.0f, 90.0f}, 0, 1, "nothing holds: Низкая, the lowest, and no further preset measured");

    // Misses: 1 in 25 frames late is within p95, 1 in 10 is not.
    {
        Run run;
        assert(run.drive(Stutter(33.3f, 25), 60.0).finished && run.benchmark.chosen() == 2);
        Run late;
        assert(late.drive(Stutter(33.3f, 10), 60.0).finished && late.benchmark.chosen() == 0);
        assert(!late.benchmark.results()[0].passes && late.benchmark.results()[0].p95_ms > AutoPresetLimitMs());
    }

    // A preset slower than 1 fps cannot fill a measurement: it fails, whatever its few intervals say.
    {
        const auto result = MeasureAutoPreset(0, {30.0f, 30.0f, 30.0f}, kAutoPresetMaxMeasureSeconds, -1.0);
        assert(!result.passes && result.frames == 3);
        Run run;
        const auto step = run.drive([](int, size_t) { return 900.0f; }, 120.0);
        assert(step.aborted || (step.finished && run.benchmark.chosen() == 0));
    }

    // The measurement waits for kAutoPresetTitleSeconds of a steady title; a flicker of another screen restarts it.
    {
        Run run;
        const auto title = [](double now) { return now < 2.0 || now > 2.5; };
        run.drive(Device({10, 10, 10}), 5.0, title);
        assert(run.applied.empty()); // 2.5 + 3 s not reached yet
        run.drive(Device({10, 10, 10}), 1.0, title);
        assert(run.applied.size() == 1 && run.applied[0] == 0);
    }

    // Leaving the title during the measurement aborts it; nothing is chosen.
    {
        Run run;
        const auto step = run.drive(Device({10, 10, 10}), 60.0, [](double now) { return now < 6.0; });
        assert(step.aborted && run.benchmark.reason() == "the title screen was left");
        assert(run.benchmark.phase() == AutoPresetBenchmark::Phase::Aborted && run.benchmark.results().empty());
        assert(run.benchmark.frame({run.now + 0.02, true, true, {}, -1.0, 0}).apply_preset == -1);
    }

    // A gap between frames (the app in the background) aborts it.
    {
        Run run;
        run.drive(Device({10, 10, 10}), 4.0);
        assert(run.benchmark.running());
        const float gap[] = {16.7f};
        const auto step = run.benchmark.frame({run.now + 1.5, true, true, gap, -1.0, 0});
        assert(step.aborted && run.benchmark.reason().find("gap") != std::string::npos);
    }

    // The player applies settings: aborted from outside, and only once.
    {
        Run run;
        run.drive(Device({10, 10, 10}), 4.0);
        assert(run.benchmark.running());
        assert(run.benchmark.abort("the player applied settings").aborted);
        assert(!run.benchmark.abort("again").aborted && run.benchmark.reason() == "the player applied settings");
        AutoPresetBenchmark idle;
        assert(idle.abort("nothing runs yet").aborted); // armed but not started: it does not start any more
        assert(idle.frame({10.0, true, true, {}, -1.0, 0}).apply_preset == -1);
    }

    // The settle frames of a switch are not counted; GPU times are averaged; the report has every number.
    {
        Run run;
        const auto step = run.drive(Device({10, 20, 40}), 60.0, {}, 7.5);
        assert(step.finished && run.benchmark.chosen() == 1);
        const auto &results = run.benchmark.results();
        assert(results.size() == 3 && results[0].passes && results[1].passes && !results[2].passes);
        for (const auto &r : results)
            assert(r.frames >= kAutoPresetMinFrames && r.seconds >= kAutoPresetMeasureSeconds && std::abs(r.gpu_ms - 7.5) < 1e-9);
        const std::string report = run.benchmark.report();
        for (const char *part : {"\"version\":1", "\"chosen\":1", "\"target_fps\":30", "\"preset\":2", "\"gpu_ms\":7.50",
                                 "\"passes\":false", "\"p95_ms\":"})
            if (report.find(part) == std::string::npos)
            {
                std::cerr << "FAIL report lacks " << part << ": " << report << "\n";
                return 1;
            }
        Run plain;
        plain.drive(Device({10, 10, 10}), 60.0);
        assert(plain.benchmark.report().find("\"gpu_ms\":null") != std::string::npos);
    }
    std::cout << "PASS automatic graphics preset: Test Lab devices, misses, slow presets, title wait, aborts, report\n";
}
