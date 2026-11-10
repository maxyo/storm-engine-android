#pragma once

// Engine side of tools/scene_sweep (see its README.md): lets an external runner load
// every game scene through the game's own scripts. Compiled only with
// -DSTORM_SCENE_SWEEP=ON (never in shipped builds) and inert unless the
// STORM_SCENE_SWEEP_DIR environment variable names the runner's control directory.
#ifdef STORM_SCENE_SWEEP
#include <cstdint>
#include <functional>
#include <string>

namespace storm::scene_sweep
{

// Called by the renderer after each presented frame.
void framePresented(uint32_t drawCalls);
// Set by the renderer: returns its counters since the previous call as key=value pairs.
// The sweep restarts the window at each event and appends it to each capture.
void setRendererWindow(std::function<std::string()> window);

} // namespace storm::scene_sweep
#endif
