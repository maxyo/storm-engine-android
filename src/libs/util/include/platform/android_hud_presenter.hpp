#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace storm::android
{
// Which game HUDs the portable UI presents, as it declared once it was mounted
// ("hudowner:sea", "hudowner:worldmap"). Game-thread owned; kept across Activities
// because the UI library, not the Activity, decides it.
//
// One coverage model for every native presentation the touch HUD replaces (land
// command sheet, sea navigator, quick action, ship indicators, world map
// instruments): the UI answers each owner revision once, as an event, when its
// frame is drawn: covering or not covering. Nothing is a lease renewed per frame,
// so the contract does not depend on how fast the UI renders. A revision the UI
// has not answered yet is pending: when the UI presents that HUD, native drawing
// stays off until the answer (no native flash after a load or an owner change,
// however slow the UI's first frame is); otherwise native drawing is unaffected.
//
// The way back is explicit, never a lease: the UI hiding its HUD answers "not covering";
// a UI runtime failure is reported by the platform host ("hudowner:lost"); and a UI
// that leaves a revision unanswered for kSilentMs of engine time is broken, not slow
// (an answer only waits for one UI frame). Both withdraw the declaration loudly: the
// native parts come back, answers that still arrive keep working.
struct HudPresenters
{
    static constexpr uint32_t kSilentMs = 10000;
    bool sea = false, worldMap = false;
    uint32_t seaSince = 0, worldMapSince = 0; // engine tick a revision began waiting; 0 = none
    bool declare(std::string_view hud)
    {
        if (hud == "sea") { seaSince = 0; return sea = true; }
        if (hud == "worldmap") { worldMapSince = 0; return worldMap = true; }
        return false;
    }
    // The UI runtime failed: nothing it declared is presented any more.
    std::string lost()
    {
        const bool had = sea || worldMap;
        sea = worldMap = false; seaSince = worldMapSince = 0;
        return had ? "HUD presenter lost: the UI runtime failed; native sea and world map HUD parts are back" : std::string();
    }
    // Called every engine frame with whether a presented HUD has a revision the UI has not
    // answered. Returns the error line to log when the UI stays silent and is withdrawn.
    std::optional<std::string> watch(std::string_view hud, bool waiting, uint32_t tick)
    {
        bool &declared = hud == "sea" ? sea : worldMap;
        uint32_t &since = hud == "sea" ? seaSince : worldMapSince;
        if (!declared || !waiting) { since = 0; return std::nullopt; }
        if (!since) { since = tick ? tick : 1; return std::nullopt; }
        if (tick - since < kSilentMs) return std::nullopt;
        declared = false; since = 0;
        return "HUD presenter " + std::string(hud) + " withdrawn: the UI left a revision unanswered for " +
            std::to_string(kSilentMs) + " ms; its native parts are back";
    }
};
inline HudPresenters hudPresenters;
} // namespace storm::android
