#include "effects/thermal.hpp"

#include "shim/shim.hpp"

#include <atomic>
#include <chrono>

#ifdef __ANDROID__
#include <dlfcn.h>
#endif

namespace storm::effects
{

namespace
{
#ifdef __ANDROID__
struct Thermal
{
    using Acquire = void *(*)();
    using Status = int (*)(void *);
    void *manager = nullptr;
    Status status = nullptr;
    Thermal()
    {
        void *library = dlopen("libandroid.so", RTLD_NOW);
        const auto acquire = library ? reinterpret_cast<Acquire>(dlsym(library, "AThermal_acquireManager")) : nullptr;
        status = library ? reinterpret_cast<Status>(dlsym(library, "AThermal_getCurrentStatus")) : nullptr;
        manager = acquire && status ? acquire() : nullptr;
        if (!manager)
            shim::logf("effects: no thermal status on this Android (AThermal needs Android 11): the relief generation "
                       "and the ray bakes do not pause when the device is hot");
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
    constexpr int kModerate = 2; // ATHERMAL_STATUS_MODERATE
    const int64_t now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    int64_t last = asked.load();
    if (now - last >= 1000 && asked.compare_exchange_strong(last, now))
        hot = thermal.manager && thermal.status(thermal.manager) >= kModerate;
    return hot.load();
#else
    return false;
#endif
}

} // namespace storm::effects
