// Embedded efkbgfx shader blobs (compiled at build time by storm_bgfx_embed_shaders, see ../../CMakeLists.txt).
// Added for the Storm Engine port; replaces upstream's shader_load callback.
#include "shaders.h"

#include <bgfx/bgfx.h>

#include <cstring>
#include <iterator>
#include <string>

#include "efk_sprite_unlit/includes.inc"
#include "efk_sprite_lit/includes.inc"
#include "efk_sprite_distortion/includes.inc"
#include "efk_sprite_adv_unlit/includes.inc"
#include "efk_sprite_adv_lit/includes.inc"
#include "efk_sprite_adv_distortion/includes.inc"
#include "efk_model_unlit/includes.inc"
#include "efk_model_lit/includes.inc"
#include "efk_model_distortion/includes.inc"
#include "efk_model_adv_unlit/includes.inc"
#include "efk_model_adv_lit/includes.inc"
#include "efk_model_adv_distortion/includes.inc"

namespace EffekseerRendererBGFX
{
namespace
{
struct EmbeddedShader
{
    const char *name;
    const uint8_t *data;
    uint32_t size;
};
#define STORM_SHADER(name) {#name, name, sizeof(name)},
const EmbeddedShader embedded[] = {
#include "efk_sprite_unlit/table.inc"
#include "efk_sprite_lit/table.inc"
#include "efk_sprite_distortion/table.inc"
#include "efk_sprite_adv_unlit/table.inc"
#include "efk_sprite_adv_lit/table.inc"
#include "efk_sprite_adv_distortion/table.inc"
#include "efk_model_unlit/table.inc"
#include "efk_model_lit/table.inc"
#include "efk_model_distortion/table.inc"
#include "efk_model_adv_unlit/table.inc"
#include "efk_model_adv_lit/table.inc"
#include "efk_model_adv_distortion/table.inc"
};
#undef STORM_SHADER
} // namespace

bgfx_shader_handle_t LoadShader(const char *name, bool vertex)
{
    // Sprites share the fragment shaders of the models: "sprite_adv_lit" -> fs_efk_model_adv_lit.
    std::string blob = vertex ? "vs_efk_" : "fs_efk_";
    if (!vertex && std::strncmp(name, "sprite_", 7) == 0)
        blob += std::string("model_") + (name + 7);
    else
        blob += name;
    for (const EmbeddedShader &shader : embedded)
    {
        if (blob == shader.name)
        {
            const bgfx::ShaderHandle handle = bgfx::createShader(bgfx::makeRef(shader.data, shader.size));
            bgfx::setName(handle, shader.name);
            return {handle.idx};
        }
    }
    return {UINT16_MAX};
}
} // namespace EffekseerRendererBGFX
