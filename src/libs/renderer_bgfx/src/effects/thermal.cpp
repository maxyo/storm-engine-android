#include "effects/thermal.hpp"

#include "effects/thermal_api.hpp"
#include "shim/shim.hpp"

#include <atomic>
#include <chrono>
#include <cmath>

#ifdef __ANDROID__
#include <android/api-level.h>
#include <dlfcn.h>
#endif

namespace storm::effects
{

namespace
{
#ifdef __ANDROID__
const char *status_name(int status)
{
    static const char *const names[] = {"none", "light", "moderate", "severe", "critical", "emergency", "shutdown"};
    return status >= 0 && status < 7 ? names[status] : "unknown";
}

struct Thermal
{
    ThermalApi api;
    void *manager = nullptr;
    Thermal()
    {
        void *library = dlopen("libandroid.so", RTLD_NOW);
        const int level = android_get_device_api_level();
        const auto resolution = resolve_thermal_api(level, [library](const char *name) {
            return library ? dlsym(library, name) : nullptr;
        });
        if (!resolution.error.empty() || (level >= kThermalStatusApi && !library))
        {
            shim::logf("effects: ERROR thermal status unavailable: %s", resolution.error.empty() ? "libandroid.so did not load"
                                                                                                  : resolution.error.c_str());
            return;
        }
        if (!resolution.available)
        {
            shim::logf("effects: no thermal status on Android API %d (AThermal needs API 30): the relief generation and "
                       "the ray bakes do not pause when the device is hot", level);
            return;
        }
        api = resolution.api;
        manager = api.acquire();
        if (!manager)
            shim::logf("effects: ERROR AThermal_acquireManager returned null on Android API %d: no thermal pause", level);
        else
            shim::logf("effects: thermal status is read (API %d%s)", level, api.headroom ? ", with headroom" : "");
    }
};
#endif
} // namespace

bool device_hot()
{
#ifdef __ANDROID__
    static Thermal thermal; // thread-safe initialisation
    static std::atomic<int64_t> asked{0};
    static std::atomic<bool> hot{false};
    static std::atomic<int> last_status{-1};
    const int64_t now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    int64_t last = asked.load();
    if (thermal.manager && now - last >= 1000 && asked.compare_exchange_strong(last, now))
    {
        const int status = thermal.api.status(thermal.manager);
        const float headroom = thermal.api.headroom ? thermal.api.headroom(thermal.manager, kHeadroomForecastSeconds)
                                                    : std::nanf("");
        if (status != last_status.exchange(status))
            shim::logf("effects: thermal status: %s, headroom %.2f", status_name(status), static_cast<double>(headroom));
        hot = thermal_hot(hot.load(), status, headroom);
    }
    return hot.load();
#else
    return false;
#endif
}

namespace
{
std::atomic<const char *> g_hold{nullptr};
} // namespace

void hold_background_work(const char *why)
{
    const char *was = g_hold.exchange(why);
    if (why && !was)
        shim::logf("effects: background work (relief generation, ray bakes) held: %s", why);
    else if (!why && was)
        shim::logf("effects: background work (relief generation, ray bakes) released (was held: %s)", was);
}

const char *background_wait_reason()
{
    if (const char *held = g_hold.load())
        return held;
    return device_hot() ? "the device is hot (thermal status MODERATE or higher)" : nullptr;
}

} // namespace storm::effects
