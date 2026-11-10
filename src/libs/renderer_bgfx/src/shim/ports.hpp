// Hand-ported programmable shaders of src/techniques, looked up by the technique shader key
// (`<fx base name>_<shader name>`, lower case) that CTechnique passes in a reference blob.
#pragma once
#include <cstdint>
#include <string_view>

namespace storm::shim
{
struct ShaderPort
{
    const char *key;
    bool vertex;
    const char *shader_name; // embedded bgfx shader
    uint8_t sampler_count;   // pixel ports: texture stages to bind
    uint32_t shader_id() const;
};
// Reference blob: kShaderRefMagic, the key bytes zero-padded to whole DWORDs, 0x0000FFFF.
inline constexpr uint32_t kShaderRefMagic = 0x4D525453; // "STRM"
const ShaderPort *find_port(std::string_view key, bool vertex);
} // namespace storm::shim
