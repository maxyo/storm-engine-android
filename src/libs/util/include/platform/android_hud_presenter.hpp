#pragma once
#include <array>
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
// The rule covers the HUD, the screen-space panels the touch HUD replaces. Markers the game
// draws in the scene itself (the hull/sails/crew bars and relation icons over ships and forts,
// the pointer over an order's target ship) are part of the 3D view: the touch HUD has nothing in
// their place, so they always draw. Every legacy layer is classified once below (kLegacyLayers):
// a world-space layer is logged once as shown, a layer missing from the table is an error.
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

enum class LegacyLayerKind
{
    Hud,   // screen-space HUD: the touch HUD presents it, refused under coverage
    World, // drawn in the scene: shown under the touch HUD too
};
struct LegacyLayer
{
    std::string_view name;
    LegacyLayerKind kind;
};
inline constexpr std::array<LegacyLayer, 15> kLegacyLayers{{
    {"sea interface", LegacyLayerKind::Hud},      // info images, perk/state and command icons, readouts, lines, border
    {"sea navigator", LegacyLayerKind::Hud},      // native compass: the touch HUD's disc
    {"sea ship cards", LegacyLayerKind::Hud},     // squadron cards: the touch HUD's ship cards
    {"land interface", LegacyLayerKind::Hud},     // location images and texts: the touch HUD's location line
    {"land command list", LegacyLayerKind::Hud},  // stock action list: the touch HUD's sheet
    {"log and actions", LegacyLayerKind::Hud},    // log lines and action hints
    {"active perk icons", LegacyLayerKind::Hud},  // perk icons over the time control
    {"timer bar", LegacyLayerKind::Hud},          // countdown bar
    {"interface cursor", LegacyLayerKind::Hud},   // mouse pointer of the original interface
    {"loading spinner", LegacyLayerKind::Hud},    // loading/saving spinner
    {"world map instruments", LegacyLayerKind::Hud}, // compass, date, coordinates, flag: the touch HUD's instruments
    {"world map fleet", LegacyLayerKind::Hud},    // ship signs of the squadron: the touch HUD's fleet
    {"ship bars", LegacyLayerKind::World},        // hull/sails/crew bars and relation icon over ships and forts
    {"ship pointer", LegacyLayerKind::World},     // pointer over an order's target ship
    // Not world-space, but the touch HUD has nothing in its place yet: shown until it is ported
    // into the HUD (two side-strength bars), then refused again.
    {"boarding status", LegacyLayerKind::World},  // crew strength bars of a deck fight
}};
inline const LegacyLayer *legacyLayer(std::string_view name)
{
    for (const auto &layer : kLegacyLayers)
        if (layer.name == name) return &layer;
    return nullptr;
}

// A legacy layer asks to draw. True: it must not (the UI presents that HUD); the first refusal
// of each HUD layer is logged, loudly, so a layer the touch HUD still lacks gets noticed. A
// world-space layer is never refused; an unclassified one is refused and reported as an error.
inline bool legacyDrawRefused(const char *layer, bool presented)
{
    if (!presented) return false;
    const LegacyLayer *known = legacyLayer(layer);
    static std::set<std::string> reported;
    const bool first = reported.insert(layer).second;
#ifdef STORM_HUD_PRESENTER_LOG
    if (first)
    {
        if (!known)
            __android_log_print(ANDROID_LOG_ERROR, "StormHud", "legacy layer '%s' is not classified (android_hud_presenter.hpp); refused", layer);
        else if (known->kind == LegacyLayerKind::World)
            __android_log_print(ANDROID_LOG_INFO, "StormHud", "world-space layer '%s' shown under the touch HUD", layer);
        else
            __android_log_print(ANDROID_LOG_WARN, "StormHud", "legacy HUD layer '%s' refused: the touch HUD presents this mode", layer);
    }
#else
    (void)first;
#endif
    return !known || known->kind == LegacyLayerKind::Hud;
}
inline bool legacyDrawRefused(const char *layer) { return legacyDrawRefused(layer, hudPresenters.any()); }
} // namespace storm::android
