// Android integration probe only. Does not load or impersonate the game engine.
#include "platform/android_runtime.hpp"
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <android/log.h>
#include <fstream>
#include <vector>

extern "C" int SDL_main(int, char **)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
        return 1;
    SDL_Window *window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    int result = 1;
    try
    {
        const auto paths = storm::android::storagePaths();
        std::filesystem::create_directories(paths.game);
        std::filesystem::create_directories(paths.user / "Logs");
        std::ofstream log(paths.user / "Logs/android-probe.log");
        if (!log) throw std::runtime_error("Cannot write probe log");
        log << "Integration probe; not the Storm engine\ngame=" << paths.game << "\nuser=" << paths.user << std::endl;
        window = SDL_CreateWindow("Storm Android integration probe", 0, 0, 0, 0,
                                  SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN);
        if (!window) throw std::runtime_error(SDL_GetError());
        unsigned count = 0;
        if (!SDL_Vulkan_GetInstanceExtensions(window, &count, nullptr))
            throw std::runtime_error(SDL_GetError());
        std::vector<const char *> extensions(count);
        if (!SDL_Vulkan_GetInstanceExtensions(window, &count, extensions.data()))
            throw std::runtime_error(SDL_GetError());
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Storm integration probe";
        app.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = count;
        info.ppEnabledExtensionNames = extensions.data();
        if (vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS)
            throw std::runtime_error("vkCreateInstance failed");
        if (!SDL_Vulkan_CreateSurface(window, instance, &surface))
            throw std::runtime_error(SDL_GetError());
        uint32_t gpuCount = 0;
        if (vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr) != VK_SUCCESS || !gpuCount)
            throw std::runtime_error("No Vulkan physical device");
        log << "Vulkan surface created; GPU count=" << gpuCount << std::endl;
        __android_log_print(ANDROID_LOG_INFO, "StormProbe", "Vulkan surface created; GPU count=%u", gpuCount);
        storm::android::Lifecycle lifecycle;
        while (!lifecycle.terminating())
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                const bool wasActive = lifecycle.canRun();
                lifecycle.consume(event.type);
                if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_AC_BACK)
                    lifecycle.consume(SDL_QUIT);
                if (wasActive && !lifecycle.canRun() && surface)
                {
                    vkDestroySurfaceKHR(instance, surface, nullptr);
                    surface = VK_NULL_HANDLE;
                }
                if (!wasActive && lifecycle.canRun() && !surface &&
                    !SDL_Vulkan_CreateSurface(window, instance, &surface))
                    throw std::runtime_error(SDL_GetError());
                if (event.type >= SDL_APP_TERMINATING && event.type <= SDL_APP_DIDENTERFOREGROUND)
                    log << "lifecycle=" << event.type << " active=" << lifecycle.canRun() << std::endl;
                if (event.type == SDL_FINGERDOWN)
                    log << "touch=" << event.tfinger.x << "," << event.tfinger.y << std::endl;
            }
            SDL_Delay(lifecycle.canRun() ? 16 : 50);
        }
        log << "clean shutdown" << std::endl;
        result = 0;
    }
    catch (const std::exception &error)
    {
        __android_log_print(ANDROID_LOG_ERROR, "StormProbe", "%s", error.what());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Integration probe failed", error.what(), window);
    }
    if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
    if (instance) vkDestroyInstance(instance, nullptr);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
