#include "ports.hpp"
#include "device.hpp"

namespace storm::shim
{
namespace
{
// Filled in as the 15 bytecode files of src/techniques are ported (see shaders/ports).
const ShaderPort ports[] = {
    {0, false, nullptr, 0},
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
