#pragma once
#include "android_gameplay.hpp"
#include "android_hud_presenter.hpp"
#include <array>

namespace storm::android
{
// The quick action ILogAndActions offers at sea (requirement U1). The complete target
// identity stays native; the touch HUD receives a revision that fences that tuple.
// The script offers one action at a time, by priority (BI_SetSeaState: Board, LandTroops,
// Reload, Moor, Map). `map`: the script would also let the squadron go to the world map now
// (its bMapEnter), so the HUD offers Map beside the action it shows (owner 2026-11-14: moor and
// map together whenever both are possible); `mapLabel` is the script's caption of Map.
struct SeaQuickActionFrame
{
    uint64_t host = 0;
    bool sea = false, available = false, complete = false;
    std::string owner, action, label, target;
    bool map = false;
    std::string mapLabel;
    bool operator==(const SeaQuickActionFrame &) const = default;
};
inline bool knownSeaQuickAction(std::string_view action)
{
    return action == "Map" || action == "Moor" || action == "Board" || action == "LandTroops" || action == "Reload";
}
// Game-thread owned.
//
// Requests "1\t<activity>\t<revision>\t<verb>\t<value>":
//   ack       value 1: the touch control showing exactly this revision is on the screen;
//             value 0: it is not (any more). Said once per revision; the word lasts until the
//             revision changes or the other word comes (android_hud_presenter.hpp). A covered
//             action, or one the presenting UI has not answered yet, hides the native icon.
//   activate  value = the action id: the frame's action, or Map when the frame offers it too.
//             Accepted once per revision, for the covered frame only.
class SeaQuickActionState
{
    inline static std::atomic<uint64_t> nextActivity_{0};
    SeaQuickActionFrame frame_;
    uint64_t activity_ = nextActivity_.fetch_add(1, std::memory_order_relaxed) + 1, revision_ = 1;
    uint64_t answered_ = 0; // the last revision the UI answered
    uint32_t pendingAt_ = 0;
    bool covered_ = false, pending_ = false;
    std::string pendingAction_;
    static bool number(std::string_view text, uint64_t &out)
    {
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), out);
        return !text.empty() && parsed.ec == std::errc() && parsed.ptr == text.data() + text.size();
    }
public:
    static constexpr uint32_t activateMs = 1500; // a tap's age from the mailbox to the game frame
    const SeaQuickActionFrame &frame() const { return frame_; }
    uint64_t activity() const { return activity_; }
    uint64_t revision() const { return revision_; }
    bool valid() const
    {
        return frame_.sea && frame_.host && frame_.available && frame_.complete && knownSeaQuickAction(frame_.action);
    }
    void update(SeaQuickActionFrame frame)
    {
        if (frame == frame_) return;
        // Another action, target or owner: what was drawn and tapped belongs to the old one.
        ++revision_;
        covered_ = pending_ = false;
        frame_ = std::move(frame);
    }
    void resetActivity()
    {
        activity_ = nextActivity_.fetch_add(1, std::memory_order_relaxed) + 1;
        ++revision_;
        covered_ = pending_ = false;
    }
    bool offer(std::string_view request, uint32_t tick)
    {
        if (request.size() > 96) return false;
        std::array<std::string_view, 5> fields;
        for (size_t i = 0; i < fields.size(); ++i)
        {
            const auto tab = request.find('\t');
            if ((i + 1 == fields.size()) != (tab == std::string_view::npos)) return false;
            fields[i] = request.substr(0, tab);
            if (tab != std::string_view::npos) request.remove_prefix(tab + 1);
        }
        uint64_t activity = 0, revision = 0;
        if (fields[0] != "1" || !number(fields[1], activity) || !number(fields[2], revision) || activity != activity_ ||
            revision != revision_) return false;
        if (fields[3] == "ack")
        {
            if (fields[4] != "0" && fields[4] != "1") return false;
            covered_ = fields[4] == "1" && valid();
            answered_ = revision_;
            hudPresenters.answered("sea");
            if (!covered_) pending_ = false;
            return fields[4] == "0" || covered_;
        }
        if (fields[3] != "activate" || !valid() || !(fields[4] == frame_.action || (fields[4] == "Map" && frame_.map)) || pending_ ||
            !covered_)
            return false;
        pending_ = true;
        pendingAction_ = fields[4];
        pendingAt_ = tick;
        return true;
    }
    // The native icon is off whenever the UI presents the sea HUD (one rule,
    // android_hud_presenter.hpp); a tap is accepted only for a drawn control (covered_).
    bool covered(uint64_t host) const
    {
        return valid() && frame_.host == host && (covered_ || hudPresenters.sea);
    }
    // A valid offer the UI has not answered yet (for HudPresenters::watch).
    bool awaiting() const { return valid() && answered_ != revision_; }
    std::optional<std::string> consume(uint32_t tick)
    {
        if (!pending_) return {};
        pending_ = false;
        if (!valid() || tick - pendingAt_ > activateMs) return {};
        // The same action offered again after this is a new one to tap: an echo of this tap is stale.
        ++revision_;
        covered_ = false;
        return std::move(pendingAction_);
    }
    std::string json() const
    {
        return "{\"schemaVersion\":1,\"activity\":" + std::to_string(activity_) + ",\"revision\":" + std::to_string(revision_) +
               ",\"available\":" + (valid() ? "true" : "false") + ",\"action\":" + gameplayJsonString(valid() ? frame_.action : "") +
               ",\"label\":" + gameplayJsonString(valid() ? frame_.label : "") +
               ",\"map\":" + (valid() && frame_.map ? "true" : "false") +
               ",\"mapLabel\":" + gameplayJsonString(valid() && frame_.map ? frame_.mapLabel : "") + "}";
    }
};
inline SeaQuickActionState seaQuickAction;
// The engine loop says whether the player can act at all (focus, lifecycle, no screen, no loading).
inline bool seaQuickLifecycleReady = false;
} // namespace storm::android
