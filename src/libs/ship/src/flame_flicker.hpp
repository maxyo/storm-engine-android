// A ship lantern's flame: two oscillators (fast and slow) that each move linearly from one random value to the next,
// `fStep` values per second. The flicker is cosmetic: it is stepped by real time (storm/cosmetic_time.hpp), so the
// time scale changes neither its speed nor its amplitude.
#pragma once

#include "math_inlines.h"

namespace storm::ship
{

struct FlickerOscillator
{
    float fStep;
    float fAmp;
    float fOneDivAmp;
    float fNewValue;
    float fOldValue;
    float fK;
};

// Advances both oscillators by `seconds` and returns the flame's intensity factor, 1 ± the flicker, in [0, 2].
inline float StepFlameFlicker(FlickerOscillator (&osc)[2], float seconds)
{
    float fIntensity = 0.0f;
    float fKAmp = 1.0f;
    for (FlickerOscillator &o : osc)
    {
        o.fK += o.fStep * seconds;
        if (o.fK >= 1.0f)
        {
            if (o.fK < 2.0f)
            {
                o.fK -= 1.0f;
                o.fOldValue = o.fNewValue;
                o.fNewValue = RRnd(-o.fAmp, o.fAmp);
            }
            else
            {
                o.fK = 0.0f;
                o.fOldValue = RRnd(-o.fAmp, o.fAmp);
                o.fNewValue = RRnd(-o.fAmp, o.fAmp);
            }
        }
        const float fIns = o.fOldValue + (o.fNewValue - o.fOldValue) * o.fK;
        fIntensity += fIns * fKAmp;
        fKAmp -= fIns * o.fOneDivAmp;
        if (fKAmp < 0.0f)
            break;
    }
    return 1.0f + Clampf(fIntensity, -1.0f, 1.0f);
}

} // namespace storm::ship
