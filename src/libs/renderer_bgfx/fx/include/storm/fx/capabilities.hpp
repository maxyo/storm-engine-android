// What the GPU behind bgfx supports, for Support / ConstrainSettings() (settings.hpp).
#pragma once

#include "storm/fx/settings.hpp"

namespace storm::fx
{

// What only the renderer knows; bgfx has no capability bits for these.
struct RendererFacts
{
    bool scene_depth = false; // the game's offscreen depth attachment is a texture that can be sampled
    bool msaa_depth = false;  // ... and it still can when the scene target is multisampled (depth is resolved)
    bool anisotropy = false;  // anisotropic filtering exists (GL_EXT_texture_filter_anisotropic or core)
};

// Call once after bgfx::init (and again after a device was re-created). The result is logged once at
// LogLevel::Info.
Support DetectSupport(const RendererFacts &renderer);

} // namespace storm::fx
