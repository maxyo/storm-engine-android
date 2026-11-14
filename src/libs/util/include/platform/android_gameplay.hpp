#pragma once
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace storm::android
{
// Game-thread owned. Java only receives immutable JSON and supplies a bounded
// mailbox through pollMobileAction; no script/entity pointers cross threads.
// Script control groups can lag entity Execute by a frame. For the two ordinary
// land groups, the verified Player owns stance; actual group bindings still own
// capabilities. Modal, scene and player guards are resolved before this policy.
struct GameplayContext
{
    std::string_view group;
    // blocked: a command menu (ComState) or a video takes the controls; video says which.
    bool blocked = false, video = false, sea = false, land = false, naval = false, worldmap = false;
    bool location = false; // a land location is loaded (land: its interface is started too)
    std::optional<bool> nativeFight;
};
inline std::string_view gameplayMode(const GameplayContext &context)
{
    if (context.blocked) return "unknown";
    if (context.worldmap)
        return context.group == "WorldMapControls" ? "worldmap" : "unknown";
    if (context.sea)
        return context.group == "Sailing1Pers" || context.group == "Sailing3Pers" ?
            (context.naval ? "naval" : "sailing") : "unknown";
    if (!context.land || !context.nativeFight.has_value()) return "unknown";
    if (context.group == "PrimaryLand" || context.group == "FightModeControls")
        return *context.nativeFight ? "melee" : "explore";
    return "unknown";
}
// Why gameplayMode is "unknown" (GameplaySnapshot::blocked): the touch UI decides from it
// whether the screen shows anything the player can use (storm::ui Host::WantsTouch).
inline std::string_view gameplayBlocked(const GameplayContext &context)
{
    if (context.blocked) return context.video ? "video" : "commands";
    if (context.worldmap) return context.group == "WorldMapControls" ? "" : "group";
    if (context.sea) return context.group == "Sailing1Pers" || context.group == "Sailing3Pers" ? "" : "group";
    if (!context.land) return context.location ? "no-land-interface" : "no-scene";
    if (!context.nativeFight.has_value()) return "no-player";
    if (context.group == "PrimaryLand" || context.group == "FightModeControls") return "";
    return "group";
}
struct GameplaySnapshot
{
    std::string mode = "unknown", language, group, scene;
    std::vector<std::string> capabilities;
    // Stable only for one native land Player in one loaded scene. Android may
    // retain locomotion across stance revisions only when this nonempty token matches.
    std::string inputOwner;
    bool analogMovement = false;
    uint64_t movementGeneration = 0;
    // Mode "unknown" only: why. starting (the engine or its scripts not up yet), paused (the
    // app not in front), loading (the loading screen), transition (a fader is up: a scene change,
    // or the fade in after a load), interface (one open or launched),
    // dialogue, cutscene (the hero is an actor), video, commands (a command menu), no-scene
    // (no land, sea or world map yet), no-land-interface (a location whose land interface is not
    // started), no-player, group (a control group no mode knows),
    // error (the scripts could not be read).
    std::string blocked;
    bool operator==(const GameplaySnapshot &) const = default;
};
inline std::string gameplayJsonString(std::string_view value)
{
    std::string out = "\"";
    for (unsigned char c : value)
    {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c >= 32) out += c;
    }
    return out + '"';
}
class GameplayState
{
    struct Action { uint64_t revision; std::string control; uint32_t tick; };
    // Survives SDL_main restart in the same process/Activity.
    inline static std::atomic<uint64_t> nextRevision_{0};
    GameplaySnapshot snapshot_;
    uint64_t revision_ = 0;
    std::deque<Action> pending_;
public:
    bool update(GameplaySnapshot snapshot)
    {
        if (revision_ && snapshot_ == snapshot) return false;
        snapshot_ = std::move(snapshot);
        revision_ = nextRevision_.fetch_add(1, std::memory_order_relaxed) + 1;
        pending_.clear();
        return true;
    }
    uint64_t revision() const { return revision_; }
    const GameplaySnapshot &snapshot() const { return snapshot_; }
    bool allows(std::string_view control) const
    {
        return std::find(snapshot_.capabilities.begin(), snapshot_.capabilities.end(), control) != snapshot_.capabilities.end();
    }
    std::string json() const
    {
        std::string result = "{\"schemaVersion\":1,\"revision\":" + std::to_string(revision_) +
            ",\"mode\":" + gameplayJsonString(snapshot_.mode) + ",\"language\":" + gameplayJsonString(snapshot_.language) +
            ",\"inputOwner\":" + gameplayJsonString(snapshot_.inputOwner) +
            ",\"analogMovement\":" + (snapshot_.analogMovement ? "true" : "false") +
            ",\"movementGeneration\":" + std::to_string(snapshot_.movementGeneration) +
            ",\"blocked\":" + gameplayJsonString(snapshot_.blocked) +
            ",\"capabilities\":[";
        for (size_t i = 0; i < snapshot_.capabilities.size(); ++i)
        {
            if (i) result += ',';
            result += gameplayJsonString(snapshot_.capabilities[i]);
        }
        return result + "]}";
    }
    bool offer(std::string_view request, uint32_t tick)
    {
        if (pending_.size() >= 8 || request.size() > 96) return false;
        const auto tab = request.find('\t');
        if (tab == std::string_view::npos) return false;
        uint64_t revision = 0;
        const auto parsed = std::from_chars(request.data(), request.data() + tab, revision);
        if (parsed.ec != std::errc() || parsed.ptr != request.data() + tab || revision != revision_) return false;
        const auto control = request.substr(tab + 1);
        // Only controls consumed by the script's Control Activation handler.
        // Locomotion and melee controls are polled/held and must use normal input.
        if (control != "Ship_Fire" && control != "Sea_CameraSwitch" &&
            control != "hk_charge1" && control != "hk_charge2" &&
            control != "hk_charge3" && control != "hk_charge4" &&
            control != "TimeScaleX1" && control != "TimeScaleX2" && control != "TimeScaleX4" && control != "TimeScaleX8" &&
            control != "BI_InstantRepair" && control != "BI_ImmediateReload") return false;
        if (!allows(control)) return false;
        pending_.push_back({revision, std::string(control), tick});
        return true;
    }
    std::optional<std::string> take(uint32_t tick)
    {
        while (!pending_.empty())
        {
            auto action = std::move(pending_.front());
            pending_.pop_front();
            if (action.revision == revision_ && tick - action.tick <= 1500 && allows(action.control))
                return action.control;
        }
        return std::nullopt;
    }
};
} // namespace storm::android
