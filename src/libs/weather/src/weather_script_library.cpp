// WEATHER_LIBRARY: natives Caribbean Legend 2.0 declares in Program/weather/WhrInternal.c and loads
// with #libriary "WEATHER_LIBRARY". They read the current weather entity: the fog density and
// the fog colour (ARGB, as the renderer's fog colour), e.g. for the background colour of
// underwater locations. Without a weather entity they log an error and return 0.

#include "weather_base.h"

#include "core.h"
#include "script_libriary.h"
#include "script_stub.hpp"
#include "v_s_stack.h"
#include "vma.hpp"

#include <spdlog/spdlog.h>

namespace
{
WEATHER_BASE *currentWeather(const char *native)
{
    const auto id = core.GetEntityId("weather");
    auto *weather = id ? static_cast<WEATHER_BASE *>(core.GetEntityPointer(id)) : nullptr;
    if (!weather)
        spdlog::error("{}: no weather entity exists; returns 0", native);
    return weather;
}

uint32_t getFogDensity(VS_STACK *s)
{
    auto *weather = currentWeather("GetFogDensity");
    auto *v = static_cast<VDATA *>(s->Push());
    if (!v)
        return IFUNCRESULT_FAILED;
    v->Set(weather ? weather->GetFloat(whf_fog_density) : 0.0f);
    return IFUNCRESULT_OK;
}

uint32_t getFogColor(VS_STACK *s)
{
    auto *weather = currentWeather("GetFogColor");
    auto *v = static_cast<VDATA *>(s->Push());
    if (!v)
        return IFUNCRESULT_FAILED;
    v->Set(static_cast<int32_t>(weather ? weather->GetColor(whc_fog_color) : 0));
    return IFUNCRESULT_OK;
}

class WEATHER_LIBRARY : public SCRIPT_LIBRIARY
{
  public:
    bool Init() override
    {
        storm::registerScriptFunction("GetFogDensity", "float", 0, getFogDensity);
        storm::registerScriptFunction("GetFogColor", "int", 0, getFogColor);
        return true;
    }
};
} // namespace

CREATE_SCRIPTLIBRIARY(WEATHER_LIBRARY)
