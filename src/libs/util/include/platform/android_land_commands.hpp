#pragma once
#include "android_gameplay.hpp"
#include <array>
#include <cmath>
#include <limits>

namespace storm::android
{
inline bool landCommandsTextValid(std::string_view value, size_t maximum)
{
    if (value.size() > maximum) return false;
    for (size_t i = 0; i < value.size();)
    {
        const auto lead = static_cast<unsigned char>(value[i]);
        const size_t count = lead >= 32 && lead < 127 ? 1 : lead >= 0xc2 && lead <= 0xdf ? 2 :
            lead >= 0xe0 && lead <= 0xef ? 3 : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
        if (!count || i + count > value.size()) return false;
        for (size_t j = 1; j < count; ++j)
            if ((static_cast<unsigned char>(value[i + j]) & 0xc0) != 0x80) return false;
        // Java's strict parser also rejects the Unicode C1 control range.
        if (lead == 0xc2 && static_cast<unsigned char>(value[i + 1]) < 0xa0) return false;
        if (count >= 3)
        {
            const auto second = static_cast<unsigned char>(value[i + 1]);
            if ((lead == 0xe0 && second < 0xa0) || (lead == 0xed && second >= 0xa0) ||
                (lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second > 0x8f)) return false;
        }
        i += count;
    }
    return true;
}
struct LandCommandItem
{
    std::string id, kind, label, source;
    // Identity is native-only and includes the complete command/target tuple.
    std::string identity;
    bool enabled = true;
    double cooldown = 1;
    bool operator==(const LandCommandItem &) const = default;
};
struct LandCommandsFrame
{
    bool available = false, active = false, complete = true;
    std::string language, stage = "commands", pendingLabel, selectedItemId;
    std::string ownerIdentity; // Actual native command-list instance; never an executable Android token.
    int mode = 0;
    std::vector<LandCommandItem> items;
    bool operator==(const LandCommandsFrame &) const = default;
};
// Missing presentation support is not a gameplay prohibition. The original
// native menu may have been opened without any Android ownership at all.
enum class LandCommandSource { Missing, Forbidden, Ready };
struct LandCommandsRequest
{
    uint64_t scene = 0, revision = 0, activity = 0;
    std::string verb, id;
    uint32_t tick = 0;
};
class LandCommandsState
{
    inline static std::atomic<uint64_t> nextEpoch_{0};
    LandCommandsFrame frame_;
    std::string sceneKey_;
    uint64_t scene_ = 0, revision_ = 1, activity_ = 0;
    bool covered_ = false;
    uint32_t coveredAt_ = 0;
    // Visual continuity only. Never turns an old draw ACK into current coverage
    // or authorizes an action; exact revision checks below remain unchanged.
    bool handoff_ = false;
    uint32_t handoffAt_ = 0;
    uint64_t handoffScene_ = 0, handoffActivity_ = 0;
    std::string handoffOwner_;
    void beginHandoff(uint32_t tick)
    {
        if (handoff_) return; // Repeated known revisions cannot postpone the deadline.
        handoff_ = true; handoffAt_ = tick; handoffScene_ = scene_;
        handoffActivity_ = activity_; handoffOwner_ = frame_.ownerIdentity;
    }
    std::deque<LandCommandsRequest> pending_;
    static bool number(std::string_view value, uint64_t &result)
    {
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
        return !value.empty() && parsed.ec == std::errc() && parsed.ptr == value.data() + value.size();
    }
public:
    const LandCommandsFrame &frame() const { return frame_; }
    uint64_t sceneEpoch() const { return scene_; }
    uint64_t revision() const { return revision_; }
    uint64_t activityGeneration() const { return activity_; }
    bool covered(uint32_t tick) const { return covered_ && frame_.active && frame_.complete && tick - coveredAt_ <= 750; }
    bool holdingPresentation() const
    {
        return handoff_ && frame_.available && frame_.active && frame_.complete &&
            handoffScene_ == scene_ && handoffActivity_ == activity_ && handoffOwner_ == frame_.ownerIdentity;
    }
    bool handoffExpired(uint32_t tick) const { return holdingPresentation() && tick - handoffAt_ >= 1000; }
    bool suppressStock(uint32_t tick) const
    {
        // An expired known handoff remains visually quiet until the game thread
        // closes it. Never paint a stock frame between expiry and native Close.
        return covered(tick) || holdingPresentation();
    }
    bool expireHandoff(uint32_t tick)
    {
        if (!handoffExpired(tick)) return false;
        invalidate(); return true;
    }
    void finishAndroidOperation(const LandCommandsRequest &request, bool executed, uint32_t tick)
    {
        const bool sameOwner = request.scene == scene_ && request.activity == activity_ && request.revision == revision_;
        const bool transition = request.verb == "open" || request.verb == "selectItem" ||
            request.verb == "activateItem" || request.verb == "back";
        const bool known = sameOwner && executed && transition && frame_.available && frame_.complete;
        if (known) beginHandoff(tick);
        invalidate(known);
    }
    void invalidate(bool preserveHandoff = false)
    {
        ++revision_;
        covered_ = false;
        if (!preserveHandoff) handoff_ = false;
        pending_.clear();
    }
    void resetActivity()
    {
        activity_ = nextEpoch_.fetch_add(1, std::memory_order_relaxed) + 1;
        invalidate();
    }
    void update(std::string sceneKey, LandCommandsFrame frame, uint32_t tick = 0)
    {
        // Always publish a bounded valid snapshot, including a recoverable
        // active/Back/Close state for incompatible or malformed script data.
        size_t textBytes = 0;
        const auto checkText = [&](std::string &text, size_t limit, bool required = false) {
            if (!landCommandsTextValid(text, limit) || (required && text.find_first_not_of(" ") == std::string::npos))
            { text = "\xE2\x80\xA6"; frame.complete = false; }
            textBytes += text.size();
        };
        checkText(frame.language, 32); checkText(frame.pendingLabel, 1024);
        for (auto &item : frame.items) { checkText(item.label, 1024, true); checkText(item.source, 512); }
        if (frame.items.size() > 256 || textBytes > 24000)
        {
            frame.items.clear(); frame.selectedItemId.clear(); frame.complete = false;
        }
        // JSON escaping can expand otherwise valid text (quotes/backslashes)
        // beyond the JNI/Java limit. Keep a complete recoverable envelope,
        // including current Back/Close ownership, instead of dropping updates.
        // Headroom covers epoch/revision digit growth later in this update.
        if (jsonFor(frame).size() > 64000)
        {
            frame.items.clear(); frame.selectedItemId.clear(); frame.complete = false;
        }
        const bool samePresentationOwner = sceneKey_ == sceneKey && frame_.ownerIdentity == frame.ownerIdentity;
        const bool canContinue = samePresentationOwner && frame.available && frame.active && frame.complete;
        if (!canContinue) handoff_ = false;
        if (sceneKey_ != sceneKey || !scene_)
        {
            sceneKey_ = std::move(sceneKey);
            scene_ = nextEpoch_.fetch_add(1, std::memory_order_relaxed) + 1;
            invalidate();
        }
        // Fractional cooldown animation must not invalidate every in-flight tap.
        // Enabled state changes still invalidate the list; native confirm checks
        // the freshly sampled cooldown even if a stale UI claims readiness.
        auto stable = frame;
        for (auto &item : stable.items) item.cooldown = item.enabled ? 1 : 0;
        auto old = frame_;
        for (auto &item : old.items) item.cooldown = item.enabled ? 1 : 0;
        if (stable != old) {
            if (canContinue && covered(tick)) beginHandoff(tick);
            invalidate(canContinue);
        }
        frame_ = std::move(frame);
    }
    bool releaseUnavailableSource(LandCommandSource source)
    {
        if (source == LandCommandSource::Ready) return false;
        // Revoke all Android authority on disappearance, including an in-flight
        // visual handoff, without closing an unowned/stock command tree.
        update({}, {});
        return source == LandCommandSource::Forbidden;
    }
    bool offer(std::string_view payload, uint32_t tick)
    {
        if (payload.size() > 256 || pending_.size() >= 8) return false;
        std::array<std::string_view, 6> fields;
        for (size_t i = 0; i < fields.size(); ++i)
        {
            const auto tab = payload.find('\t');
            if (i + 1 == fields.size())
            {
                if (tab != std::string_view::npos) return false;
                fields[i] = payload;
            }
            else
            {
                if (tab == std::string_view::npos) return false;
                fields[i] = payload.substr(0, tab);
                payload.remove_prefix(tab + 1);
            }
        }
        LandCommandsRequest request;
        if (fields[0] != "1" || !number(fields[1], request.scene) || !number(fields[2], request.revision) ||
            !number(fields[3], request.activity) || request.scene != scene_ || request.revision != revision_ ||
            request.activity != activity_ || !activity_ || !frame_.available) return false;
        if (handoffExpired(tick)) return false; // Native timeout closes this owner; no late ACK/action can revive it.
        request.verb = fields[4]; request.id = fields[5]; request.tick = tick;
        if (request.verb == "ack")
        {
            if (request.id != "0" && request.id != "1" && request.id != "2") return false;
            // 2 means the live Android owner is drawing this known snapshot.
            // It may bridge already drawn coverage, but cannot initiate an
            // unacknowledged takeover or extend an existing deadline.
            if (request.id == "2" && covered(tick)) beginHandoff(tick);
            covered_ = request.id == "1" && frame_.active && frame_.complete;
            coveredAt_ = tick;
            if (request.id == "1") handoff_ = false;
            if (request.id == "0") { pending_.clear(); handoff_ = false; }
            return true;
        }
        if (request.verb != "open" && request.verb != "selectItem" &&
            request.verb != "activateItem" && request.verb != "back" && request.verb != "close") return false;
        if ((request.verb == "back" || request.verb == "close") && !request.id.empty()) return false;
        if (request.verb != "open" && !frame_.active) return false;
        if (request.verb == "open" && frame_.active) return false;
        if (request.verb == "open" && !request.id.empty()) return false;
        if (request.verb == "selectItem" || request.verb == "activateItem")
        {
            if (!frame_.complete) return false;
            if (std::none_of(frame_.items.begin(), frame_.items.end(), [&](const auto &item) {
                return item.id == request.id && (request.verb != "activateItem" || (item.enabled && frame_.selectedItemId == request.id));
            })) return false;
        }
        if (!pending_.empty()) return false;
        pending_.push_back(std::move(request));
        return true;
    }
    std::optional<LandCommandsRequest> take(uint32_t tick)
    {
        if (handoffExpired(tick)) { pending_.clear(); return {}; }
        while (!pending_.empty())
        {
            auto request = std::move(pending_.front()); pending_.pop_front();
            if (request.scene == scene_ && request.revision == revision_ && request.activity == activity_ &&
                tick - request.tick <= 1500 && frame_.available) return request;
        }
        return {};
    }
    std::string jsonFor(const LandCommandsFrame &frame) const
    {
        const auto quote = [](const std::string &s) { return gameplayJsonString(s); };
        std::string out = "{\"schemaVersion\":1,\"sceneEpoch\":" + std::to_string(scene_) +
            ",\"revision\":" + std::to_string(revision_) + ",\"activityGeneration\":" + std::to_string(activity_) +
            ",\"available\":" + (frame.available ? "true" : "false") + ",\"active\":" + (frame.active ? "true" : "false") +
            ",\"complete\":" + (frame.complete ? "true" : "false") +
            ",\"language\":" + quote(frame.language) + ",\"stage\":" + quote(frame.stage) +
            ",\"mode\":" + std::to_string(frame.mode) +
            ",\"pendingLabel\":" + quote(frame.pendingLabel) + ",\"selectedItemId\":" + quote(frame.selectedItemId) +
            ",\"canBack\":" + (frame.active ? "true" : "false") + ",\"canClose\":" + (frame.active ? "true" : "false") + ",\"items\":[";
        for (size_t i = 0; i < frame.items.size(); ++i)
        {
            const auto &item = frame.items[i]; if (i) out += ',';
            out += "{\"id\":" + quote(item.id) + ",\"kind\":" + quote(item.kind) + ",\"label\":" + quote(item.label) + ",\"source\":" + quote(item.source) +
                ",\"enabled\":" + (item.enabled ? "true" : "false") + ",\"cooldown\":" +
                std::to_string(std::isfinite(item.cooldown) ? std::clamp(item.cooldown, 0.0, 1.0) : 0.0) + "}";
        }
        return out + "]}";
    }
    std::string json() const { return jsonFor(frame_); }

};
inline LandCommandsState landCommands;
// Defined by battle_interface. All entry points run on the game thread and
// resolve the live entity on each call; no retained pointers cross a frame.
void refreshLandCommands(bool ready);
bool executeLandCommand(const LandCommandsRequest &request);
void closeLandCommands();
} // namespace storm::android
