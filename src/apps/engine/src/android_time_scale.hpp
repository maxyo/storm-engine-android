#pragma once
#ifdef __ANDROID__
// Four steps of the game's time acceleration for a touch HUD (owner decision): x1, x2, x4, x8.
// The scripts keep the logic: TimeScaleCounter (scale = 1 + counter * 0.25) is set one step of
// their own ladder below the target and one TimeScaleFaster / TimeScaleSlower activation runs
// their handler, which applies their guards (underwater, quest locks), clamps (x6 in a fight,
// x3 on land), logs the new scale once and keeps the TimeSpeed perk in step.
#include "core.h"
#include "platform/android_ui.hpp"
#include "v_data.h"

#include <array>
#include <string_view>

namespace storm::android
{
// The scripts' counters of x1, x2, x4, x8 and the counter one of their own steps below each.
inline constexpr std::array<int32_t, 4> TimeScaleCounters{0, 4, 12, 28};
inline constexpr std::array<int32_t, 4> TimeScaleFrom{1, 3, 10, 24};

// "TimeScaleX1" … "TimeScaleX8" → the step, or -1.
inline int timeScaleStep(std::string_view control)
{
    constexpr std::array<std::string_view, 4> names{"TimeScaleX1", "TimeScaleX2", "TimeScaleX4", "TimeScaleX8"};
    for (size_t i = 0; i < names.size(); ++i)
        if (control == names[i]) return static_cast<int>(i);
    return -1;
}

// Runs at a completed frame boundary, like every other control activation.
inline void applyTimeScaleStep(int step)
{
    auto *counter = static_cast<VDATA *>(core.GetScriptVariable("TimeScaleCounter"));
    int32_t current = 0;
    if (step < 0 || step >= static_cast<int>(TimeScaleCounters.size()) || !counter || !counter->Get(current)) return;
    if (current == TimeScaleCounters[step]) return;
    counter->Set(TimeScaleFrom[step]);
    // No toast for the new scale (owner decision): the HUD's time button shows it.
    ++gameMessagesMuted;
    core.Event("Control Activation", "s", step == 0 ? "TimeScaleSlower" : "TimeScaleFaster");
    --gameMessagesMuted;
}
} // namespace storm::android
#endif
