#pragma once
#ifdef __ANDROID__
#include <android/log.h>
#include <sys/system_properties.h>
#include <array>
#include <string>
#include <string_view>

namespace storm::android
{
// Read-only, game-thread diagnostics. Six independent transition channels and
// a process-wide budget keep steady frames and broken adapters quiet.
inline void traceLandAdapter(unsigned channel, const std::string &state)
{
    static std::array<std::string, 6> previous;
    static unsigned remaining = 128;
    if (channel >= previous.size() || !remaining || previous[channel] == state) return;
    char setting[PROP_VALUE_MAX] = {};
    __system_property_get("log.tag.StormInput", setting);
    if (std::string_view(setting) != "DEBUG") return;
    previous[channel] = state; --remaining;
    constexpr const char *names[] = {"bootstrap", "readiness", "source", "helper", "owner", "selection"};
    __android_log_print(ANDROID_LOG_DEBUG, "StormInput", "land-adapter %s %s", names[channel], state.c_str());
}
}
#endif
