// Automatic graphics preset (Android, renderer.cpp): on the first start, and again when kAutoPresetVersion is newer
// than the version of the stored automatic choice, the title scene is measured on the rungs of kAutoPresetRungs and
// the highest rung that holds kAutoPresetTargetFps with headroom is kept. The player's own choice of «Графика» is
// never measured over (the app stores its source; GraphicsSettings.java).
//
// Rungs: «Низкая» at a lower internal height (kAutoPresetLowestHeight, chosen only by this measurement; the app shows
// the height on the settings screen), «Низкая», «Средняя», «Высокая». The measurement starts on «Низкая» and goes up
// while rungs pass; when «Низкая» itself fails, the rung below it is measured and kept (the lowest the game has; the
// report says whether it holds the target).
//
// Headroom. The title (a ship at sea, 110-200 draws) is one of the cheapest scenes of the game; a port town draws
// 400-490. On the owner's Xiaomi 15 Ultra (Adreno 830) scenes of 120-190 draws took 13-15.5 ms of GPU and towns
// 17-21.5 ms (perf audit 2026-11-14): towns cost up to 1.5x the title's GPU time. So a rung passes when
//   * where the GPU offers timer queries: its mean GPU frame time x kAutoPresetHeadroom fits 1000 /
//     kAutoPresetTargetFps, and the 95th percentile of its present intervals is at most AutoPresetLimitMs() (the
//     CPU side holds the target too);
//   * without timer queries (some Mali): the 95th percentile of its present intervals fits 1000 /
//     kAutoPresetTargetFps / kAutoPresetHeadroom. Present intervals are whole display refreshes, so this asks for
//     the 60 fps the measurement paces at (kAutoPresetPacingFps) on 95 % of the frames.
// The measurement paces at kAutoPresetPacingFps whatever «Частота кадров» says (renderer.cpp restores it after), so
// the margin means the same for every player. The jitter allowance only takes in the scheduling noise of a frame that
// fits.
//
// The relief generation and the ray bakes are held while the measurement waits on the title and while it runs
// (renderer.cpp; effects thermal.hpp): their threads take all cores but two. The measurement starts after
// kAutoPresetTitleSeconds of the title screen with settled effects and no background work in progress (a relief map
// already started finishes first). Each rung then settles kAutoPresetSettleSeconds (the switch rebuilds render
// targets and links programs; those frames are not counted) and is measured kAutoPresetMeasureSeconds, at least
// kAutoPresetMinFrames frames. Leaving the title, the app going to the background (the lifecycle's own signal, not a
// frame gap: a slow GPU may take a second per frame) or new settings from the player abort it; nothing is stored then
// and the next start measures again.
//
// Header-only and free of the engine, so the host check tests/android_auto_preset runs it as it is.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace storm::render
{

// Raise when a renderer change shifts the cost of the presets enough that stored automatic choices are stale.
// 2: headroom, the rung below «Низкая», background work held, paced at 60 fps.
constexpr int kAutoPresetVersion = 2;
constexpr uint16_t kAutoPresetLowestHeight = 720; // the rung below «Низкая» (fx kLowRenderHeight is 900)
constexpr double kAutoPresetTargetFps = 30.0;
constexpr double kAutoPresetHeadroom = 1.5; // town / title GPU time, see above
constexpr uint32_t kAutoPresetPacingFps = 60;
constexpr double kAutoPresetJitterMs = 2.0;
constexpr double kAutoPresetTitleSeconds = 3.0;
constexpr double kAutoPresetSettleSeconds = 1.5;
constexpr double kAutoPresetMeasureSeconds = 3.0;
constexpr double kAutoPresetMaxMeasureSeconds = 8.0; // a very slow rung ends with fewer frames
constexpr size_t kAutoPresetMinFrames = 30;
constexpr size_t kAutoPresetLeastFrames = 8; // fewer than this after the longest measurement: not measured

// A rung of the measurement: a preset (0 Низкая, 1 Средняя, 2 Высокая) and the scene's internal height (0: the
// preset's own).
struct AutoPresetRung
{
    int preset = 0;
    uint16_t render_height = 0;
};
constexpr std::array<AutoPresetRung, 4> kAutoPresetRungs = {{{0, kAutoPresetLowestHeight}, {0, 0}, {1, 0}, {2, 0}}};
constexpr int kAutoPresetFirstRung = 1; // «Низкая»
constexpr int kAutoPresetTopRung = static_cast<int>(kAutoPresetRungs.size()) - 1;

// The frame budget at the target.
inline double AutoPresetBudgetMs()
{
    return 1000.0 / kAutoPresetTargetFps;
}

// The 95th percentile of present intervals a rung may reach with GPU timers (its GPU time decides the headroom).
inline double AutoPresetLimitMs()
{
    return AutoPresetBudgetMs() + kAutoPresetJitterMs;
}

// The 95th percentile of present intervals a rung may reach without GPU timers: the headroom in the intervals.
inline double AutoPresetHeadroomLimitMs()
{
    return AutoPresetBudgetMs() / kAutoPresetHeadroom + kAutoPresetJitterMs;
}

// One rung's measurement.
struct AutoPresetResult
{
    int rung = 0;
    size_t frames = 0;
    double seconds = 0.0;
    float p50_ms = 0.0f, p95_ms = 0.0f, max_ms = 0.0f;
    double gpu_ms = -1.0; // mean GPU frame time, < 0: no timer queries
    bool passes = false;
    const char *why = ""; // what decided
};

inline float AutoPresetPercentile(std::vector<float> sorted, size_t percent)
{
    if (sorted.empty())
        return 0.0f;
    std::sort(sorted.begin(), sorted.end());
    return sorted[(sorted.size() * percent + 99) / 100 - 1];
}

inline AutoPresetResult MeasureAutoPreset(int rung, const std::vector<float> &intervals_ms, double seconds, double gpu_ms)
{
    AutoPresetResult result;
    result.rung = rung;
    result.frames = intervals_ms.size();
    result.seconds = seconds;
    result.gpu_ms = gpu_ms;
    if (intervals_ms.empty())
    {
        result.why = "no frames";
        return result;
    }
    result.p50_ms = AutoPresetPercentile(intervals_ms, 50);
    result.p95_ms = AutoPresetPercentile(intervals_ms, 95);
    result.max_ms = *std::max_element(intervals_ms.begin(), intervals_ms.end());
    if (result.frames < kAutoPresetLeastFrames)
        result.why = "too few frames";
    else if (gpu_ms >= 0.0)
    {
        if (result.p95_ms > AutoPresetLimitMs())
            result.why = "p95 present interval over the limit";
        else if (gpu_ms * kAutoPresetHeadroom > AutoPresetBudgetMs())
            result.why = "GPU time x headroom over the budget";
        else
        {
            result.passes = true;
            result.why = "GPU time x headroom within the budget";
        }
    }
    else if (result.p95_ms > AutoPresetHeadroomLimitMs())
        result.why = "p95 present interval over the headroom limit (no GPU timers)";
    else
    {
        result.passes = true;
        result.why = "p95 present interval within the headroom limit (no GPU timers)";
    }
    return result;
}

// The highest rung of an unbroken run of passing rungs from «Низкая» up; the rung below «Низкая» when «Низкая»
// fails (whether or not it passes itself: nothing is lower).
inline int ChooseAutoPreset(std::span<const AutoPresetResult> results)
{
    const auto passes = [&](int rung) {
        const auto found = std::find_if(results.begin(), results.end(), [&](const AutoPresetResult &r) { return r.rung == rung; });
        return found != results.end() && found->passes;
    };
    if (!passes(kAutoPresetFirstRung))
        return 0;
    int chosen = kAutoPresetFirstRung;
    while (chosen < kAutoPresetTopRung && passes(chosen + 1))
        ++chosen;
    return chosen;
}

// The measurement as a state machine, driven once per frame by the renderer.
class AutoPresetBenchmark
{
  public:
    enum class Phase
    {
        WaitingForTitle,
        Settling,
        Measuring,
        Done,
        Aborted,
    };

    struct Frame
    {
        double now_s = 0.0;
        bool title = false;          // the title screen is shown
        bool effects_ready = false;  // no preset switch pending (EffectsReport::pending is false)
        bool background_busy = false; // held background work is still finishing (a relief map in progress)
        bool backgrounded = false;    // the app went to the background since the last frame
        std::span<const float> intervals_ms; // present intervals since the last frame
        double gpu_ms = -1.0;        // GPU time of the frames since the last one, < 0: none
        size_t gpu_frames = 0;
    };

    struct Step
    {
        int apply_rung = -1;   // >= 0: switch the effects to this rung of kAutoPresetRungs now
        bool finished = false; // Done this frame: chosen() and report() are final
        bool aborted = false;  // Aborted this frame: reason()
    };

    Phase phase() const { return phase_; }
    bool running() const { return phase_ == Phase::Settling || phase_ == Phase::Measuring; }
    // Background work must wait: the measurement runs, or waits on the title to start.
    bool holds_background(bool title) const { return running() || (phase_ == Phase::WaitingForTitle && title); }
    int chosen() const { return chosen_; }
    const std::vector<AutoPresetResult> &results() const { return results_; }
    const std::string &reason() const { return reason_; }

    Step frame(const Frame &frame)
    {
        Step step;
        switch (phase_)
        {
        case Phase::WaitingForTitle:
            if (!frame.title || !frame.effects_ready || frame.background_busy || frame.backgrounded)
            {
                title_since_ = -1.0;
                break;
            }
            if (title_since_ < 0.0)
                title_since_ = frame.now_s;
            if (frame.now_s - title_since_ >= kAutoPresetTitleSeconds)
                step.apply_rung = begin(kAutoPresetFirstRung, frame.now_s);
            break;
        case Phase::Settling:
        case Phase::Measuring:
            if (!frame.title)
                return abort("the title screen was left");
            if (frame.backgrounded)
                return abort("the game went to the background");
            if (phase_ == Phase::Settling)
            {
                if (frame.effects_ready && frame.now_s - phase_since_ >= kAutoPresetSettleSeconds)
                {
                    phase_ = Phase::Measuring;
                    phase_since_ = frame.now_s;
                    intervals_.clear();
                    gpu_sum_ = 0.0;
                    gpu_frames_ = 0;
                }
                break;
            }
            if (!frame.effects_ready)
                return abort("the effects switched during the measurement");
            intervals_.insert(intervals_.end(), frame.intervals_ms.begin(), frame.intervals_ms.end());
            if (frame.gpu_ms >= 0.0 && frame.gpu_frames > 0)
            {
                gpu_sum_ += frame.gpu_ms;
                gpu_frames_ += frame.gpu_frames;
            }
            {
                const double seconds = frame.now_s - phase_since_;
                if (!(seconds >= kAutoPresetMeasureSeconds && intervals_.size() >= kAutoPresetMinFrames) &&
                    seconds < kAutoPresetMaxMeasureSeconds)
                    break;
                results_.push_back(MeasureAutoPreset(rung_, intervals_, seconds,
                                                     gpu_frames_ ? gpu_sum_ / static_cast<double>(gpu_frames_) : -1.0));
                const bool passed = results_.back().passes;
                if (passed && rung_ >= kAutoPresetFirstRung && rung_ < kAutoPresetTopRung)
                {
                    step.apply_rung = begin(rung_ + 1, frame.now_s);
                    break;
                }
                if (!passed && rung_ == kAutoPresetFirstRung)
                {
                    step.apply_rung = begin(0, frame.now_s);
                    break;
                }
                chosen_ = ChooseAutoPreset(results_);
                phase_ = Phase::Done;
                step.finished = true;
                step.apply_rung = chosen_;
            }
            break;
        case Phase::Done:
        case Phase::Aborted:
            break;
        }
        return step;
    }

    // From outside the frame (the player applied settings): ends a running measurement.
    Step abort(const std::string &reason)
    {
        Step step;
        if (phase_ == Phase::Done || phase_ == Phase::Aborted)
            return step;
        reason_ = reason;
        phase_ = Phase::Aborted;
        step.aborted = true;
        return step;
    }

    // The measurement as one log line / the JSON the app keeps for the diagnostics export.
    std::string report() const
    {
        const AutoPresetRung &chosen = kAutoPresetRungs[static_cast<size_t>(chosen_)];
        char head[320];
        snprintf(head, sizeof(head),
                 "{\"version\":%d,\"chosen\":%d,\"render_height\":%u,\"target_fps\":%.0f,\"pacing_fps\":%u,"
                 "\"headroom\":%.2f,\"limit_p95_ms\":%.2f,\"limit_p95_no_gpu_ms\":%.2f,\"rungs\":[",
                 kAutoPresetVersion, chosen.preset, static_cast<unsigned>(chosen.render_height), kAutoPresetTargetFps,
                 kAutoPresetPacingFps, kAutoPresetHeadroom, AutoPresetLimitMs(), AutoPresetHeadroomLimitMs());
        std::string json = head;
        for (size_t i = 0; i < results_.size(); ++i)
        {
            const AutoPresetResult &r = results_[i];
            const AutoPresetRung &rung = kAutoPresetRungs[static_cast<size_t>(r.rung)];
            char gpu[32] = "null";
            if (r.gpu_ms >= 0.0)
                snprintf(gpu, sizeof(gpu), "%.2f", r.gpu_ms);
            char entry[480];
            snprintf(entry, sizeof(entry),
                     "%s{\"preset\":%d,\"render_height\":%u,\"frames\":%zu,\"seconds\":%.2f,\"fps\":%.1f,\"p50_ms\":%.2f,"
                     "\"p95_ms\":%.2f,\"max_ms\":%.2f,\"gpu_ms\":%s,\"passes\":%s,\"why\":\"%s\"}",
                     i ? "," : "", rung.preset, static_cast<unsigned>(rung.render_height), r.frames, r.seconds,
                     r.seconds > 0.0 ? r.frames / r.seconds : 0.0, r.p50_ms, r.p95_ms, r.max_ms, gpu,
                     r.passes ? "true" : "false", r.why);
            json += entry;
        }
        json += "]}";
        return json;
    }

  private:
    int begin(int rung, double now)
    {
        rung_ = rung;
        phase_ = Phase::Settling;
        phase_since_ = now;
        return rung;
    }

    Phase phase_ = Phase::WaitingForTitle;
    int rung_ = kAutoPresetFirstRung;
    int chosen_ = kAutoPresetFirstRung;
    double title_since_ = -1.0;
    double phase_since_ = 0.0;
    std::vector<float> intervals_;
    double gpu_sum_ = 0.0;
    size_t gpu_frames_ = 0;
    std::vector<AutoPresetResult> results_;
    std::string reason_;
};

} // namespace storm::render
