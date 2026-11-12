#include "frame_pacing.hpp"

#include "shim.hpp"

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
#include <jni.h>
#include <swappy/swappyGL.h>
#include <swappy/swappyGL_extra.h>

// patches/bgfx-swap-present-hook.patch (src/glcontext_egl.cpp) defines these two.
namespace bgfx
{
typedef bool (*EglPresentFn)(void *_display, void *_surface);
void setEglPresent(EglPresentFn _present);
} // namespace bgfx
#endif

namespace storm::shim
{

#ifdef __ANDROID__
namespace
{
using Clock = std::chrono::steady_clock;
constexpr auto kStatsWindow = std::chrono::seconds(30);

double to_ms(std::chrono::nanoseconds value)
{
    return std::chrono::duration<double, std::milli>(value).count();
}

void log_error(const char *message)
{
    __android_log_print(ANDROID_LOG_ERROR, "storm-bgfx", "%s", message);
    fprintf(stderr, "[storm-bgfx] ERROR: %s\n", message);
}
} // namespace
#endif

FramePacing &FramePacing::instance()
{
    static FramePacing pacing;
    return pacing;
}

#ifdef __ANDROID__

void FramePacing::start()
{
    if (swappy_)
        return;
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = env ? static_cast<jobject>(SDL_AndroidGetActivity()) : nullptr;
    const bool initialised = activity && SwappyGL_init(env, activity);
    if (activity)
        env->DeleteLocalRef(activity);
    if (!initialised || !SwappyGL_isEnabled())
    {
        log_error(!activity ? "frame pacing: Swappy GL unavailable (no JNI environment or activity for the game thread); "
                              "presenting with eglSwapBuffers at swap interval 1: the display's rate, unpaced"
                            : "frame pacing: Swappy GL unavailable (SwappyGL_init failed or the device disables it, see the "
                              "Swappy lines above); presenting with eglSwapBuffers at swap interval 1: the display's rate, "
                              "unpaced");
        if (activity)
            SwappyGL_destroy(); // whatever SwappyGL_init left behind
        return;
    }
    swappy_ = true;
    // The scheduler and the ADPF hint session place the game thread; Swappy's pinning would fight them.
    SwappyGL_setUseAffinity(false);
    SwappyGL_setAutoSwapInterval(true);
    SwappyGL_setAutoPipelineMode(true);
    choose_interval();
    bgfx::setEglPresent(&FramePacing::present);
    reset_stats();
}

void FramePacing::choose_interval()
{
    const int count = SwappyGL_getSupportedRefreshPeriodsNS(nullptr, 0);
    std::vector<uint64_t> periods(static_cast<size_t>(std::max(count, 0)));
    if (count > 0)
        SwappyGL_getSupportedRefreshPeriodsNS(periods.data(), count);
    const uint64_t current = SwappyGL_getRefreshPeriodNanos();
    if (periods.empty())
        periods.push_back(current);
    // A whole number of refreshes of one of the display's modes closest to the target; on a tie the current
    // mode (no switch), then the slower mode.
    const auto target = static_cast<double>(kTargetFrame.count());
    uint64_t best_period = 0, best_count = 0;
    double best_error = 0.0;
    std::string rates;
    for (const uint64_t period : periods)
    {
        if (period == 0)
            continue;
        const uint64_t refreshes = std::max<uint64_t>(1, std::llround(target / period));
        const double error = std::abs(static_cast<double>(refreshes * period) - target);
        const bool better = best_period == 0 || error < best_error - 1e5 ||
                            (error < best_error + 1e5 && (period == current || (best_period != current && period > best_period)));
        if (better)
        {
            best_period = period;
            best_count = refreshes;
            best_error = error;
        }
        char rate[48];
        snprintf(rate, sizeof(rate), "%s%.2f ms (%.0f Hz)", rates.empty() ? "" : ", ", period / 1e6, 1e9 / period);
        rates += rate;
    }
    if (best_period == 0)
    {
        // Swappy knows no display period: keep its own default pacing.
        log_error("frame pacing: Swappy reports no display refresh period; it paces at its own default interval");
        return;
    }
    const uint64_t interval = best_period * best_count;
    const uint64_t slowest =
        best_period * std::max<uint64_t>(best_count, std::llround(static_cast<double>(kSlowestFrame.count()) / best_period));
    SwappyGL_setSwapIntervalNS(interval);
    SwappyGL_setMaxAutoSwapIntervalNS(slowest);
    swap_interval_ = std::chrono::nanoseconds(SwappyGL_getSwapIntervalNS());
    refresh_period_ = std::chrono::nanoseconds(current);
    logf("frame pacing: Swappy GL %d.%d.%d (%s); display modes %s; current refresh %.2f ms (%.0f Hz); "
         "swap interval %.2f ms (%.1f fps, %llu refreshes of the %.0f Hz mode), slower down to %.2f ms when frames do "
         "not fit; CPU affinity off (ADPF)",
         SWAPPY_MAJOR_VERSION, SWAPPY_MINOR_VERSION, SWAPPY_BUGFIX_VERSION, STORM_SWAPPY_PACKAGE, rates.c_str(),
         current / 1e6, current ? 1e9 / current : 0.0, interval / 1e6, 1e9 / interval,
         static_cast<unsigned long long>(best_count), 1e9 / best_period, slowest / 1e6);
    // ADPF aims at the preferred rate (FramePacing in frame_pacing.hpp: not at a lowered one).
    storm::platform::frameWorkTarget(std::chrono::nanoseconds(interval));
}

void FramePacing::set_window(void *native_window)
{
    if (!swappy_)
        return;
    SwappyGL_setWindow(static_cast<ANativeWindow *>(native_window));
    // No interval spans the time without a window.
    last_present_ = {};
}

void FramePacing::shutdown()
{
    if (!swappy_)
        return;
    bgfx::setEglPresent(nullptr);
    SwappyGL_destroy();
    swappy_ = false;
}

bool FramePacing::present(void *display, void *surface)
{
    return instance().present_frame(display, surface);
}

bool FramePacing::present_frame(void *display, void *surface)
{
    const auto before = Clock::now();
    bool presented = false;
    {
        STORM_PROFILE_ZONE("present: Swappy waits for the vsync slot");
        presented = SwappyGL_swap(static_cast<EGLDisplay>(display), static_cast<EGLSurface>(surface));
    }
    const auto after = Clock::now();
    if (!presented)
    {
        static uint32_t failures = 0;
        if (failures++ % 600 == 0)
            logf("ERROR: SwappyGL_swap failed (EGL error 0x%x), %u failures so far", eglGetError(), failures);
    }
    const auto wait = std::chrono::duration_cast<std::chrono::nanoseconds>(after - before);
    storm::platform::presentWaited(wait);
    STORM_PROFILE_PLOT("present wait ms", to_ms(wait));
    STORM_PROFILE_PLOT("Swappy swap interval ms", to_ms(swap_interval_));
    if (last_present_ != Clock::time_point{})
    {
        intervals_ms_.push_back(static_cast<float>(to_ms(after - last_present_)));
        wait_ms_ += to_ms(wait);
    }
    last_present_ = after;

    const std::chrono::nanoseconds interval(SwappyGL_getSwapIntervalNS()), refresh(SwappyGL_getRefreshPeriodNanos());
    if (interval != swap_interval_ || refresh != refresh_period_)
    {
        logf("frame pacing: Swappy presents every %.2f ms (%.1f fps) at a %.2f ms (%.0f Hz) display refresh "
             "(was %.2f ms at %.2f ms)",
             to_ms(interval), interval.count() ? 1e9 / interval.count() : 0.0, to_ms(refresh),
             refresh.count() ? 1e9 / refresh.count() : 0.0, to_ms(swap_interval_), to_ms(refresh_period_));
        swap_interval_ = interval;
        refresh_period_ = refresh;
    }
    const auto elapsed = after - window_start_;
    if (elapsed >= kStatsWindow || intervals_ms_.size() >= 8192)
    {
        log_stats(std::chrono::duration<double>(elapsed).count());
        reset_stats();
    }
    return presented;
}

void FramePacing::log_stats(double seconds)
{
    if (intervals_ms_.empty())
        return;
    std::vector<float> sorted = intervals_ms_;
    std::sort(sorted.begin(), sorted.end());
    const size_t count = sorted.size();
    const auto percentile = [&](size_t percent) { return sorted[(count * percent + 99) / 100 - 1]; };
    // Frame intervals in display refreshes (1 ... 5, 6 = six or more).
    const double refresh = refresh_period_.count() ? to_ms(refresh_period_) : to_ms(kTargetFrame);
    uint32_t buckets[6] = {};
    for (const float ms : intervals_ms_)
        ++buckets[std::clamp<long>(std::lround(ms / refresh), 1, 6) - 1];
    const auto share = [&](uint64_t value, uint64_t total) { return total ? 100.0 * value / total : 0.0; };
    logf("frame pacing: %.1f s, %zu frames (%.1f fps) at %.2f ms intervals (Swappy), %.2f ms refresh; frame ms p50 %.2f "
         "p95 %.2f p99 %.2f max %.2f; refreshes per frame 1:%.1f%% 2:%.1f%% 3:%.1f%% 4:%.1f%% 5:%.1f%% 6+:%.1f%%; "
         "present wait mean %.2f ms",
         seconds, count, count / seconds, to_ms(swap_interval_), refresh, percentile(50), percentile(95), percentile(99),
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

void FramePacing::start()
{
}

void FramePacing::set_window(void *)
{
}

void FramePacing::shutdown()
{
}

#endif

} // namespace storm::shim
