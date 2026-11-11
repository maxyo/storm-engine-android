#include "steam_api_script_lib.hpp"

#include "core.h"
#include "s_import_func.h"
#include "v_s_stack.h"
#include "vma.hpp"

// The game scripts call the Steam functions of the original PC release (achievements, stats, DLC
// checks). There is no Steam on our platforms: the functions keep their script signatures and
// report "no Steam", every DLC owned (the DLC content ships with the game), no achievements/stats.
namespace steamapi
{
CREATE_SCRIPTLIBRIARY(SteamApiScriptLib)

namespace
{
// Pops `arguments` script arguments and pushes `result`.
template <int32_t result, int arguments> uint32_t answer(VS_STACK *pS)
{
    for (int i = 0; i < arguments; ++i)
    {
        if (!pS->Pop())
        {
            return IFUNCRESULT_FAILED;
        }
    }
    auto *pV = pS->Push();
    if (!pV)
    {
        return IFUNCRESULT_FAILED;
    }
    pV->Set(result);
    return IFUNCRESULT_OK;
}

struct ScriptFunction
{
    const char *name;
    int32_t arguments;
    uint32_t (*function)(VS_STACK *);
};

constexpr ScriptFunction kFunctions[] = {
    {"GetSteamEnabled", 0, answer<0, 0>},  {"GetDLCenabled", 1, answer<1, 1>},
    {"GetDLCCount", 0, answer<0, 0>},      {"GetDLCData", 1, answer<0, 1>},
    {"DLCStartOverlay", 1, answer<0, 1>},  {"SetAchievement", 1, answer<0, 1>},
    {"GetAchievement", 1, answer<0, 1>},   {"SetStat", 2, answer<0, 2>},
    {"GetStat", 1, answer<0, 1>},          {"StoreStats", 0, answer<0, 0>},
    {"ClearAchievement", 1, answer<0, 1>}, {"ResetStats", 1, answer<0, 1>},
};
} // namespace

bool SteamApiScriptLib::Init()
{
    for (const auto &function : kFunctions)
    {
        IFUNCINFO sIFuncInfo;
        sIFuncInfo.nArguments = function.arguments;
        sIFuncInfo.pFuncName = function.name;
        sIFuncInfo.pReturnValueName = "int";
        sIFuncInfo.pFuncAddress = function.function;
        core.SetScriptFunction(&sIFuncInfo);
    }
    return true;
}
} // namespace steamapi
