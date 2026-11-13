// Which bgfx backend the game window runs: Vulkan or OpenGL ES (storm/render/frame_owner.hpp, SetRenderBackend).
//
// The choice is made before bgfx::init from three things: what was asked for (engine.ini render_backend, the
// Android graphics setting of the same name), what the system's Vulkan says about itself (ProbeVulkan: the loader
// and the device bgfx would take, read without bgfx) and whether an earlier Vulkan start on this driver never got
// to a presented frame (BackendAttempt). Vulkan then still has to initialise in bgfx and pass the backend self-test
// (Context::start); if it does not, the device is started again on OpenGL ES. Every outcome is one
// "render backend:" log line with the reason (the application's diagnostics export keeps it).
//
// OpenGL ES is not a fallback kept "just in case": it is the backend of every device Vulkan is not chosen for.
#pragma once

#include "storm/render/frame_owner.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace storm::shim
{

// What the Vulkan loader and its preferred physical device report. `problem` is empty when a device was read.
struct VulkanDevice
{
    std::string problem;       // why there is no usable answer (no loader, no instance, no device ...)
    uint32_t instance_version = 0;
    std::string name;          // VkPhysicalDeviceProperties::deviceName
    uint32_t api_version = 0;  // of the device
    uint32_t driver_version = 0;
    uint32_t vendor_id = 0, device_id = 0;
    bool software = false;     // VK_PHYSICAL_DEVICE_TYPE_CPU (lavapipe, SwiftShader)
    uint8_t pipeline_cache_uuid[16] = {};
    bool swapchain = false;    // VK_KHR_swapchain and VK_KHR_maintenance1: bgfx's device needs both
    bool border_color = false; // VK_EXT_custom_border_color: D3DSAMP_BORDERCOLOR other than opaque black
    bool anisotropy = false;   // samplerAnisotropy
    float max_anisotropy = 0.0f;
    bool large_points = false; // largePoints: point lists wider than one pixel (the stars)
    float max_point_size = 1.0f;

    bool found() const { return problem.empty(); }
    // "Adreno (TM) 740, Vulkan 1.3.128, driver 0x80273000, vendor 0x5143 device 0x43050a01" or the problem.
    std::string describe() const;
    // What a cache of this device's pipelines is valid for: device, driver and the driver's own cache id.
    std::string cache_key() const;
};

// Loads the system's Vulkan loader, creates an instance and reads the physical device bgfx's Vulkan backend would
// take (a discrete GPU, else an integrated one, else the first). Nothing stays loaded or alive afterwards.
VulkanDevice ProbeVulkan();

struct BackendDecision
{
    bool vulkan = false;
    std::string reason;
};

// GPU vendors (VkPhysicalDeviceProperties::vendorID) whose devices run Vulkan when nothing is asked for
// (render_backend = auto). Filled from measurements on real devices only (local-testing/vulkan in the workspace);
// a vendor that is not listed runs OpenGL ES unless Vulkan is asked for.
bool VulkanByDefault(uint32_t vendor_id);

// Pure: the backend to try first. `failed_attempt`: BackendAttempt::failed_before() for this device.
BackendDecision DecideBackend(render::Backend request, const VulkanDevice &device, bool failed_attempt);

// Guards a Vulkan start against a driver that kills the process inside it: a marker file is written before
// bgfx::init and removed when frames have been presented. A marker found at start for the same device and driver
// means the last Vulkan start never got that far.
class BackendAttempt
{
  public:
    // `directory`: under the cache root (render::ProgramCacheConfig::directory's parent); empty: no guard, logged.
    BackendAttempt(std::filesystem::path directory, const VulkanDevice &device);
    bool failed_before() const { return failed_before_; }
    void begin();   // before bgfx::init on Vulkan
    void succeed(); // frames were presented, or Vulkan was given up in an orderly way (reason logged by the caller)

  private:
    std::filesystem::path file_;
    std::string key_;
    bool failed_before_ = false;
};

} // namespace storm::shim
