// Automatic graphics preset (Android, renderer.cpp): on the first start, and again when kAutoPresetVersion is newer
// than the version of the stored automatic choice, the title scene is measured on each preset, cheapest first, and
// the highest preset that holds kAutoPresetTargetFps is kept. The player's own choice of «Графика» is never
// measured over (the app stores its source; GraphicsSettings.java).
//
// A preset passes when the 95th percentile of its present intervals is at most 1000 / kAutoPresetTargetFps plus
// kAutoPresetJitterMs. Present intervals are whole display refreshes (Swappy paces them), so a frame that misses
// 33.3 ms shows a whole refresh later (41.7 ms at 120 Hz, 44.4 at 90, 50 at 60); the jitter allowance only takes in
// the scheduling noise of a frame that fits. GPU timer averages are logged with each preset where the GPU offers
// timer queries (not on every Mali); they do not decide, because they are not available everywhere.
//
// The measurement starts after kAutoPresetTitleSeconds of the title screen with settled effects. Each preset then
// settles kAutoPresetSettleSeconds (the switch rebuilds render targets and links programs; those frames are not
// counted) and is measured kAutoPresetMeasureSeconds, at least kAutoPresetMinFrames frames. A preset that misses
// the target ends the measurement: the presets cost more in order (fx/settings.cpp). Leaving the title, a frame gap
// longer than kAutoPresetMaxGapSeconds (the app was in the background) or new settings from the player abort it;
// nothing is stored then and the next start measures again.
//
// Header-only and free of the engine, so the host check tests/android_auto_preset runs it as it is.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace storm::render
{

// Raise when a renderer change shifts the cost of the presets enough that stored automatic choices are stale.
constexpr int kAutoPresetVersion = 1;
constexpr int kAutoPresetHighest = 2; // 0 Низкая, 1 Средняя, 2 Высокая
constexpr double kAutoPresetTargetFps = 30.0;
constexpr double kAutoPresetJitterMs = 2.0;
constexpr double kAutoPresetTitleSeconds = 3.0;
constexpr double kAutoPresetSettleSeconds = 1.5;
constexpr double kAutoPresetMeasureSeconds = 3.0;
constexpr double kAutoPresetMaxMeasureSeconds = 8.0; // a very slow preset ends with fewer frames
constexpr size_t kAutoPresetMinFrames = 30;
constexpr size_t kAutoPresetLeastFrames = 8; // fewer than this after the longest measurement: not measured
constexpr double kAutoPresetMaxGapSeconds = 1.0;

inline double AutoPresetLimitMs()
{
    return 1000.0 / kAutoPresetTargetFps + kAutoPresetJitterMs;
}

// One preset's measurement.
struct AutoPresetResult
{
    int preset = 0;
    size_t frames = 0;
    double seconds = 0.0;
    float p50_ms = 0.0f, p95_ms = 0.0f, max_ms = 0.0f;
    double gpu_ms = -1.0; // mean GPU frame time, < 0: no timer queries
    bool passes = false;
};

inline float AutoPresetPercentile(std::vector<float> sorted, size_t percent)
{
    if (sorted.empty())
        return 0.0f;
    std::sort(sorted.begin(), sorted.end());
    return sorted[(sorted.size() * percent + 99) / 100 - 1];
}

inline AutoPresetResult MeasureAutoPreset(int preset, const std::vector<float> &intervals_ms, double seconds, double gpu_ms)
{
    AutoPresetResult result;
    result.preset = preset;
    result.frames = intervals_ms.size();
    result.seconds = seconds;
    result.gpu_ms = gpu_ms;
    if (intervals_ms.empty())
        return result;
    result.p50_ms = AutoPresetPercentile(intervals_ms, 50);
    result.p95_ms = AutoPresetPercentile(intervals_ms, 95);
    result.max_ms = *std::max_element(intervals_ms.begin(), intervals_ms.end());
    result.passes = result.frames >= kAutoPresetLeastFrames && result.p95_ms <= AutoPresetLimitMs();
    return result;
}

// The highest preset of an unbroken run of passing presets from the cheapest; «Низкая» when none passes (the
// lowest the game has; the report says it misses the target).
inline int ChooseAutoPreset(std::span<const AutoPresetResult> results)
{
    int chosen = 0;
    for (int preset = 0; preset <= kAutoPresetHighest; ++preset)
    {
        const auto found = std::find_if(results.begin(), results.end(), [&](const AutoPresetResult &r) { return r.preset == preset; });
        if (found == results.end() || !found->passes)
            break;
        chosen = preset;
    }
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
        std::span<const float> intervals_ms; // present intervals since the last frame
        double gpu_ms = -1.0;        // GPU time of the frames since the last one, < 0: none
        size_t gpu_frames = 0;
    };

    struct Step
    {
        int apply_preset = -1; // >= 0: switch the effects to this preset now
        bool finished = false; // Done this frame: chosen() and report() are final
        bool aborted = false;  // Aborted this frame: reason()
    };

    Phase phase() const { return phase_; }
    bool running() const { return phase_ == Phase::Settling || phase_ == Phase::Measuring; }
    int chosen() const { return chosen_; }
    const std::vector<AutoPresetResult> &results() const { return results_; }
    const std::string &reason() const { return reason_; }

    Step frame(const Frame &frame)
    {
        Step step;
        const double gap = have_last_ ? frame.now_s - last_now_ : 0.0;
        have_last_ = true;
        last_now_ = frame.now_s;
        switch (phase_)
        {
        case Phase::WaitingForTitle:
            if (!frame.title || !frame.effects_ready || gap > kAutoPresetMaxGapSeconds)
            {
                title_since_ = -1.0;
                break;
            }
            if (title_since_ < 0.0)
                title_since_ = frame.now_s;
            if (frame.now_s - title_since_ >= kAutoPresetTitleSeconds)
                step.apply_preset = begin(0, frame.now_s);
            break;
        case Phase::Settling:
        case Phase::Measuring:
            if (!frame.title)
                return abort("the title screen was left");
            if (gap > kAutoPresetMaxGapSeconds)
            {
                char text[96];
                snprintf(text, sizeof(text), "a %.2f s gap between frames (the game was in the background)", gap);
                return abort(text);
            }
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
                results_.push_back(MeasureAutoPreset(preset_, intervals_, seconds,
                                                     gpu_frames_ ? gpu_sum_ / static_cast<double>(gpu_frames_) : -1.0));
                if (results_.back().passes && preset_ < kAutoPresetHighest)
                {
                    step.apply_preset = begin(preset_ + 1, frame.now_s);
                    break;
                }
                chosen_ = ChooseAutoPreset(results_);
                phase_ = Phase::Done;
                step.finished = true;
                step.apply_preset = chosen_;
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
        std::string json = "{\"version\":" + std::to_string(kAutoPresetVersion) + ",\"chosen\":" + std::to_string(chosen_);
        char number[64];
        snprintf(number, sizeof(number), ",\"target_fps\":%.0f,\"limit_p95_ms\":%.2f,\"presets\":[", kAutoPresetTargetFps,
                 AutoPresetLimitMs());
        json += number;
        for (size_t i = 0; i < results_.size(); ++i)
        {
            const AutoPresetResult &r = results_[i];
            char gpu[32] = "null";
            if (r.gpu_ms >= 0.0)
                snprintf(gpu, sizeof(gpu), "%.2f", r.gpu_ms);
            char entry[288];
            snprintf(entry, sizeof(entry),
                     "%s{\"preset\":%d,\"frames\":%zu,\"seconds\":%.2f,\"fps\":%.1f,\"p50_ms\":%.2f,\"p95_ms\":%.2f,"
                     "\"max_ms\":%.2f,\"gpu_ms\":%s,\"passes\":%s}",
                     i ? "," : "", r.preset, r.frames, r.seconds, r.seconds > 0.0 ? r.frames / r.seconds : 0.0, r.p50_ms,
                     r.p95_ms, r.max_ms, gpu,
                     r.passes ? "true" : "false");
            json += entry;
        }
        json += "]}";
        return json;
    }

  private:
    int begin(int preset, double now)
    {
        preset_ = preset;
        phase_ = Phase::Settling;
        phase_since_ = now;
        return preset;
    }

    Phase phase_ = Phase::WaitingForTitle;
    int preset_ = 0;
    int chosen_ = 0;
    double title_since_ = -1.0;
    double phase_since_ = 0.0;
    double last_now_ = 0.0;
    bool have_last_ = false;
    std::vector<float> intervals_;
    double gpu_sum_ = 0.0;
    size_t gpu_frames_ = 0;
    std::vector<AutoPresetResult> results_;
    std::string reason_;
};

} // namespace storm::render
