#pragma once

// Tracy profiler instrumentation (cmake/tracy.cmake). Records only in development builds configured with
// -DSTORM_TRACY=ON and only in the sources listed with storm_tracy_sources() in src/CMakeLists.txt; anywhere
// else these expand to nothing. Names are string literals (Tracy keeps the pointer).
#ifdef STORM_TRACY
#include <tracy/Tracy.hpp>

#define STORM_PROFILE_ZONE(name) ZoneScopedN(name)
// The current zone's text, e.g. which entity a per-entity zone ran (copied).
#define STORM_PROFILE_ZONE_TEXT(text, size) ZoneText(text, size)
#define STORM_PROFILE_FRAME() FrameMark
#define STORM_PROFILE_PLOT(name, value) TracyPlot(name, value)
#define STORM_PROFILE_THREAD(name) tracy::SetThreadName(name)
#else
#define STORM_PROFILE_ZONE(name) static_cast<void>(0)
#define STORM_PROFILE_ZONE_TEXT(text, size) static_cast<void>(0)
#define STORM_PROFILE_FRAME() static_cast<void>(0)
#define STORM_PROFILE_PLOT(name, value) static_cast<void>(0)
#define STORM_PROFILE_THREAD(name) static_cast<void>(0)
#endif
