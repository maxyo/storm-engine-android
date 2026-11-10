// Hand-ported programmable shaders (vs_1_1 / ps_1_x bytecode -> bgfx shaders),
// looked up by the FNV-1a hash of the original D3D9 bytecode.
#pragma once
#include <cstdint>

namespace storm::shim
{
struct ShaderPort
{
    uint64_t hash;
    bool vertex;
    const char *shader_name; // embedded bgfx shader
    uint8_t sampler_count;   // pixel ports: texture stages to bind
    uint32_t shader_id() const;
};
const ShaderPort *find_port(uint64_t hash, bool vertex);
} // namespace storm::shim
