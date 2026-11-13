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

void FramePacing::start(bool render_thread)
{
    if (started_)
        return;
    started_ = true;
    render_thread_ = render_thread;
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
        SDL_DisplayMode mode{};
        if (SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.refresh_rate > 0)
            refresh_period_ = std::chrono::nanoseconds(1'000'000'000LL / mode.refresh_rate);
        char message[512];
        snprintf(message, sizeof(message),
                 "frame pacing: Swappy GL unavailable (%s); presenting with eglSwapBuffers at swap interval 1, the "
                 "display's own rate (%s), unpaced; ADPF aims at %.2f ms; frame times are still logged",
                 activity ? "SwappyGL_init failed or the device disables it, see the Swappy lines above"
                          : "no JNI environment or activity for the game thread",
                 refresh_period_.count() ? (std::to_string(1'000'000'000LL / refresh_period_.count()) + " Hz").c_str()
                                         : "refresh unknown to SDL",
                 to_ms(target_));
        log_error(message);
        preferred_ = target_;
        storm::platform::frameWorkTarget(target_);
    }
    // Both ways the present goes through present_frame: the same frame times and present wait.
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
    std::string rates;
    for (const uint64_t period : periods)
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
    SwappyGL_setSwapIntervalNS(interval);
    SwappyGL_setMaxAutoSwapIntervalNS(slowest);
    {
        std::lock_guard lock(mutex_);
        swap_interval_ = std::chrono::nanoseconds(SwappyGL_getSwapIntervalNS());
        refresh_period_ = std::chrono::nanoseconds(current);
    }
    logf("frame pacing: Swappy GL %d.%d.%d (%s); frame rate %.0f fps (setting); display modes %s; current refresh "
         "%.2f ms (%.0f Hz); swap interval %.2f ms (%.1f fps, %llu refreshes of the %.0f Hz mode), slower down to %.2f "
         "ms when frames do not fit; CPU affinity off (ADPF)",
         SWAPPY_MAJOR_VERSION, SWAPPY_MINOR_VERSION, SWAPPY_BUGFIX_VERSION, STORM_SWAPPY_PACKAGE, 1e9 / target_.count(),
         rates.c_str(), current / 1e6, current ? 1e9 / current : 0.0, interval / 1e6, 1e9 / interval,
         static_cast<unsigned long long>(choice.refreshes), 1e9 / choice.period, slowest / 1e6);
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
    if (swappy_)
        SwappyGL_setWindow(static_cast<ANativeWindow *>(native_window));
    // No interval spans the time without a window.
    std::lock_guard lock(mutex_);
    last_present_ = {};
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
    bgfx::setEglPresent(nullptr);
    if (swappy_)
        SwappyGL_destroy();
    swappy_ = false;
    started_ = false;
}

bool FramePacing::present(void *display, void *surface)
{
    return instance().present_frame(display, surface);
}

bool FramePacing::present_frame(void *display, void *surface)
{
    const auto before = Clock::now();
    bool presented = false;
    if (swappy_)
    {
        STORM_PROFILE_ZONE("present: Swappy waits for the vsync slot");
        presented = SwappyGL_swap(static_cast<EGLDisplay>(display), static_cast<EGLSurface>(surface));
    }
    else
    {
        STORM_PROFILE_ZONE("present: eglSwapBuffers (no Swappy)");
        presented = eglSwapBuffers(static_cast<EGLDisplay>(display), static_cast<EGLSurface>(surface)) == EGL_TRUE;
    }
    const auto after = Clock::now();
    if (!presented)
    {
        static uint32_t failures = 0;
        if (failures++ % 600 == 0)
            logf("ERROR: %s failed (EGL error 0x%x), %u failures so far", swappy_ ? "SwappyGL_swap" : "eglSwapBuffers",
                 eglGetError(), failures);
    }
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
    }
    last_present_ = after;

    const std::chrono::nanoseconds interval(swappy_ ? SwappyGL_getSwapIntervalNS() : 0),
        refresh(swappy_ ? SwappyGL_getRefreshPeriodNanos() : refresh_period_.count());
    if (swappy_ && (interval != swap_interval_ || refresh != refresh_period_))
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
    // Frame intervals in display refreshes (1 ... 5, 6 = six or more); without a known refresh in 60 Hz ones.
    const double refresh = refresh_period_.count() ? to_ms(refresh_period_) : to_ms(kDefaultFrame);
    uint32_t buckets[6] = {};
    for (const float ms : intervals_ms_)
        ++buckets[std::clamp<long>(std::lround(ms / refresh), 1, 6) - 1];
    const auto share = [&](uint64_t value, uint64_t total) { return total ? 100.0 * value / total : 0.0; };
    char pacing[64];
    if (swappy_)
        snprintf(pacing, sizeof(pacing), "Swappy at %.2f ms intervals", to_ms(swap_interval_));
    else
        snprintf(pacing, sizeof(pacing), "eglSwapBuffers, no Swappy");
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

void FramePacing::start(bool)
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

#endif

} // namespace storm::shim

namespace storm::render
{
void SetFrameRate(uint32_t fps)
{
    shim::FramePacing::instance().set_frame_rate(fps);
}
} // namespace storm::render
