#include "frame_pacing.hpp"
#include "swap_interval.hpp"

#include "shim.hpp"

#include <utility>

#ifdef __ANDROID__
#include "platform/host_hooks.hpp"
#include "profiler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <EGL/egl.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_system.h>
#include <android/log.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <jni.h>
#include <swappy/swappyGL.h>
#include <swappy/swappyGL_extra.h>
#include <swappy/swappyVk.h>

namespace bgfx
{
// patches/bgfx-swap-present-hook.patch (src/glcontext_egl.cpp) defines these two.
typedef bool (*EglPresentFn)(void *_display, void *_surface);
void setEglPresent(EglPresentFn _present);
// patches/bgfx-vk-present-hook.patch (src/renderer_vk.cpp) defines these two.
struct VkPresentHooks
{
    void (*swapChainCreated)(void *_physicalDevice, void *_device, void *_queue, uint32_t _queueFamily, uint64_t _swapChain,
                             void *_nwh);
    void (*swapChainDestroying)(void *_device, uint64_t _swapChain);
    int32_t (*present)(void *_queue, const void *_presentInfo);
};
void setVkPresentHooks(const VkPresentHooks *_hooks);
} // namespace bgfx
#endif

namespace storm::shim
{

#ifdef __ANDROID__
namespace
{
using Clock = std::chrono::steady_clock;
constexpr auto kStatsWindow = std::chrono::seconds(30);
constexpr size_t kMaxSamples = 4096; // set_sampling: the game thread takes them every frame

double to_ms(std::chrono::nanoseconds value)
{
    return std::chrono::duration<double, std::milli>(value).count();
}

void log_error(const char *message)
{
    __android_log_print(ANDROID_LOG_ERROR, "storm-bgfx", "%s", message);
    fprintf(stderr, "[storm-bgfx] ERROR: %s\n", message);
}

// The refresh period (ns) the display runs now, from Android (Display.getRefreshRate of the activity's window
// manager's default display; game thread, through SDL's JNI environment); 0 when it cannot be read (logged).
uint64_t display_refresh_period()
{
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = env ? static_cast<jobject>(SDL_AndroidGetActivity()) : nullptr;
    if (!activity)
    {
        log_error("frame pacing: no JNI environment or activity to read the display's refresh rate");
        return 0;
    }
    float rate = 0.0f;
    jclass activity_class = env->GetObjectClass(activity);
    jmethodID window_manager = env->GetMethodID(activity_class, "getWindowManager", "()Landroid/view/WindowManager;");
    jobject manager = window_manager ? env->CallObjectMethod(activity, window_manager) : nullptr;
    jclass manager_class = manager ? env->GetObjectClass(manager) : nullptr;
    jmethodID default_display = manager_class ? env->GetMethodID(manager_class, "getDefaultDisplay", "()Landroid/view/Display;") : nullptr;
    jobject display = default_display ? env->CallObjectMethod(manager, default_display) : nullptr;
    jclass display_class = display ? env->GetObjectClass(display) : nullptr;
    jmethodID refresh = display_class ? env->GetMethodID(display_class, "getRefreshRate", "()F") : nullptr;
    if (refresh)
        rate = env->CallFloatMethod(display, refresh);
    if (env->ExceptionCheck())
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        rate = 0.0f;
    }
    for (jobject local : {static_cast<jobject>(display_class), display, static_cast<jobject>(manager_class), manager,
                          static_cast<jobject>(activity_class), activity})
        if (local)
            env->DeleteLocalRef(local);
    if (!(rate > 1.0f))
    {
        log_error("frame pacing: Display.getRefreshRate gave no rate; the display mode is not checked");
        return 0;
    }
    return static_cast<uint64_t>(1e9 / rate + 0.5);
}

// The Vulkan handles as bgfx's hooks pass them: a swap chain is 64 bits, a pointer on a 64-bit ABI.
VkSwapchainKHR swap_chain_handle(uint64_t value)
{
#if defined(__LP64__)
    return reinterpret_cast<VkSwapchainKHR>(value);
#else
    return static_cast<VkSwapchainKHR>(value);
#endif
}
} // namespace
#endif

FramePacing &FramePacing::instance()
{
    static FramePacing pacing;
    return pacing;
}

#ifdef __ANDROID__

void FramePacing::start(bool render_thread, bool vulkan)
{
    if (started_)
        return;
    started_ = true;
    render_thread_ = render_thread;
    vulkan_ = vulkan;
    if (vulkan_)
    {
        // Swappy Vk is set up per swap chain (vk_swap_chain_created); until one exists nothing is paced. What needs
        // no swap chain is set here.
        SwappyVk_setAutoSwapInterval(true);
        SwappyVk_setAutoPipelineMode(true);
        vulkan_library_ = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        vk_queue_present_ = vulkan_library_ ? reinterpret_cast<int32_t (*)(void *, const void *)>(
                                                  dlsym(vulkan_library_, "vkQueuePresentKHR"))
                                            : nullptr;
        if (!vk_queue_present_)
            log_error("frame pacing: the Vulkan loader has no vkQueuePresentKHR: without Swappy nothing can be presented");
        preferred_ = target_;
        storm::platform::frameWorkTarget(target_);
        static const bgfx::VkPresentHooks hooks = {&FramePacing::vk_swap_chain_created,
                                                   &FramePacing::vk_swap_chain_destroying, &FramePacing::vk_present};
        bgfx::setVkPresentHooks(&hooks);
        reset_stats();
        return;
    }
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = env ? static_cast<jobject>(SDL_AndroidGetActivity()) : nullptr;
    const bool initialised = activity && SwappyGL_init(env, activity);
    if (activity)
        env->DeleteLocalRef(activity);
    swappy_ = initialised && SwappyGL_isEnabled();
    if (swappy_)
    {
        // The scheduler and the ADPF hint session place the game thread; Swappy's pinning would fight them.
        SwappyGL_setUseAffinity(false);
        SwappyGL_setAutoSwapInterval(true);
        SwappyGL_setAutoPipelineMode(true);
        choose_interval();
    }
    else
    {
        if (activity)
            SwappyGL_destroy(); // whatever SwappyGL_init left behind
        unpaced(activity ? "SwappyGL_init failed or the device disables it, see the Swappy lines above"
                         : "no JNI environment or activity for the game thread");
        preferred_ = target_;
        storm::platform::frameWorkTarget(target_);
    }
    // Both ways the present goes through present(): the same frame times and present wait.
    bgfx::setEglPresent(&FramePacing::present);
    reset_stats();
}

void FramePacing::unpaced(const char *why)
{
    // The display's rate for the statistics; SDL's video calls belong to the game thread, where only the OpenGL ES
    // start gets here (the Vulkan swap chain is created on the presenting thread: 60 Hz refreshes are assumed).
    SDL_DisplayMode mode{};
    if (!vulkan_ && SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.refresh_rate > 0)
    {
        std::lock_guard lock(mutex_);
        refresh_period_ = std::chrono::nanoseconds(1'000'000'000LL / mode.refresh_rate);
    }
    char message[512];
    snprintf(message, sizeof(message),
             "frame pacing: Swappy %s unavailable (%s); presenting with %s, the display's own rate (%s), unpaced; ADPF "
             "aims at %.2f ms; frame times are still logged",
             vulkan_ ? "Vk" : "GL", why,
             vulkan_ ? "vkQueuePresentKHR on the FIFO swap chain" : "eglSwapBuffers at swap interval 1",
             mode.refresh_rate > 0 ? (std::to_string(mode.refresh_rate) + " Hz").c_str() : "refresh unknown to SDL",
             to_ms(target_));
    log_error(message);
}

void FramePacing::vk_swap_chain_created(void *physical_device, void *device, void *queue, uint32_t queue_family,
                                        uint64_t swap_chain, void *native_window)
{
    FramePacing &pacing = instance();
    // SDL attaches the presenting thread to the Java VM on first use.
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = env ? static_cast<jobject>(SDL_AndroidGetActivity()) : nullptr;
    const auto vk_device = static_cast<VkDevice>(device);
    const VkSwapchainKHR vk_swap_chain = swap_chain_handle(swap_chain);
    uint64_t refresh = 0;
    bool paced = false;
    std::lock_guard chain(pacing.chain_mutex_);
    if (activity)
    {
        SwappyVk_setQueueFamilyIndex(vk_device, static_cast<VkQueue>(queue), queue_family);
        paced = SwappyVk_initAndGetRefreshCycleDuration(env, activity, static_cast<VkPhysicalDevice>(physical_device),
                                                        vk_device, vk_swap_chain, &refresh);
        env->DeleteLocalRef(activity);
        bool enabled = false;
        paced = paced && SwappyVk_isEnabled(vk_swap_chain, &enabled) && enabled;
    }
    pacing.vk_device_ = device;
    pacing.vk_swap_chain_ = swap_chain;
    pacing.vk_refresh_ns_ = refresh;
    if (paced)
    {
        SwappyVk_setWindow(vk_device, vk_swap_chain, static_cast<ANativeWindow *>(native_window));
        // The interval chosen for the swap chain before this one, until the game thread has chosen for this one.
        if (pacing.vk_interval_ns_)
        {
            SwappyVk_setSwapIntervalNS(vk_device, vk_swap_chain, pacing.vk_interval_ns_);
            SwappyVk_setMaxAutoSwapIntervalNS(pacing.vk_threshold_ns_);
        }
    }
    else
        pacing.unpaced(activity ? "SwappyVk_initAndGetRefreshCycleDuration failed or the device disables it, see the "
                                  "Swappy lines above"
                                : "no JNI environment or activity for the presenting thread");
    pacing.swappy_ = paced;
    pacing.chain_changed_ = true;
}

void FramePacing::vk_swap_chain_destroying(void *device, uint64_t swap_chain)
{
    FramePacing &pacing = instance();
    std::lock_guard chain(pacing.chain_mutex_);
    if (pacing.vk_swap_chain_ != swap_chain)
        return;
    if (pacing.swappy_)
    {
        SwappyVk_destroySwapchain(static_cast<VkDevice>(device), swap_chain_handle(swap_chain));
        SwappyVk_destroyDevice(static_cast<VkDevice>(device)); // the next swap chain registers the queue again
    }
    pacing.swappy_ = false;
    pacing.vk_device_ = nullptr;
    pacing.vk_swap_chain_ = 0;
}

int32_t FramePacing::vk_present(void *queue, const void *present_info)
{
    FramePacing &pacing = instance();
    const auto before = Clock::now();
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    if (pacing.swappy_)
    {
        STORM_PROFILE_ZONE("present: Swappy waits for the vsync slot");
        result = SwappyVk_queuePresent(static_cast<VkQueue>(queue), static_cast<const VkPresentInfoKHR *>(present_info));
    }
    else if (pacing.vk_queue_present_)
    {
        STORM_PROFILE_ZONE("present: vkQueuePresentKHR (no Swappy)");
        result = static_cast<VkResult>(pacing.vk_queue_present_(queue, present_info));
    }
    // Suboptimal, out of date and a lost surface are bgfx's to handle (it re-creates the swap chain).
    if (result < VK_SUCCESS && result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_ERROR_SURFACE_LOST_KHR)
    {
        static uint32_t failures = 0;
        if (failures++ % 600 == 0)
            logf("ERROR: %s failed (VkResult %d), %u failures so far",
                 pacing.swappy_ ? "SwappyVk_queuePresent" : "vkQueuePresentKHR", static_cast<int>(result), failures);
    }
    pacing.presented(before, Clock::now());
    return static_cast<int32_t>(result);
}

void FramePacing::game_frame()
{
    if (mode_check_period_ && Clock::now() >= mode_check_at_)
        check_display_mode();
    if (!chain_changed_.exchange(false))
        return;
    if (swappy_)
        choose_interval(); // logs the interval, sets the ADPF target
    else
    {
        preferred_ = target_;
        storm::platform::frameWorkTarget(target_);
    }
}

std::chrono::nanoseconds FramePacing::swappy_interval()
{
    if (!swappy_)
        return std::chrono::nanoseconds::zero();
    if (!vulkan_)
        return std::chrono::nanoseconds(SwappyGL_getSwapIntervalNS());
    return std::chrono::nanoseconds(SwappyVk_getSwapIntervalNS(swap_chain_handle(vk_swap_chain_)));
}

std::chrono::nanoseconds FramePacing::swappy_refresh()
{
    if (!swappy_)
        return refresh_period_;
    return std::chrono::nanoseconds(vulkan_ ? vk_refresh_ns_ : SwappyGL_getRefreshPeriodNanos());
}

void FramePacing::check_display_mode()
{
    const uint64_t chosen = std::exchange(mode_check_period_, 0);
    const uint64_t display = display_refresh_period();
    if (!display)
        return; // logged
    if (DisplayInMode(chosen, display))
    {
        logf("frame pacing: the display runs the chosen %.0f Hz mode", 1e9 / chosen);
        return;
    }
    char message[256];
    snprintf(message, sizeof(message),
             "frame pacing: the display stayed at %.2f Hz instead of the %.2f Hz mode chosen for %.0f fps; choosing "
             "again among whole refreshes of %.2f ms",
             1e9 / display, 1e9 / chosen, 1e9 / target_.count(), display / 1e6);
    log_error(message);
    choose_interval(display);
}

void FramePacing::choose_interval(uint64_t display_period)
{
    // Vulkan: the swap chain stays while this runs (the presenting thread re-creates it under the same mutex).
    std::unique_lock chain(chain_mutex_, std::defer_lock);
    if (vulkan_)
    {
        chain.lock();
        if (!swappy_ || !vk_swap_chain_)
            return; // gone meanwhile: the next one asks again (chain_changed_)
    }
    const VkSwapchainKHR vk_swap_chain = swap_chain_handle(vk_swap_chain_);
    const int count = vulkan_ ? SwappyVk_getSupportedRefreshPeriodsNS(nullptr, 0, vk_swap_chain)
                              : SwappyGL_getSupportedRefreshPeriodsNS(nullptr, 0);
    std::vector<uint64_t> periods(static_cast<size_t>(std::max(count, 0)));
    if (count > 0)
    {
        if (vulkan_)
            SwappyVk_getSupportedRefreshPeriodsNS(periods.data(), count, vk_swap_chain);
        else
            SwappyGL_getSupportedRefreshPeriodsNS(periods.data(), count);
    }
    const uint64_t current = display_period ? display_period : vulkan_ ? vk_refresh_ns_ : SwappyGL_getRefreshPeriodNanos();
    if (periods.empty())
        periods.push_back(current);
    // The display did not enter the mode chosen before: the mode it is in is the only one.
    const std::vector<uint64_t> listed = periods;
    if (display_period)
        periods.assign(1, display_period);
    std::string rates;
    for (const uint64_t period : listed)
    {
        if (period == 0)
            continue;
        char rate[48];
        snprintf(rate, sizeof(rate), "%s%.2f ms (%.0f Hz)", rates.empty() ? "" : ", ", period / 1e6, 1e9 / period);
        rates += rate;
    }
    const SwapChoice choice = ChooseSwapInterval(periods, current, static_cast<uint64_t>(target_.count()));
    if (choice.period == 0)
    {
        // Swappy knows no display period: keep its own default pacing.
        log_error("frame pacing: Swappy reports no display refresh period; it paces at its own default interval");
        return;
    }
    const uint64_t interval = choice.interval();
    const uint64_t slowest = SlowestSwapInterval(choice, static_cast<uint64_t>(kSlowestFrame.count()));
    const uint64_t threshold = SwapThreshold(choice, slowest);
    if (vulkan_)
    {
        SwappyVk_setSwapIntervalNS(static_cast<VkDevice>(vk_device_), vk_swap_chain, interval);
        SwappyVk_setMaxAutoSwapIntervalNS(threshold);
        vk_interval_ns_ = interval;
        vk_threshold_ns_ = threshold;
    }
    else
    {
        SwappyGL_setSwapIntervalNS(interval);
        SwappyGL_setMaxAutoSwapIntervalNS(threshold);
    }
    {
        std::lock_guard lock(mutex_);
        swap_interval_ = swappy_interval();
        refresh_period_ = std::chrono::nanoseconds(current);
        threshold_ = std::chrono::nanoseconds(threshold);
    }
    // Whether the display entered the chosen mode is seen about a second later (game_frame); a choice among the
    // display's own period is not checked again.
    mode_check_period_ = display_period ? 0 : choice.period;
    mode_check_at_ = Clock::now() + kModeCheckDelay;
    // Swappy Vk has no CPU affinity of its own to switch off.
    logf("frame pacing: Swappy %s %d.%d.%d (%s); frame rate %.0f fps (setting); display modes %s; current refresh "
         "%.2f ms (%.0f Hz)%s; swap interval %.2f ms (%.1f fps, %llu refreshes of the %.0f Hz mode), slower down to %.2f "
         "ms when frames do not fit (paced up to %.2f ms; slower than that Swappy stops pacing); CPU affinity off (ADPF)",
         vulkan_ ? "Vk" : "GL", SWAPPY_MAJOR_VERSION, SWAPPY_MINOR_VERSION, SWAPPY_BUGFIX_VERSION, STORM_SWAPPY_PACKAGE,
         1e9 / target_.count(), rates.c_str(), current / 1e6, current ? 1e9 / current : 0.0,
         display_period ? ", the only mode used (the display stayed in it)" : "", interval / 1e6, 1e9 / interval,
         static_cast<unsigned long long>(choice.refreshes), 1e9 / choice.period, slowest / 1e6, threshold / 1e6);
    preferred_ = std::chrono::nanoseconds(interval);
    // ADPF aims at the preferred rate (FramePacing in frame_pacing.hpp: not at a lowered one).
    storm::platform::frameWorkTarget(std::chrono::nanoseconds(interval));
}

void FramePacing::set_frame_rate(uint32_t fps)
{
    if (fps < 1 || fps > 1000)
    {
        char message[128];
        snprintf(message, sizeof(message), "frame pacing: frame rate %u fps is not valid; keeping %.0f fps", fps,
                 1e9 / target_.count());
        log_error(message);
        return;
    }
    const std::chrono::nanoseconds target(1'000'000'000LL / fps);
    if (target == target_)
        return;
    target_ = target;
    if (!started_)
        return; // start() paces at it
    if (swappy_)
    {
        choose_interval(); // logs the new interval, sets the ADPF target
        return;
    }
    preferred_ = target_;
    logf("frame pacing: frame rate %u fps (setting); without Swappy the present stays unpaced, ADPF aims at %.2f ms", fps,
         to_ms(target_));
    storm::platform::frameWorkTarget(target_);
}

void FramePacing::set_window(void *native_window)
{
    if (vulkan_)
    {
        // A swap chain gets its window when it is created (vk_swap_chain_created); only the loss is told here.
        std::lock_guard chain(chain_mutex_);
        if (swappy_ && vk_swap_chain_ && !native_window)
            SwappyVk_setWindow(static_cast<VkDevice>(vk_device_), swap_chain_handle(vk_swap_chain_), nullptr);
    }
    else if (swappy_)
        SwappyGL_setWindow(static_cast<ANativeWindow *>(native_window));
    // No interval spans the time without a window.
    std::lock_guard lock(mutex_);
    last_present_ = {};
}

void FramePacing::set_sampling(bool on)
{
    std::lock_guard lock(mutex_);
    sampling_ = on;
    samples_ms_.clear();
}

std::vector<float> FramePacing::take_intervals()
{
    std::lock_guard lock(mutex_);
    return std::exchange(samples_ms_, {});
}

void FramePacing::game_thread_waited(std::chrono::nanoseconds wait)
{
    storm::platform::presentWaited(wait);
}

void FramePacing::render_thread_started(int tid)
{
    storm::platform::frameWorkThread(tid);
}

std::chrono::nanoseconds FramePacing::preferred_interval() const
{
    return preferred_;
}

std::chrono::nanoseconds FramePacing::take_present_wait()
{
    return std::exchange(present_wait_, std::chrono::nanoseconds::zero());
}

void FramePacing::shutdown()
{
    if (!started_)
        return;
    if (vulkan_)
    {
        // bgfx::shutdown released the swap chain (vk_swap_chain_destroying) before this.
        bgfx::setVkPresentHooks(nullptr);
        if (vulkan_library_)
            dlclose(vulkan_library_);
        vulkan_library_ = nullptr;
        vk_queue_present_ = nullptr;
        vk_interval_ns_ = vk_threshold_ns_ = 0;
        mode_check_period_ = 0;
        chain_changed_ = false;
    }
    else
    {
        bgfx::setEglPresent(nullptr);
        if (swappy_)
            SwappyGL_destroy();
    }
    swappy_ = false;
    started_ = false;
}

bool FramePacing::present(void *display, void *surface)
{
    FramePacing &pacing = instance();
    const auto before = Clock::now();
    bool presented = false;
    if (pacing.swappy_)
    {
        STORM_PROFILE_ZONE("present: Swappy waits for the vsync slot");
        presented = SwappyGL_swap(static_cast<EGLDisplay>(display), static_cast<EGLSurface>(surface));
    }
    else
    {
        STORM_PROFILE_ZONE("present: eglSwapBuffers (no Swappy)");
        presented = eglSwapBuffers(static_cast<EGLDisplay>(display), static_cast<EGLSurface>(surface)) == EGL_TRUE;
    }
    if (!presented)
    {
        static uint32_t failures = 0;
        if (failures++ % 600 == 0)
            logf("ERROR: %s failed (EGL error 0x%x), %u failures so far", pacing.swappy_ ? "SwappyGL_swap" : "eglSwapBuffers",
                 eglGetError(), failures);
    }
    pacing.presented(before, Clock::now());
    return presented;
}

void FramePacing::presented(Clock::time_point before, Clock::time_point after)
{
    const auto wait = std::chrono::duration_cast<std::chrono::nanoseconds>(after - before);
    present_wait_ += wait;
    if (!render_thread_)
        storm::platform::presentWaited(wait); // the game thread waited here (host_hooks.hpp: game thread only)
    std::lock_guard lock(mutex_);
    STORM_PROFILE_PLOT("present wait ms", to_ms(wait));
    STORM_PROFILE_PLOT("Swappy swap interval ms", to_ms(swap_interval_));
    if (last_present_ != Clock::time_point{})
    {
        intervals_ms_.push_back(static_cast<float>(to_ms(after - last_present_)));
        wait_ms_ += to_ms(wait);
        if (sampling_ && samples_ms_.size() < kMaxSamples)
            samples_ms_.push_back(intervals_ms_.back());
    }
    last_present_ = after;

    const std::chrono::nanoseconds interval = swappy_interval(), refresh = swappy_refresh();
    if (swappy_ && (interval != swap_interval_ || refresh != refresh_period_))
    {
        if (SwapPaced(static_cast<uint64_t>(interval.count()), static_cast<uint64_t>(threshold_.count())))
            logf("frame pacing: Swappy presents every %.2f ms (%.1f fps) at a %.2f ms (%.0f Hz) display refresh "
                 "(was %.2f ms at %.2f ms)",
                 to_ms(interval), interval.count() ? 1e9 / interval.count() : 0.0, to_ms(refresh),
                 refresh.count() ? 1e9 / refresh.count() : 0.0, to_ms(swap_interval_), to_ms(refresh_period_));
        else
            logf("frame pacing: Swappy stopped pacing: frames do not fit the slowest paced interval (%.2f ms), its auto "
                 "interval stepped to %.2f ms past it; frames are presented unpaced, as they finish, at a %.2f ms (%.0f Hz) "
                 "display refresh (was %.2f ms)",
                 to_ms(threshold_), to_ms(interval), to_ms(refresh), refresh.count() ? 1e9 / refresh.count() : 0.0,
                 to_ms(swap_interval_));
        swap_interval_ = interval;
        refresh_period_ = refresh;
    }
    const auto elapsed = after - window_start_;
    if (elapsed >= kStatsWindow || intervals_ms_.size() >= 8192)
    {
        log_stats(std::chrono::duration<double>(elapsed).count());
        reset_stats();
    }
}

void FramePacing::log_stats(double seconds)
{
    if (intervals_ms_.empty())
        return;
    std::vector<float> sorted = intervals_ms_;
    std::sort(sorted.begin(), sorted.end());
    const size_t count = sorted.size();
    const auto percentile = [&](size_t percent) { return sorted[(count * percent + 99) / 100 - 1]; };
    // Frame intervals in display refreshes (1 ... 5, 6 = six or more); without a known refresh in 60 Hz ones.
    const double refresh = refresh_period_.count() ? to_ms(refresh_period_) : to_ms(kDefaultFrame);
    uint32_t buckets[6] = {};
    for (const float ms : intervals_ms_)
        ++buckets[std::clamp<long>(std::lround(ms / refresh), 1, 6) - 1];
    const auto share = [&](uint64_t value, uint64_t total) { return total ? 100.0 * value / total : 0.0; };
    char pacing[96];
    if (swappy_ && SwapPaced(static_cast<uint64_t>(swap_interval_.count()), static_cast<uint64_t>(threshold_.count())))
        snprintf(pacing, sizeof(pacing), "Swappy %s at %.2f ms intervals", vulkan_ ? "Vk" : "GL", to_ms(swap_interval_));
    else if (swappy_)
        snprintf(pacing, sizeof(pacing), "Swappy %s unpaced (frames slower than %.2f ms)", vulkan_ ? "Vk" : "GL",
                 to_ms(threshold_));
    else
        snprintf(pacing, sizeof(pacing), "%s, no Swappy", vulkan_ ? "vkQueuePresentKHR" : "eglSwapBuffers");
    logf("frame pacing: %.1f s, %zu frames (%.1f fps), %s, %.2f ms refresh%s; frame ms p50 %.2f p95 %.2f p99 %.2f max "
         "%.2f; refreshes per frame 1:%.1f%% 2:%.1f%% 3:%.1f%% 4:%.1f%% 5:%.1f%% 6+:%.1f%%; present wait mean %.2f ms",
         seconds, count, count / seconds, pacing, refresh, refresh_period_.count() ? "" : " (assumed)", percentile(50),
         percentile(95), percentile(99),
         sorted.back(), share(buckets[0], count), share(buckets[1], count), share(buckets[2], count),
         share(buckets[3], count), share(buckets[4], count), share(buckets[5], count), wait_ms_ / count);
}

void FramePacing::reset_stats()
{
    intervals_ms_.clear();
    wait_ms_ = 0.0;
    window_start_ = Clock::now();
}

#else // desktop: bgfx swaps with eglSwapBuffers and the vsync flag of the device

void FramePacing::start(bool, bool)
{
}

void FramePacing::game_frame()
{
}

void FramePacing::game_thread_waited(std::chrono::nanoseconds)
{
}

void FramePacing::render_thread_started(int)
{
}

std::chrono::nanoseconds FramePacing::preferred_interval() const
{
    return std::chrono::nanoseconds::zero();
}

std::chrono::nanoseconds FramePacing::take_present_wait()
{
    return std::chrono::nanoseconds::zero();
}

void FramePacing::set_window(void *)
{
}

void FramePacing::shutdown()
{
}

void FramePacing::set_frame_rate(uint32_t)
{
}

void FramePacing::set_sampling(bool)
{
}

std::vector<float> FramePacing::take_intervals()
{
    return {};
}

#endif

} // namespace storm::shim

namespace storm::render
{
void SetFrameRate(uint32_t fps)
{
    shim::FramePacing::instance().set_frame_rate(fps);
}

void SamplePresentIntervals(bool on)
{
    shim::FramePacing::instance().set_sampling(on);
}

std::vector<float> TakePresentIntervals()
{
    return shim::FramePacing::instance().take_intervals();
}
} // namespace storm::render
