// Embedded efkbgfx shaders (Storm Engine port).
#pragma once

#include <bgfx/c99/bgfx.h>

namespace EffekseerRendererBGFX
{
// `name` is a renderer shader name such as "sprite_unlit" or "model_adv_lit". Returns an invalid
// handle when no blob exists for it.
bgfx_shader_handle_t LoadShader(const char *name, bool vertex);
} // namespace EffekseerRendererBGFX
