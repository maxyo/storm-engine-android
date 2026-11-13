// An embedded shader for both backends of the engine (cmake/storm_shaders.cmake, fx/cmake/storm_fx_shaders.cmake):
// the ESSL blob for OpenGL ES and the SPIR-V blob for Vulkan. Header only; needs bgfx alone.
#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

namespace storm::render
{

struct ShaderBlob
{
    const uint8_t *essl = nullptr;
    uint32_t essl_size = 0;
    const uint8_t *spirv = nullptr;
    uint32_t spirv_size = 0;
};

// The two arrays of "<namespace>/<name>.bin.h" and "<namespace>/<name>.spv.bin.h".
#define STORM_SHADER_BLOB(name)                                                                                        \
    ::storm::render::ShaderBlob                                                                                        \
    {                                                                                                                  \
        name, sizeof(name), name##_spv, sizeof(name##_spv)                                                             \
    }

// The shader for the backend bgfx runs (after bgfx::init). The blobs live as long as the program (static arrays).
// The Noop backend (headless checks) only reads a blob's header, which every blob has: it gets the ESSL one.
inline bgfx::ShaderHandle CreateShader(const ShaderBlob &blob, const char *name)
{
    const bool vulkan = bgfx::getRendererType() == bgfx::RendererType::Vulkan;
    const bgfx::ShaderHandle shader =
        bgfx::createShader(vulkan ? bgfx::makeRef(blob.spirv, blob.spirv_size) : bgfx::makeRef(blob.essl, blob.essl_size));
    if (bgfx::isValid(shader))
        bgfx::setName(shader, name);
    return shader;
}

} // namespace storm::render
