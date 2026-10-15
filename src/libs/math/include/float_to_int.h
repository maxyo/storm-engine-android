#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

#if !defined(STORM_FORCE_SCALAR_MATH) && (defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1))
#include <xmmintrin.h>
#define STORM_MATH_HAS_SSE 1
#else
#define STORM_MATH_HAS_SSE 0
#endif

namespace storm::math
{
// CVTSS2SI/CVTTSS2SI return INT32_MIN for NaN, infinity and overflow
// with masked exceptions. Check in double before casting to avoid C++ UB.
inline int32_t checked_int32(double value)
{
    constexpr auto minimum = std::numeric_limits<int32_t>::min();
    constexpr auto maximum = std::numeric_limits<int32_t>::max();
    if (!std::isfinite(value) || value < minimum || value > maximum)
        return minimum;
    return static_cast<int32_t>(value);
}

inline int32_t truncate_to_int(float value)
{
#if STORM_MATH_HAS_SSE
    return _mm_cvttss_si32(_mm_set_ss(value));
#else
    return checked_int32(std::trunc(static_cast<double>(value)));
#endif
}

// Use the current floating-point rounding mode, including ties-to-even under
// FE_TONEAREST. std::round/lround would change the legacy half-integer behavior.
inline int32_t round_to_int(float value)
{
#if STORM_MATH_HAS_SSE
    return _mm_cvtss_si32(_mm_set_ss(value));
#else
    return checked_int32(std::nearbyint(static_cast<double>(value)));
#endif
}
} // namespace storm::math
