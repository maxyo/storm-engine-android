// The device's thermal status, for the background work that must yield when it is hot: the relief generation and
// the ray bakes (town light, visibility, bounce: fx::SetBakeThermal).
#pragma once

namespace storm::effects
{

// True from ATHERMAL_STATUS_MODERATE up, the first level where the platform asks to cut load, or when the thermal
// headroom forecast says it is about to (thermal_api.hpp; AThermal, Android 11+ / API 30, headroom API 31, through
// dlsym: the app's minimum API is 24; asked at most once a second). Older Android and the host have no thermal
// status: never hot (said once in the log); a symbol missing on API 30+ is logged as an error.
bool device_hot();

} // namespace storm::effects
