#include "ports.hpp"
#include "device.hpp"

namespace storm::shim
{
namespace
{
// The 15 vs_1_1 / ps_1_x shaders referenced by src/techniques (see shaders/ports).
const ShaderPort ports[] = {
    {"grass_main", true, "vs_port_grass", 0},
    {"particles_vertex_shader", true, "vs_port_particles", 0},
    {"particles_pixel_shader", false, "fs_port_particles", 4},
    {"postprocess_shader", false, "fs_port_postprocess", 4},
    {"caustic_vertex_shader", true, "vs_port_caustic", 0},
    {"caustic_pixel_shader", false, "fs_port_caustic", 3},
    {"sea_sea2_vertex_shader", true, "vs_port_sea2", 0},
    {"sea_sea2_pixel_shader", false, "fs_port_sea2", 4},
    {"sea_sea2foam_vertex_shader", true, "vs_port_sea2foam", 0},
    {"sea_sea2foam_pixel_shader", false, "fs_port_sea2foam", 5},
    {"sea_sea2sunroad_vertex_shader", true, "vs_port_sea2sunroad", 0},
    {"sea_sea2sunroad_pixel_shader", false, "fs_port_sea2sunroad", 4},
    {"sea_sea3_vertex_shader", true, "vs_port_sea3", 0},
    {"sea_sea3_pixel_shader", false, "fs_port_sea3", 4},
    {"worldmap_shader", true, "vs_port_worldmap", 0},
};
} // namespace

uint32_t ShaderPort::shader_id() const
{
    return shader_id_by_name(shader_name);
}

const ShaderPort *find_port(std::string_view key, bool vertex)
{
    for (const ShaderPort &port : ports)
        if (port.key == key && port.vertex == vertex)
            return &port;
    return nullptr;
}
} // namespace storm::shim
