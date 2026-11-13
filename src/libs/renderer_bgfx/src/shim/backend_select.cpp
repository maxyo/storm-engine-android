#include "backend_select.hpp"

#include "shim.hpp"

// The Vulkan headers bgfx itself compiles against (bgfx/3rdparty/khronos); no prototypes: every function is looked
// up in the loader this file opens.
#define VK_NO_PROTOTYPES
#define VK_NO_STDINT_H
#include <vulkan-local/vulkan_core.h>

#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <system_error>
#include <vector>

namespace storm::shim
{

namespace
{
// Closes the loader when the probe is done, whichever way it leaves.
struct Loader
{
    void *library = nullptr;
    ~Loader()
    {
        if (library)
            dlclose(library);
    }
    template <typename Function> Function symbol(const char *name) const
    {
        return reinterpret_cast<Function>(dlsym(library, name));
    }
};

std::string version_text(uint32_t version)
{
    char text[32];
    snprintf(text, sizeof(text), "%u.%u.%u", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version),
             VK_API_VERSION_PATCH(version));
    return text;
}

// bgfx's order (renderer_vk.cpp, the physical device loop): a discrete GPU, else an integrated one, else the first.
int rank(VkPhysicalDeviceType type)
{
    return type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : type == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 1 : 0;
}
} // namespace

std::string VulkanDevice::describe() const
{
    if (!found())
        return problem;
    char text[512];
    snprintf(text, sizeof(text), "%s, Vulkan %s, driver 0x%x, vendor 0x%x device 0x%x%s", name.c_str(),
             version_text(api_version).c_str(), driver_version, vendor_id, device_id, software ? " (software)" : "");
    return text;
}

std::string VulkanDevice::cache_key() const
{
    char uuid[33];
    for (int i = 0; i < 16; ++i)
        snprintf(uuid + 2 * i, 3, "%02x", pipeline_cache_uuid[i]);
    return describe() + " | pipeline cache " + uuid;
}

VulkanDevice ProbeVulkan()
{
    VulkanDevice out;
    Loader loader;
#ifdef __ANDROID__
    loader.library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#else
    loader.library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
    if (!loader.library)
    {
        out.problem = "no Vulkan loader on this system (libvulkan)";
        return out;
    }
    const auto create_instance = loader.symbol<PFN_vkCreateInstance>("vkCreateInstance");
    const auto destroy_instance = loader.symbol<PFN_vkDestroyInstance>("vkDestroyInstance");
    const auto enumerate_devices = loader.symbol<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
    const auto device_properties = loader.symbol<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties");
    const auto device_features = loader.symbol<PFN_vkGetPhysicalDeviceFeatures>("vkGetPhysicalDeviceFeatures");
    const auto device_extensions =
        loader.symbol<PFN_vkEnumerateDeviceExtensionProperties>("vkEnumerateDeviceExtensionProperties");
    if (!create_instance || !destroy_instance || !enumerate_devices || !device_properties || !device_features ||
        !device_extensions)
    {
        out.problem = "the Vulkan loader lacks core entry points";
        return out;
    }
    // Vulkan 1.0 loaders have no vkEnumerateInstanceVersion.
    out.instance_version = VK_API_VERSION_1_0;
    if (const auto instance_version = loader.symbol<PFN_vkEnumerateInstanceVersion>("vkEnumerateInstanceVersion"))
        instance_version(&out.instance_version);
    if (out.instance_version < VK_API_VERSION_1_1)
    {
        out.problem = "the Vulkan loader is " + version_text(out.instance_version) + ", older than 1.1";
        return out;
    }
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "storm-engine backend probe";
    application.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create.pApplicationInfo = &application;
    VkInstance instance = VK_NULL_HANDLE;
    if (const VkResult result = create_instance(&create, nullptr, &instance); result != VK_SUCCESS)
    {
        out.problem = "vkCreateInstance failed (" + std::to_string(result) + ")";
        return out;
    }
    uint32_t count = 0;
    enumerate_devices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    if (count == 0 || enumerate_devices(instance, &count, devices.data()) < VK_SUCCESS)
    {
        destroy_instance(instance, nullptr);
        out.problem = "Vulkan reports no physical device";
        return out;
    }
    devices.resize(count);
    VkPhysicalDevice chosen = devices[0];
    VkPhysicalDeviceProperties properties{};
    device_properties(chosen, &properties);
    for (const VkPhysicalDevice device : devices)
    {
        VkPhysicalDeviceProperties candidate{};
        device_properties(device, &candidate);
        if (rank(candidate.deviceType) > rank(properties.deviceType))
        {
            chosen = device;
            properties = candidate;
        }
    }
    VkPhysicalDeviceFeatures features{};
    device_features(chosen, &features);
    uint32_t extension_count = 0;
    device_extensions(chosen, nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> extensions(extension_count);
    if (extension_count)
        device_extensions(chosen, nullptr, &extension_count, extensions.data());
    extensions.resize(extension_count);
    const auto has = [&](const char *name) {
        for (const VkExtensionProperties &extension : extensions)
            if (std::strcmp(extension.extensionName, name) == 0)
                return true;
        return false;
    };
    out.name = properties.deviceName;
    out.api_version = properties.apiVersion;
    out.driver_version = properties.driverVersion;
    out.vendor_id = properties.vendorID;
    out.device_id = properties.deviceID;
    out.software = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
    std::memcpy(out.pipeline_cache_uuid, properties.pipelineCacheUUID, sizeof(out.pipeline_cache_uuid));
    // bgfx enables both by name when it creates the device (renderer_vk.cpp).
    out.swapchain = has(VK_KHR_SWAPCHAIN_EXTENSION_NAME) && has(VK_KHR_MAINTENANCE1_EXTENSION_NAME);
    out.border_color = has(VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME);
    out.anisotropy = features.samplerAnisotropy == VK_TRUE;
    out.max_anisotropy = out.anisotropy ? properties.limits.maxSamplerAnisotropy : 0.0f;
    out.large_points = features.largePoints == VK_TRUE;
    out.max_point_size = out.large_points ? properties.limits.pointSizeRange[1] : 1.0f;
    destroy_instance(instance, nullptr);
    return out;
}

bool VulkanByDefault(uint32_t)
{
    // No GPU family yet: the first device measurements (OpenGL ES against Vulkan, the same build) are pending.
    return false;
}

BackendDecision DecideBackend(render::Backend request, const VulkanDevice &device, bool failed_attempt)
{
    if (request == render::Backend::OpenGLES)
        return {false, "render_backend asks for OpenGL ES"};
    const bool asked = request == render::Backend::Vulkan;
    const char *prefix = asked ? "render_backend asks for Vulkan, but " : "";
    if (!device.found())
        return {false, prefix + device.problem};
    if (device.api_version < VK_API_VERSION_1_1)
        return {false, prefix + ("the device has Vulkan " + version_text(device.api_version) + ", older than 1.1")};
    if (!device.swapchain)
        return {false, std::string(prefix) + "the device lacks VK_KHR_swapchain or VK_KHR_maintenance1"};
    if (failed_attempt)
        return {false, std::string(prefix) + "the last Vulkan start on this device and driver never presented a frame "
                                             "(the marker of that start is still in the cache)"};
    if (asked)
        return {true, "render_backend asks for Vulkan"};
    if (!VulkanByDefault(device.vendor_id))
        return {false, "Vulkan is available but not the default for this GPU vendor (no device measurement favours "
                       "it yet); render_backend = vulkan asks for it"};
    return {true, "Vulkan is the default for this GPU vendor"};
}

BackendAttempt::BackendAttempt(std::filesystem::path directory, const VulkanDevice &device) : key_(device.cache_key())
{
    if (directory.empty())
    {
        logf("render backend: no cache directory configured: a Vulkan start that kills the process is not remembered");
        return;
    }
    file_ = std::move(directory) / "vulkan-start";
    std::ifstream in(file_);
    std::string stored;
    if (in && std::getline(in, stored))
        failed_before_ = stored == key_; // another device or driver: its failure says nothing about this one
}

void BackendAttempt::begin()
{
    if (file_.empty())
        return;
    std::error_code error;
    std::filesystem::create_directories(file_.parent_path(), error);
    std::ofstream out(file_, std::ios::trunc);
    out << key_ << '\n';
    out.flush();
    if (!out)
        logf("ERROR: render backend: cannot write %s: a Vulkan start that kills the process is not remembered",
             file_.string().c_str());
}

void BackendAttempt::succeed()
{
    if (file_.empty())
        return;
    std::error_code error;
    std::filesystem::remove(file_, error);
}

} // namespace storm::shim
