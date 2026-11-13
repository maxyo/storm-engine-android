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
    bool vertex_texture = false; // vertex shaders can sample textures (GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS > 0)
};

// Call once after bgfx::init (and again after a device was re-created). The result is logged once at
// LogLevel::Info.
Support DetectSupport(const RendererFacts &renderer);

// Typical limits of GPU families, for tests and for forcing a profile on a development device
// (engine option fx_caps_profile): the presets must constrain cleanly and report what they lose on each.
enum class CapsProfile : uint8_t
{
    Device,       // what DetectSupport found (no restriction)
    Gles30Minimal, // GLES 3.0 floor: no MSAA, no anisotropy, no vertex texture fetch of RGBA16F
    MaliG5x,      // Mali-G52/G57: MSAA yes, multisampled depth not resolvable, anisotropy yes
    MaliG7x,      // Mali-G7x / Immortalis: everything
    Adreno6xx,    // everything but the multisampled depth resolve
    Adreno7xx8xx, // everything
    PowerVR,      // Rogue / IMG: no MSAA render targets in practice, no anisotropy
    Xclipse,      // Samsung Xclipse (RDNA): everything
    Count,
};
// `device` restricted to the profile (a profile never adds what the device lacks).
Support RestrictSupport(const Support &device, CapsProfile profile);
const char *CapsProfileName(CapsProfile profile);

} // namespace storm::fx
