// Platform-service natives Caribbean Legend 2.0 declares in Program/seadogs.c (Steam identity,
// overlay, rich presence, DLC and curator checks, anti-piracy) and in Program/RichPresence.c
// (DISCORD_SCRIPT_LIBRARY). The Android port has no Steam client and no Discord client: every
// native here is an explicit stub that logs once (storm::reportScriptStub) and returns the value
// CL's scripts treat as "service not available".
// Registered for every game: a native the scripts do not declare is unused (the compiler logs it).

#include "cl_platform_natives.hpp"

#include "core.h"
#include "script_stub.hpp"
#include "v_s_stack.h"
#include "vma.hpp"

#include <ctime>
#include <string>

namespace steamapi
{
CREATE_SCRIPTLIBRIARY(DISCORD_SCRIPT_LIBRARY)

namespace
{
void pop(VS_STACK *stack, int count)
{
    for (int i = 0; i < count; ++i)
        stack->Pop();
}

uint32_t pushInt(VS_STACK *stack, int32_t value)
{
    VDATA *v = stack->Push();
    if (!v)
        return IFUNCRESULT_FAILED;
    v->Set(value);
    return IFUNCRESULT_OK;
}

uint32_t pushString(VS_STACK *stack, const char *value)
{
    VDATA *v = stack->Push();
    if (!v)
        return IFUNCRESULT_FAILED;
    v->Set(value);
    return IFUNCRESULT_OK;
}

// One stub per native: `arguments` popped, a fixed result pushed.
template <int Arguments, int32_t Result> uint32_t intStub(VS_STACK *stack)
{
    pop(stack, Arguments);
    return pushInt(stack, Result);
}

uint32_t isVerified(VS_STACK *stack)
{
    // TODO real ownership check (Steam login) before any public release.
    storm::reportScriptStub("IsVerified", "internal demo build: ownership is not checked, reports verified (TODO real "
                                          "ownership check (Steam login) before any public release)");
    return intStub<0, 1>(stack);
}

uint32_t verify(VS_STACK *stack)
{
    // TODO real ownership check (Steam login) before any public release.
    storm::reportScriptStub("Verify", "internal demo build: ownership is not checked, reports verified (TODO real "
                                      "ownership check (Steam login) before any public release)");
    return intStub<0, 1>(stack);
}

uint32_t getPersonaName(VS_STACK *stack)
{
    // CL uses the Steam name as an in-game captain name in two quests.
    storm::reportScriptStub("GetPersonaName", "no Steam account: returns \"Captain\"");
    return pushString(stack, "Captain");
}

uint32_t getSteamId(VS_STACK *stack)
{
    storm::reportScriptStub("GetSteamID", "no Steam account: returns 0");
    return intStub<0, 0>(stack);
}

uint32_t getAppId(VS_STACK *stack)
{
    storm::reportScriptStub("GetAppID", "no Steam client: returns 0");
    return intStub<0, 0>(stack);
}

uint32_t isSteamDeck(VS_STACK *stack)
{
    storm::reportScriptStub("IsSteamDeck", "not a Steam Deck: returns false");
    return intStub<0, 0>(stack);
}

uint32_t isVirtualKeyboardShowing(VS_STACK *stack)
{
    storm::reportScriptStub("IsVirtualKeyboardShowing", "no Steam overlay: returns false");
    return intStub<0, 0>(stack);
}

uint32_t showSteamVirtualKeyboard(VS_STACK *stack)
{
    storm::reportScriptStub("ShowSteamVirtualKeyboard", "no Steam overlay: returns false");
    return intStub<5, 0>(stack);
}

uint32_t getOverlaysInfo(VS_STACK *stack)
{
    // CL lists its installed Workshop mods ("overlays") here; none are installed.
    storm::reportScriptStub("GetOverlaysInfo", "no Steam Workshop: reports 0 overlays, the output is untouched");
    return intStub<1, 0>(stack);
}

uint32_t gameOverlayToWebPage(VS_STACK *stack)
{
    storm::reportScriptStub("GameOverlayToWebPage", "no Steam overlay: the page is not opened, returns false");
    return intStub<1, 0>(stack);
}

uint32_t gameOverlayToWebPageModal(VS_STACK *stack)
{
    storm::reportScriptStub("GameOverlayToWebPageModal", "no Steam overlay: the page is not opened, returns false");
    return intStub<1, 0>(stack);
}

uint32_t steamSetRichPresence(VS_STACK *stack)
{
    storm::reportScriptStub("SteamSetRichPresence", "no Steam client: ignored, returns false");
    return intStub<2, 0>(stack);
}

uint32_t steamClearRichPresence(VS_STACK *stack)
{
    storm::reportScriptStub("SteamClearRichPresence", "no Steam client: ignored, returns false");
    return intStub<0, 0>(stack);
}

uint32_t bIsSubscribedApp(VS_STACK *stack)
{
    // CL checks ownership of a DLC (a franchise reward) with it.
    storm::reportScriptStub("BIsSubscribedApp", "no Steam client: DLC ownership is unknown, returns false");
    return intStub<1, 0>(stack);
}

uint32_t beginCuratorCheckAsync(VS_STACK *stack)
{
    // CL's main menu uses -1 as the status when Steam is not available.
    storm::reportScriptStub("BeginCuratorCheckAsync", "no Steam client: returns -1 (not available)");
    return intStub<2, -1>(stack);
}

uint32_t discordInitialize(VS_STACK *stack)
{
    storm::reportScriptStub("DiscordInitialize", "no Discord client: returns false");
    return intStub<3, 0>(stack);
}

uint32_t discordSetRichPresence(VS_STACK *stack)
{
    storm::reportScriptStub("DiscordSetRichPresence", "no Discord client: ignored, returns false");
    return intStub<2, 0>(stack);
}

// Not a stub: CL documents it as the current Unix time (seconds) as a string.
uint32_t discordGetSystemTime(VS_STACK *stack)
{
    return pushString(stack, std::to_string(static_cast<long long>(std::time(nullptr))).c_str());
}
} // namespace

void registerClPlatformNatives()
{
    using storm::registerScriptFunction;
    registerScriptFunction("IsVerified", "int", 0, isVerified);
    registerScriptFunction("Verify", "int", 0, verify);
    registerScriptFunction("GetPersonaName", "string", 0, getPersonaName);
    registerScriptFunction("GetSteamID", "int", 0, getSteamId);
    registerScriptFunction("GetAppID", "int", 0, getAppId);
    registerScriptFunction("IsSteamDeck", "int", 0, isSteamDeck);
    registerScriptFunction("IsVirtualKeyboardShowing", "int", 0, isVirtualKeyboardShowing);
    registerScriptFunction("ShowSteamVirtualKeyboard", "int", 5, showSteamVirtualKeyboard);
    registerScriptFunction("GetOverlaysInfo", "int", 1, getOverlaysInfo);
    registerScriptFunction("GameOverlayToWebPage", "int", 1, gameOverlayToWebPage);
    registerScriptFunction("GameOverlayToWebPageModal", "int", 1, gameOverlayToWebPageModal);
    registerScriptFunction("SteamSetRichPresence", "int", 2, steamSetRichPresence);
    registerScriptFunction("SteamClearRichPresence", "int", 0, steamClearRichPresence);
    registerScriptFunction("BIsSubscribedApp", "int", 1, bIsSubscribedApp);
    registerScriptFunction("BeginCuratorCheckAsync", "int", 2, beginCuratorCheckAsync);
}

bool DISCORD_SCRIPT_LIBRARY::Init()
{
    // Only CL loads this library (#libriary "DISCORD_SCRIPT_LIBRARY").
    storm::registerScriptFunction("DiscordInitialize", "int", 3, discordInitialize);
    storm::registerScriptFunction("DiscordSetRichPresence", "int", 2, discordSetRichPresence);
    storm::registerScriptFunction("DiscordGetSystemTime", "string", 0, discordGetSystemTime);
    return true;
}
} // namespace steamapi
