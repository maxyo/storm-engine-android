#pragma once
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#if defined(__ANDROID__) && __has_include(<android/log.h>)
#include <android/log.h>
#define STORM_HUD_PRESENTER_LOG 1
#endif

namespace storm::android
{
// Which game HUDs the portable UI presents, as it declared once it was mounted
// ("hudowner:land", "hudowner:sea", "hudowner:worldmap", "hudowner:spyglass"). Game-thread
// owned; kept across Activities because the UI library, not the Activity, decides it.
//
// ONE coverage rule: while the UI presents a HUD, the legacy rendering of that HUD (the battle,
// land and world-map interfaces, their icons, readouts, markers, perk icons, log lines, action
// hints, the native compass, the spyglass info) is off at its source, every frame,
// deterministically: no answer, no timing, no race. Whatever the game shows that the touch
// HUD lacks must be ported into it, never let through; a legacy layer that tries to draw under
// coverage is refused and logged once (legacyDrawRefused).
//
// The UI still answers each owner revision once (covering or not), as an event: those answers
// carry what the engine needs (the navigator aperture, the quick action's authority to
// activate, the orders panel) and prove the UI alive. The way back to the legacy HUD is
// explicit: a UI runtime failure reported by the platform host ("hudowner:lost"), or a UI that
// leaves a revision unanswered for kSilentMs of engine time (an answer only waits for one UI
// frame): the declaration is withdrawn loudly and the legacy HUD returns.
struct HudPresenters
{
    static constexpr uint32_t kSilentMs = 10000;
    bool land = false, sea = false, worldMap = false, spyglass = false;
    uint32_t seaSince = 0, worldMapSince = 0, spyglassSince = 0; // engine tick a revision began waiting; 0 = none
    bool declare(std::string_view hud)
    {
        if (hud == "land") return land = true;
        if (hud == "sea") { seaSince = 0; return sea = true; }
        if (hud == "worldmap") { worldMapSince = 0; return worldMap = true; }
        if (hud == "spyglass") { spyglassSince = 0; return spyglass = true; }
        return false;
    }
    // Legacy HUD layers shared by every mode (perk icons, log lines, timers): off while the UI
    // presents any HUD (it presents them all in this application).
    bool any() const { return land || sea || worldMap || spyglass; }
    // The UI runtime failed: nothing it declared is presented any more.
    std::string lost()
    {
        const bool had = any();
        land = sea = worldMap = spyglass = false; seaSince = worldMapSince = spyglassSince = 0;
        return had ? "HUD presenter lost: the UI runtime failed; the legacy HUD is back" : std::string();
    }
    // Called every engine frame with whether a presented HUD has a revision the UI has not
    // answered. Returns the error line to log when the UI stays silent and is withdrawn.
    std::optional<std::string> watch(std::string_view hud, bool waiting, uint32_t tick)
    {
        bool &declared = hud == "sea" ? sea : hud == "worldmap" ? worldMap : spyglass;
        uint32_t &since = hud == "sea" ? seaSince : hud == "worldmap" ? worldMapSince : spyglassSince;
        if (!declared || !waiting) { since = 0; return std::nullopt; }
        if (!since) { since = tick ? tick : 1; return std::nullopt; }
        if (tick - since < kSilentMs) return std::nullopt;
        declared = false; since = 0;
        return "HUD presenter " + std::string(hud) + " withdrawn: the UI left a revision unanswered for " +
            std::to_string(kSilentMs) + " ms; its legacy HUD is back";
    }
};
inline HudPresenters hudPresenters;

// A legacy HUD layer asks to draw. True: it must not (the UI presents that HUD); the first
// refusal of each layer is logged, loudly, so a layer the touch HUD still lacks gets noticed.
inline bool legacyDrawRefused(const char *layer, bool presented)
{
    if (!presented) return false;
    static std::set<std::string> reported;
    if (reported.insert(layer).second)
    {
#ifdef STORM_HUD_PRESENTER_LOG
        __android_log_print(ANDROID_LOG_WARN, "StormHud", "legacy HUD layer '%s' refused: the touch HUD presents this mode", layer);
#endif
    }
    return true;
}
inline bool legacyDrawRefused(const char *layer) { return legacyDrawRefused(layer, hudPresenters.any()); }
} // namespace storm::android
