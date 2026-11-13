// The backend choice (src/shim/backend_select.hpp) without a GPU: what DecideBackend answers for each request and
// device, and the marker of a Vulkan start that never presented a frame (BackendAttempt).
#include "backend_select.hpp"

#include <cstdio>
#include <filesystem>
#include <unistd.h>

using namespace storm;
using shim::BackendAttempt;
using shim::DecideBackend;
using shim::VulkanDevice;

namespace
{
bool expect(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    return ok;
}

VulkanDevice device(uint32_t major, uint32_t minor)
{
    VulkanDevice out;
    out.name = "Test GPU";
    out.api_version = major << 22 | minor << 12;
    out.driver_version = 7;
    out.vendor_id = 0x5143;
    out.device_id = 0x43050a01;
    out.swapchain = true;
    return out;
}
} // namespace

int main()
{
    bool ok = true;
    const VulkanDevice good = device(1, 3);
    VulkanDevice none;
    none.problem = "no Vulkan loader on this system (libvulkan)";
    VulkanDevice old = device(1, 0);
    VulkanDevice bare = device(1, 1);
    bare.swapchain = false;

    ok &= expect(!DecideBackend(render::Backend::OpenGLES, good, false).vulkan, "gles asked: OpenGL ES on a Vulkan device");
    ok &= expect(DecideBackend(render::Backend::Vulkan, good, false).vulkan, "vulkan asked: Vulkan on a 1.3 device");
    ok &= expect(DecideBackend(render::Backend::Vulkan, device(1, 1), false).vulkan, "vulkan asked: Vulkan on a 1.1 device");
    ok &= expect(!DecideBackend(render::Backend::Vulkan, old, false).vulkan, "vulkan asked: not on a 1.0 device");
    ok &= expect(!DecideBackend(render::Backend::Vulkan, none, false).vulkan, "vulkan asked: not without a loader");
    ok &= expect(!DecideBackend(render::Backend::Vulkan, bare, false).vulkan, "vulkan asked: not without a swap chain");
    ok &= expect(!DecideBackend(render::Backend::Vulkan, good, true).vulkan,
                 "vulkan asked: not after a Vulkan start that never presented");
    for (const VulkanDevice *each : {&none, &old, &bare})
        ok &= expect(!DecideBackend(render::Backend::Vulkan, *each, false).reason.empty(), "every refusal has a reason");
    // auto follows the vendors measured faster on Vulkan, and never goes where vulkan asked for does not.
    const bool by_default = shim::VulkanByDefault(good.vendor_id);
    ok &= expect(DecideBackend(render::Backend::Auto, good, false).vulkan == by_default,
                 "auto: Vulkan exactly for the vendors listed as default");
    ok &= expect(!DecideBackend(render::Backend::Auto, old, false).vulkan && !DecideBackend(render::Backend::Auto, none, false).vulkan &&
                     !DecideBackend(render::Backend::Auto, good, true).vulkan,
                 "auto: never where a Vulkan asked for is refused");

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("storm-backend-select-check-" + std::to_string(getpid()));
    std::filesystem::remove_all(directory);
    {
        BackendAttempt first(directory, good);
        ok &= expect(!first.failed_before(), "no marker: no failed start");
        first.begin();
    }
    ok &= expect(BackendAttempt(directory, good).failed_before(), "a start that never confirmed is found at the next start");
    VulkanDevice updated = good;
    updated.driver_version = 8;
    ok &= expect(!BackendAttempt(directory, updated).failed_before(), "... but says nothing about another driver version");
    {
        BackendAttempt again(directory, good);
        again.begin();
        again.succeed();
    }
    ok &= expect(!BackendAttempt(directory, good).failed_before(), "a confirmed start leaves no marker");
    std::filesystem::remove_all(directory);
    std::printf("%s: backend_select_check\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
