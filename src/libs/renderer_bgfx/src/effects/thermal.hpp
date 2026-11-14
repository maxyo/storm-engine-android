// When the background work must yield: the relief generation and the ray bakes (town light, visibility, bounce:
// fx::SetBakeGate). It waits while the device is hot and while the renderer holds it (the automatic graphics preset
// measures the GPU: renderer auto_preset.hpp).
#pragma once

namespace storm::effects
{

// True from ATHERMAL_STATUS_MODERATE up, the first level where the platform asks to cut load, or when the thermal
// headroom forecast says it is about to (thermal_api.hpp; AThermal, Android 11+ / API 30, headroom API 31, through
// dlsym: the app's minimum API is 24; asked at most once a second). Older Android and the host have no thermal
// status: never hot (said once in the log); a symbol missing on API 30+ is logged as an error.
bool device_hot();

// Main thread: the renderer holds the background work (`why` says why; null releases it). Logged on change.
void hold_background_work(const char *why);
// Why the background work must wait now (the hold, else the thermal status); null: it may run. Any thread.
const char *background_wait_reason();

} // namespace storm::effects
