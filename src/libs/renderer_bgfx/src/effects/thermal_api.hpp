// The AThermal NDK API's availability and the pause decision, free of Android so a host check can run them: the
// symbol lookup is injected (thermal.cpp passes dlsym on libandroid.so). A symbol missing on an API that has it is an
// error to log loudly, never a quiet "no thermal status" (a wrong symbol name once did that on every phone).
#pragma once

#include <cmath>
#include <string>

namespace storm::effects
{

struct ThermalApi
{
    void *(*acquire)() = nullptr;                 // AThermal_acquireManager, API 30
    int (*status)(void *) = nullptr;              // AThermal_getCurrentThermalStatus, API 30
    float (*headroom)(void *, int) = nullptr;     // AThermal_getThermalHeadroom, API 31
};

struct ThermalResolution
{
    ThermalApi api;
    bool available = false; // the status can be read
    std::string error;      // not empty: the API level has the functions but a symbol did not resolve
};

constexpr int kThermalStatusApi = 30;
constexpr int kThermalHeadroomApi = 31;

// lookup(name) returns the symbol's address or null.
template <class Lookup> ThermalResolution resolve_thermal_api(int api_level, Lookup lookup)
{
    ThermalResolution result;
    if (api_level < kThermalStatusApi)
        return result;
    result.api.acquire = reinterpret_cast<void *(*)()>(lookup("AThermal_acquireManager"));
    result.api.status = reinterpret_cast<int (*)(void *)>(lookup("AThermal_getCurrentThermalStatus"));
    if (!result.api.acquire)
        result.error += " AThermal_acquireManager";
    if (!result.api.status)
        result.error += " AThermal_getCurrentThermalStatus";
    if (api_level >= kThermalHeadroomApi)
    {
        result.api.headroom = reinterpret_cast<float (*)(void *, int)>(lookup("AThermal_getThermalHeadroom"));
        if (!result.api.headroom)
            result.error += " AThermal_getThermalHeadroom";
    }
    if (!result.error.empty())
        result.error = "Android API " + std::to_string(api_level) + " has AThermal but these symbols did not resolve:" +
                       result.error;
    result.available = result.api.acquire && result.api.status;
    return result;
}

// ATHERMAL_STATUS_MODERATE: the first level where the platform asks to cut load.
constexpr int kThermalModerate = 2;
// Headroom (1.0 = severe throttling) forecast kHeadroomForecastSeconds ahead: pause early from kHeadroomPause, resume
// below kHeadroomResume (and a status under MODERATE), so it does not flap.
constexpr int kHeadroomForecastSeconds = 10;
constexpr float kHeadroomPause = 0.9f;
constexpr float kHeadroomResume = 0.75f;

// headroom is NaN when the platform cannot forecast (then the status alone decides).
inline bool thermal_hot(bool was_hot, int status, float headroom)
{
    if (status >= kThermalModerate)
        return true;
    if (std::isnan(headroom))
        return false;
    return was_hot ? headroom >= kHeadroomResume : headroom >= kHeadroomPause;
}

} // namespace storm::effects
