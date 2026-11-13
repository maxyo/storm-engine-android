// The backend a GPU check runs on. run.py runs every check on OpenGL ES, then again with STORM_CHECK_BACKEND=vulkan
// where the host has a Vulkan device, and looks for the shim's "render backend: Vulkan" line in that run's output.
#pragma once

#include "storm/render/frame_owner.hpp"

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace storm::host_check
{
// The run asks for Vulkan (expectations that depend on the backend).
inline bool VulkanAsked()
{
    const char *value = std::getenv("STORM_CHECK_BACKEND");
    return value && std::string_view(value) == "vulkan";
}

// Before the device is created.
inline void UseBackendFromEnvironment()
{
    const char *value = std::getenv("STORM_CHECK_BACKEND");
    const std::string_view backend = value ? value : "gles";
    if (backend != "gles" && backend != "vulkan")
    {
        std::fprintf(stderr, "STORM_CHECK_BACKEND=%s is not gles or vulkan\n", value);
        std::exit(2);
    }
    render::SetRenderBackend(backend == "vulkan" ? render::Backend::Vulkan : render::Backend::OpenGLES, {});
}
} // namespace storm::host_check
