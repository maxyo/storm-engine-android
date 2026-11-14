// The package's programs by the names of their embedded shaders (shaders/shaders.cmake), for a caller that knows a
// program only by those names (the renderer's recorded Vulkan pipelines, src/shim/pipeline_sets.hpp).
#pragma once

#include <bgfx/bgfx.h>

namespace storm::fx
{

// The program of two of the package's shaders; invalid, and nothing logged, when either name is not one of them.
// The caller owns the handle (bgfx shares one program between the callers of the same shaders).
bgfx::ProgramHandle FindProgram(const char *vertex, const char *fragment);

} // namespace storm::fx
