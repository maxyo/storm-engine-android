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
    uint8_t vsc_count;       // vertex ports: constant registers c0.. the shader declares (PORT_VSC_COUNT)
    bool slope_bias = false; // pixel ports compiled with STORM_SLOPE_BIAS (their technique sets the bias)
    bool sea_fx = false;     // the Sea2 ports: effects::Host::prepare_sea sets u_seaFx / s_seaDepth and the waves (sea_waves.sh)
    uint32_t shader_id() const;
};
// Reference blob: kShaderRefMagic, the key bytes zero-padded to whole DWORDs, 0x0000FFFF.
inline constexpr uint32_t kShaderRefMagic = 0x4D525453; // "STRM"
const ShaderPort *find_port(std::string_view key, bool vertex);
} // namespace storm::shim
