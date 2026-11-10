#include "ports.hpp"
#include "device.hpp"

namespace storm::shim
{
namespace
{
// The 15 bytecode files of src/techniques (see shaders/ports); the vertex ports are
// literal per-instruction translations of the .vso files.
const ShaderPort ports[] = {
    {0x7e4096e4a06244bdull, true, "vs_port_grass", 0},       // effects/grass_main.vso
    {0x9fec30aae083e760ull, true, "vs_port_particles", 0},   // particles/particles_vertex_shader.vso
    {0xbe0238d886253bb2ull, false, "fs_port_particles", 4},  // particles/particles_pixel_shader.pso
    {0xd2b0fc22df71061bull, false, "fs_port_postprocess", 4}, // postprocess/postprocess_shader.pso
    {0xabfb8325bc32d5e2ull, true, "vs_port_caustic", 0},     // weather/caustic_vertex_shader.vso
    {0xf1f78a40cf8ffd66ull, false, "fs_port_caustic", 3},    // weather/caustic_pixel_shader.pso
    {0xa3b24dd0b5a5acdaull, true, "vs_port_sea2", 0},        // weather/sea_sea2_vertex_shader.vso
    {0x2c35e14750a293fdull, false, "fs_port_sea2", 4},       // weather/sea_sea2_pixel_shader.pso
    {0x50724131f31f9df8ull, true, "vs_port_sea2foam", 0},    // weather/sea_sea2foam_vertex_shader.vso
    {0x62cad3c5d353e242ull, false, "fs_port_sea2foam", 5},   // weather/sea_sea2foam_pixel_shader.pso
    {0x4ca912ed61d6c345ull, true, "vs_port_sea2sunroad", 0}, // weather/sea_sea2sunroad_vertex_shader.vso
    {0x84b5a434e9dd321bull, false, "fs_port_sea2sunroad", 4}, // weather/sea_sea2sunroad_pixel_shader.pso
    {0xd285557cce4daf98ull, true, "vs_port_sea3", 0},        // weather/sea_sea3_vertex_shader.vso
    {0xbc1a95c3d1f059a4ull, false, "fs_port_sea3", 4},       // weather/sea_sea3_pixel_shader.pso
    {0x64d4d2edd7ef1bcdull, true, "vs_port_worldmap", 0},    // worldmap/worldmap_shader.vso
};
} // namespace

uint32_t ShaderPort::shader_id() const
{
    return shader_id_by_name(shader_name);
}

const ShaderPort *find_port(uint64_t hash, bool vertex)
{
    for (const ShaderPort &port : ports)
        if (port.shader_name && port.hash == hash && port.vertex == vertex)
            return &port;
    return nullptr;
}
} // namespace storm::shim
