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
// Script control groups can lag entity Execute by a frame. Select a mode only
// when direct modal/native state agrees with the group whose bindings we use.
struct GameplayContext
{
    std::string_view group;
    bool blocked = false, sea = false, land = false, naval = false;
    std::optional<bool> nativeFight;
};
inline std::string_view gameplayMode(const GameplayContext &context)
{
    if (context.blocked) return "unknown";
    if (context.sea)
        return context.group == "Sailing1Pers" || context.group == "Sailing3Pers" ?
            (context.naval ? "naval" : "sailing") : "unknown";
    if (!context.land || !context.nativeFight.has_value()) return "unknown";
    if (context.group == "PrimaryLand" && !*context.nativeFight) return "explore";
    if (context.group == "FightModeControls" && *context.nativeFight) return "melee";
    return "unknown";
}
struct GameplaySnapshot
{
    std::string mode = "unknown", language, group, scene;
    std::vector<std::string> capabilities;
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
            control != "hk_charge3" && control != "hk_charge4") return false;
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
