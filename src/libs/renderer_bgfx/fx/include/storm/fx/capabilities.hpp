// What the GPU behind bgfx supports, for Support / ConstrainSettings() (settings.hpp).
#pragma once

#include "storm/fx/settings.hpp"

namespace storm::fx
{

// Call once after bgfx::init (and again after a device was re-created). `scene_depth` is the renderer's own
// knowledge: whether the game's offscreen depth attachment is created as a texture that can be sampled.
// The result is logged once at LogLevel::Info.
Support DetectSupport(bool scene_depth);

} // namespace storm::fx
