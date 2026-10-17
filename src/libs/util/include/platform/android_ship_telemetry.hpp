#pragma once
#include "android_gameplay.hpp"
#include <cmath>
#include <cstdlib>
#include <cctype>

namespace storm::android
{
enum ShipHudMask : uint32_t { ShipHull = 1, ShipCrew = 2, ShipAmmo = 4, ShipSailPosition = 8,
                             ShipSailIntegrity = 16, ShipFleet = 32 };
// Runtime text is UTF-8 in the engine's resource pipeline. Never guess another
// encoding: malformed metadata is omitted, while valid names are clipped only
// between code points. Keep the entire JSON payload valid and bounded.
inline std::string shipTextValue(std::string_view text)
{
    size_t end = 0;
    while (end < text.size() && end < 96)
    {
        const auto lead = static_cast<unsigned char>(text[end]);
        const size_t count = lead <= 0x7f ? 1 : lead >= 0xc2 && lead <= 0xdf ? 2 :
            lead >= 0xe0 && lead <= 0xef ? 3 : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
        if (!count || end + count > text.size()) return {};
        for (size_t i = 1; i < count; ++i)
            if ((static_cast<unsigned char>(text[end + i]) & 0xc0) != 0x80) return {};
        if (count >= 3)
        {
            const auto second = static_cast<unsigned char>(text[end + 1]);
            if ((lead == 0xe0 && second < 0xa0) || (lead == 0xed && second >= 0xa0) ||
                (lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second > 0x8f)) return {};
        }
        if (end + count > 96) break;
        end += count;
    }
    return std::string(text.substr(0, end));
}
inline std::optional<double> shipNumberValue(std::string_view text)
{
    if (text.empty() || text.size() > 128) return {};
    const std::string input(text);
    char *end = nullptr;
    const double value = std::strtod(input.c_str(), &end);
    if (end == input.c_str()) return {};
    while (end < input.data() + input.size() && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (end != input.data() + input.size() || !std::isfinite(value) || value < 0) return {};
    return value;
}
struct ShipMetric
{
    double current, maximum;
    bool operator==(const ShipMetric &) const = default;
};
inline std::optional<ShipMetric> shipMetric(double current, double maximum)
{
    if (!std::isfinite(current) || !std::isfinite(maximum) || current < 0 || maximum <= 0) return {};
    return ShipMetric{current, maximum};
}
struct ShipSailGuard
{
    bool sea = false, up = false, down = false, mainCharacter = false;
    bool stopped = false, dead = false, canDropSails = true;
    std::optional<double> state;
};
inline bool shipSailControlAllowed(const ShipSailGuard &guard)
{
    return guard.sea && guard.up && guard.down && guard.mainCharacter && !guard.stopped &&
        !guard.dead && guard.canDropSails && guard.state && std::isfinite(*guard.state) &&
        *guard.state >= 0 && *guard.state <= 1;
}
struct ShipTelemetryEntry
{
    int characterIndex = -1;
    std::string entityId, name, type;
    std::optional<ShipMetric> hull, crew, sails;
    bool operator==(const ShipTelemetryEntry &) const = default;
};
struct ShipTelemetryFrame
{
    bool valid = false;
    ShipTelemetryEntry ship;
    std::vector<ShipTelemetryEntry> fleet;
    std::string ammo;
    int ammoType = -1;
    std::optional<double> ammoCount, sailState;
    bool canSetSails = false;
    bool operator==(const ShipTelemetryFrame &) const = default;
    uint32_t availableMask() const
    {
        if (!valid) return 0;
        uint32_t mask = (ship.hull ? ShipHull : 0) | (ship.crew ? ShipCrew : 0) |
            (ship.sails ? ShipSailIntegrity : 0) | (sailState ? ShipSailPosition : 0);
        if (!ammo.empty() && ammoType >= 0 && ammoCount) mask |= ShipAmmo;
        if (std::all_of(fleet.begin(), fleet.end(), [](const auto &entry) {
            return entry.hull && entry.crew && entry.sails && !entry.name.empty(); })) mask |= ShipFleet;
        return mask;
    }
};
// Native and Java exchange value copies only. All instances are game-thread owned.
class ShipTelemetryState
{
    ShipTelemetryFrame frame_;
    uint64_t revision_ = 0, sequence_ = 0, hudRevision_ = 0;
    uint32_t acknowledged_ = 0, lastPublishedTick_ = 0;
    bool published_ = false, forcePublish_ = true;
    std::string lastPublished_;
    struct SailTarget { uint64_t revision; int detent; uint32_t tick; };
    std::optional<SailTarget> target_;
    static std::optional<std::pair<uint64_t, uint32_t>> parse(std::string_view request)
    {
        if (request.size() > 48) return {};
        const auto tab = request.find('\t');
        if (tab == std::string_view::npos) return {};
        uint64_t revision = 0;
        uint32_t value = 0;
        const auto first = std::from_chars(request.data(), request.data() + tab, revision);
        const auto second = std::from_chars(request.data() + tab + 1, request.data() + request.size(), value);
        if (first.ec != std::errc() || first.ptr != request.data() + tab ||
            second.ec != std::errc() || second.ptr != request.data() + request.size()) return {};
        return std::pair{revision, value};
    }
    static void appendMetric(std::string &json, const char *key, const std::optional<ShipMetric> &metric)
    {
        if (!metric) return;
        json += ",\"" + std::string(key) + "\":" + std::to_string(metric->current) +
            ",\"" + key + "Max\":" + std::to_string(metric->maximum);
    }
    static void appendEntry(std::string &json, const ShipTelemetryEntry &entry)
    {
        json += ",\"characterIndex\":" + std::to_string(entry.characterIndex) +
            ",\"entityId\":" + gameplayJsonString(entry.entityId);
        if (!entry.name.empty()) json += ",\"name\":" + gameplayJsonString(entry.name);
        if (!entry.type.empty()) json += ",\"type\":" + gameplayJsonString(entry.type);
        appendMetric(json, "hull", entry.hull);
        appendMetric(json, "crew", entry.crew);
        appendMetric(json, "sailIntegrity", entry.sails);
    }
public:
    void update(uint64_t revision, ShipTelemetryFrame frame)
    {
        if (revision != revision_)
        {
            revision_ = revision;
            ++hudRevision_;
            acknowledged_ = 0;
            target_.reset();
            forcePublish_ = true;
        }
        if (frame_ != frame)
        {
            const auto sameFleetIdentity = [&]() {
                if (frame_.fleet.size() != frame.fleet.size()) return false;
                for (size_t i = 0; i < frame.fleet.size(); ++i)
                    if (frame_.fleet[i].characterIndex != frame.fleet[i].characterIndex ||
                        frame_.fleet[i].entityId != frame.fleet[i].entityId) return false;
                return true;
            };
            // A ready ACK for an old list cannot hide a newly arrived ship before
            // Java has rendered it. Ordinary changing HP values retain the ACK.
            const bool sameFleet = sameFleetIdentity();
            const bool sameMain = frame_.ship.entityId == frame.ship.entityId;
            if (!sameFleet || !sameMain || frame_.availableMask() != frame.availableMask())
            {
                ++hudRevision_;
                forcePublish_ = true;
            }
            if (!sameFleet) acknowledged_ &= ~ShipFleet;
            if (!sameMain) { acknowledged_ = 0; target_.reset(); }
            if (frame_.valid != frame.valid) forcePublish_ = true;
            frame_ = std::move(frame);
            ++sequence_;
        }
        acknowledged_ &= frame_.availableMask();
        if (!frame_.valid) { acknowledged_ = 0; target_.reset(); }
    }
    const ShipTelemetryFrame &frame() const { return frame_; }
    uint64_t revision() const { return revision_; }
    uint64_t hudRevision() const { return hudRevision_; }
    uint32_t mask() const { return acknowledged_ & frame_.availableMask(); }
    void resetActivity()
    {
        ++hudRevision_;
        acknowledged_ = 0;
        target_.reset();
        published_ = false;
        forcePublish_ = true;
    }
    bool acknowledge(std::string_view request)
    {
        if (request.size() > 80) return false;
        const auto lastTab = request.find_last_of('\t');
        if (lastTab == std::string_view::npos) return false;
        const auto parsed = parse(request.substr(0, lastTab));
        uint64_t hudRevision = 0;
        const auto generation = std::from_chars(request.data() + lastTab + 1, request.data() + request.size(), hudRevision);
        if (!parsed || generation.ec != std::errc() || generation.ptr != request.data() + request.size() ||
            hudRevision != hudRevision_ || parsed->first != revision_ || parsed->second > 63 || !frame_.valid) return false;
        acknowledged_ = parsed->second & frame_.availableMask();
        return true;
    }
    bool offerSails(std::string_view request, uint32_t tick)
    {
        const auto parsed = parse(request);
        if (!parsed || parsed->first != revision_ || parsed->second > 2 || !frame_.valid || !frame_.canSetSails) return false;
        target_ = SailTarget{revision_, static_cast<int>(parsed->second), tick};
        return true;
    }
    std::optional<int> takeSails(uint32_t tick)
    {
        const auto pending = target_;
        target_.reset();
        if (pending && pending->revision == revision_ && tick - pending->tick <= 1500 && frame_.valid && frame_.canSetSails)
            return pending->detent;
        return {};
    }
    std::string json() const
    {
        std::string result = "{\"schemaVersion\":1,\"gameplayRevision\":" + std::to_string(revision_) +
            ",\"sequence\":" + std::to_string(sequence_) + ",\"hudRevision\":" + std::to_string(hudRevision_) +
            ",\"valid\":" + (frame_.valid ? "true" : "false");
        if (frame_.valid)
        {
            appendEntry(result, frame_.ship);
            if (!frame_.ammo.empty()) result += ",\"ammo\":" + gameplayJsonString(frame_.ammo);
            if (frame_.ammoType >= 0) result += ",\"ammoType\":" + std::to_string(frame_.ammoType);
            if (frame_.ammoCount) result += ",\"ammoCount\":" + std::to_string(*frame_.ammoCount);
            if (frame_.sailState) result += ",\"sailState\":" + std::to_string(*frame_.sailState);
            result += std::string(",\"canSetSails\":") + (frame_.canSetSails ? "true" : "false") +
                ",\"sailSteps\":[0,0.5,1],\"fleet\":[";
            for (size_t i = 0; i < frame_.fleet.size(); ++i)
            {
                if (i) result += ',';
                std::string entry;
                appendEntry(entry, frame_.fleet[i]);
                result += '{' + entry.substr(1) + '}';
            }
            result += ']';
        }
        return result + '}';
    }
    std::optional<std::string> publication(uint32_t tick) const
    {
        const auto value = json();
        if (value.size() > 8192 || (published_ && value == lastPublished_)) return {};
        if (published_ && !forcePublish_ && tick - lastPublishedTick_ < 100) return {};
        return value;
    }
    void published(const std::string &value, uint32_t tick)
    {
        lastPublished_ = value;
        lastPublishedTick_ = tick;
        published_ = true;
        forcePublish_ = false;
    }
};
inline ShipTelemetryState shipTelemetry;
} // namespace storm::android
