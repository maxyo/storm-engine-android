#pragma once
#include "android_gameplay.hpp"
#include "android_hud_presenter.hpp"
#include <cmath>
#include <array>
#include <cstdlib>
#include <cctype>
#include <limits>
#include <cstdio>

namespace storm::android
{
enum ShipHudMask : uint32_t { ShipHull = 1, ShipCrew = 2, ShipAmmo = 4, ShipSailPosition = 8,
                             ShipSailIntegrity = 16, ShipFleet = 32, ShipSpeed = 64, ShipPowder = 128,
                             ShipDateTime = 256, ShipLocation = 512, ShipWind = 1024,
                             ShipAll = 2047 };
inline constexpr uint32_t ShipCaptions = ShipSpeed | ShipPowder | ShipDateTime | ShipLocation | ShipWind;
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
// Captions must be complete: clipping a date/location must never authorize hiding
// its native counterpart. Reject malformed UTF-8, controls and oversize input.
inline std::string shipCaptionText(std::string_view text)
{
    if (text.size() > 160) return {};
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    for (size_t i = 0; i < text.size();)
    {
        const auto lead = static_cast<unsigned char>(text[i]);
        const size_t count = lead >= 0x20 && lead <= 0x7e ? 1 : lead >= 0xc2 && lead <= 0xdf ? 2 :
            lead >= 0xe0 && lead <= 0xef ? 3 : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
        if (!count || i + count > text.size()) return {};
        for (size_t j = 1; j < count; ++j)
            if ((static_cast<unsigned char>(text[i + j]) & 0xc0) != 0x80) return {};
        if (count >= 3)
        {
            const auto second = static_cast<unsigned char>(text[i + 1]);
            if ((lead == 0xe0 && second < 0xa0) || (lead == 0xed && second >= 0xa0) ||
                (lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second > 0x8f)) return {};
        }
        i += count;
    }
    return std::string(text);
}
inline std::string shipDateTimeText(std::string_view text, std::string_view prefix)
{
    if (prefix.empty() || shipCaptionText(prefix).empty() || !text.starts_with(prefix)) return {};
    return shipCaptionText(text.substr(prefix.size()));
}
inline std::optional<double> shipDisplayedSpeed(double value)
{
    if (!std::isfinite(value)) return {};
    // Match the original ExtPrint %.1f rounding, including signed reverse speed.
    char display[64];
    const int length = std::snprintf(display, sizeof(display), "%.1f", value);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(display)) return {};
    char *end = nullptr;
    const auto rounded = std::strtod(display, &end);
    if (end != display + length || !std::isfinite(rounded)) return {};
    return rounded == 0 ? 0 : rounded;
}
inline std::optional<int32_t> shipPowderCount(std::string_view text)
{
    const auto value = shipNumberValue(text);
    if (!value || *value > std::numeric_limits<int32_t>::max() || std::floor(*value) != *value) return {};
    return static_cast<int32_t>(*value);
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
// The ship's picture as the original battle interface draws it: a cell of its ship-icon atlas
// (BattleInterface.ShipIcon / wm_sign.shiptexturename, battle_interface\ship_icons2.tga). `texture`
// is the texture-namespace name ("battle_interface/ship_icons2"), `uv` left, top, right, bottom 0…1.
struct ShipPicture
{
    std::string texture;
    std::array<float, 4> uv{};
    bool operator==(const ShipPicture &) const = default;
};
// "battle_interface\ship_icons2.tga.tx" → "battle_interface/ship_icons2"; "" when it is not a
// plain relative texture name (the UI resolves it under RESOURCE/Textures of the player's game).
inline std::string shipPictureTexture(std::string_view file)
{
    std::string name;
    for (const char c : file)
    {
        if (static_cast<unsigned char>(c) < 0x20 || c == ':' || c == '"') return {};
        name += c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (const char *suffix : {".tx", ".tga"})
    {
        const std::string_view tail(suffix);
        if (name.size() > tail.size() && std::string_view(name).substr(name.size() - tail.size()) == tail)
            name.resize(name.size() - tail.size());
    }
    if (name.empty() || name.size() > 128 || name.front() == '/' || name.find("..") != std::string::npos) return {};
    return name;
}
// A picture with a texture and a rectangle inside 0…1, or nothing.
inline std::optional<ShipPicture> shipPicture(std::string_view file, const std::array<float, 4> &uv)
{
    ShipPicture picture{shipPictureTexture(file), uv};
    for (const float v : uv)
        if (!std::isfinite(v) || v < 0.f || v > 1.f) return {};
    if (picture.texture.empty() || uv[2] <= uv[0] || uv[3] <= uv[1]) return {};
    return picture;
}
inline std::string shipPictureJson(const ShipPicture &picture)
{
    char uv[96];
    std::snprintf(uv, sizeof uv, "[%.6g,%.6g,%.6g,%.6g]", picture.uv[0], picture.uv[1], picture.uv[2], picture.uv[3]);
    return "{\"texture\":" + gameplayJsonString(picture.texture) + ",\"uv\":" + uv + "}";
}
struct ShipTelemetryEntry
{
    int characterIndex = -1;
    std::string entityId, name, type;
    std::optional<ShipMetric> hull, crew, sails;
    std::optional<int32_t> shipClass;
    std::optional<ShipPicture> picture;
    bool operator==(const ShipTelemetryEntry &) const = default;
};
inline constexpr std::array<const char *, 4> ShipAmmoNames{"balls", "grapes", "knippels", "bombs"};
inline std::optional<int64_t> shipAmmoCount(std::string_view text)
{
    const auto value = shipNumberValue(text);
    if (!value || *value >= 0x1p63 || std::floor(*value) != *value) return {};
    return static_cast<int64_t>(*value);
}
// A battle ability of the ship's people as a touch HUD shows it (android_ship_abilities.hpp).
struct ShipAbility
{
    enum State { Ready, Active, Recovery, Blocked };
    std::string id; // InstantRepair, ImmediateReload
    State state = Blocked;
    double fill = 0.0; // Active: what is left of the effect; Recovery: how far it has recovered
    bool operator==(const ShipAbility &) const = default;
};
struct ShipTelemetryFrame
{
    bool valid = false;
    ShipTelemetryEntry ship;
    std::vector<ShipTelemetryEntry> fleet;
    std::string ammo;
    int ammoType = -1;
    std::optional<double> ammoCount, sailState;
    // Cargo inventory per ammunition kind; an absent attribute is not an empty hold.
    std::array<std::optional<int64_t>, 4> ammoCounts{};
    bool canSetSails = false;
    bool captionsVisible = false;
    bool navigatorAvailable = false, navigatorZoomIn = false, navigatorZoomOut = false;
    uint64_t navigatorGeneration = 0, navigatorLayoutId = 0;
    std::optional<bool> navigatorAmmoWarning, navigatorPowderWarning;
    std::optional<int> navigatorWindState;

    std::optional<double> shipSpeedKnots, windSpeed;
    // How far the guns are loaded: the mean charge of the cannon sides the ship has, 0…1.
    std::optional<double> cannonCharge;
    // The world's time scale the scripts set (the game's acceleration, x1 = 1).
    std::optional<double> timeScale;
    std::vector<ShipAbility> abilities;
    std::optional<int32_t> powderCount;
    std::string dateTimeText, locationText, navigatorAmmoText;
    bool operator==(const ShipTelemetryFrame &) const = default;
    uint32_t availableMask() const
    {
        if (!valid) return 0;
        uint32_t mask = (ship.hull ? ShipHull : 0) | (ship.crew ? ShipCrew : 0) |
            (ship.sails ? ShipSailIntegrity : 0) | (sailState ? ShipSailPosition : 0);
        if (!ammo.empty() && ammoType >= 0 && ammoCount) mask |= ShipAmmo;
        if (std::all_of(fleet.begin(), fleet.end(), [](const auto &entry) {
            return entry.hull && entry.crew && entry.sails && !entry.name.empty(); })) mask |= ShipFleet;
        if (captionsVisible)
        {
            if (shipSpeedKnots) mask |= ShipSpeed;
            if (windSpeed) mask |= ShipWind;
            if (powderCount) mask |= ShipPowder;
            if (!dateTimeText.empty()) mask |= ShipDateTime;
            if (!locationText.empty()) mask |= ShipLocation;
        }
        return mask;
    }
};
// Resolve current battle-interface captions on the game thread; implemented by battle_interface.
#ifdef __ANDROID__
void readShipCaptions(ShipTelemetryFrame &frame, uint64_t mainShipId);
// The character's ship picture in the battle interface's ship icons (BIShipIcon: the picture
// number the script gives for BIDT_SHIPPICTURE, the icon atlas and its grid); implemented by
// battle_interface. Nothing while the battle interface has no such ship.
std::optional<ShipPicture> seaShipPicture(int characterIndex);
#endif

// Native and Java exchange value copies only. All instances are game-thread owned.
class ShipTelemetryState
{
    ShipTelemetryFrame frame_;
    uint64_t revision_ = 0, sequence_ = 0, hudRevision_ = 0;
    uint64_t answered_ = 0; // the last HUD revision the UI answered with its mask
    uint32_t lastPublishedTick_ = 0;
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
        if (entry.shipClass) json += ",\"shipClass\":" + std::to_string(*entry.shipClass);
        if (entry.picture) json += ",\"picture\":" + shipPictureJson(*entry.picture);
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
            // Another fleet, main ship, availability or caption visibility is a new HUD revision
            // for the UI to answer; ordinary changing HP values are not.
            const bool sameFleet = sameFleetIdentity();
            const bool sameMain = frame_.ship.entityId == frame.ship.entityId;
            if (!sameFleet || !sameMain || frame_.availableMask() != frame.availableMask() ||
                frame_.captionsVisible != frame.captionsVisible)
            {
                ++hudRevision_;
                forcePublish_ = true;
            }
            if (!sameMain) target_.reset();
            if (frame_.valid != frame.valid) forcePublish_ = true;
            frame_ = std::move(frame);
            ++sequence_;
        }
        if (!frame_.valid) target_.reset();
    }
    const ShipTelemetryFrame &frame() const { return frame_; }
    uint64_t revision() const { return revision_; }
    uint64_t hudRevision() const { return hudRevision_; }
    // The native indicators the touch HUD replaces: all of them while the UI presents the sea HUD
    // (android_hud_presenter.hpp: one deterministic rule, no answer timing); otherwise none is
    // replaced (no UI acknowledges anything then).
    uint32_t mask() const { return hudPresenters.sea ? ShipAll : 0u; }
    // A valid revision the UI has not answered yet (its silence watchdog, HudPresenters::watch).
    bool awaiting() const { return frame_.valid && answered_ != hudRevision_; }
    void resetActivity()
    {
        ++hudRevision_;
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
            hudRevision != hudRevision_ || parsed->first != revision_ || parsed->second > ShipAll || !frame_.valid) return false;
        answered_ = hudRevision_;
        hudPresenters.answered("sea");
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
            result += std::string(",\"captionsVisible\":") + (frame_.captionsVisible ? "true" : "false");
            result += std::string(",\"navigatorAvailable\":") + (frame_.navigatorAvailable ? "true" : "false");
            result += ",\"navigatorGeneration\":" + std::to_string(frame_.navigatorGeneration);
            result += ",\"navigatorLayoutId\":" + std::to_string(frame_.navigatorLayoutId);
            result += std::string(",\"navigatorZoomIn\":") + (frame_.navigatorZoomIn ? "true" : "false");
            result += std::string(",\"navigatorZoomOut\":") + (frame_.navigatorZoomOut ? "true" : "false");
            if(frame_.navigatorAmmoWarning) result += std::string(",\"navigatorAmmoWarning\":") + (*frame_.navigatorAmmoWarning ? "true" : "false");
            if(frame_.navigatorPowderWarning) result += std::string(",\"navigatorPowderWarning\":") + (*frame_.navigatorPowderWarning ? "true" : "false");
            if(frame_.navigatorWindState) result += ",\"navigatorWindState\":" + std::to_string(*frame_.navigatorWindState);
            if (frame_.shipSpeedKnots) result += ",\"shipSpeedKnots\":" + std::to_string(*frame_.shipSpeedKnots);
            if (frame_.windSpeed) result += ",\"windSpeed\":" + std::to_string(*frame_.windSpeed);
            if (frame_.cannonCharge) result += ",\"cannonCharge\":" + std::to_string(*frame_.cannonCharge);
            if (frame_.timeScale) result += ",\"timeScale\":" + std::to_string(*frame_.timeScale);
            if (!frame_.abilities.empty())
            {
                static constexpr const char *states[] = {"ready", "active", "recovery", "blocked"};
                result += ",\"abilities\":[";
                for (size_t i = 0; i < frame_.abilities.size(); ++i)
                {
                    const auto &ability = frame_.abilities[i];
                    result += std::string(i ? "," : "") + "{\"id\":" + gameplayJsonString(ability.id) + ",\"state\":\"" + states[ability.state] +
                              "\",\"fill\":" + std::to_string(ability.fill) + "}";
                }
                result += "]";
            }
            if (frame_.powderCount) result += ",\"powderCount\":" + std::to_string(*frame_.powderCount);
            if (!frame_.dateTimeText.empty()) result += ",\"dateTimeText\":" + gameplayJsonString(frame_.dateTimeText);
            if (!frame_.locationText.empty()) result += ",\"locationText\":" + gameplayJsonString(frame_.locationText);
            if (!frame_.navigatorAmmoText.empty()) result += ",\"navigatorAmmoText\":" + gameplayJsonString(frame_.navigatorAmmoText);
            if (!frame_.ammo.empty()) result += ",\"ammo\":" + gameplayJsonString(frame_.ammo);
            if (frame_.ammoType >= 0) result += ",\"ammoType\":" + std::to_string(frame_.ammoType);
            if (frame_.ammoCount) result += ",\"ammoCount\":" + std::to_string(*frame_.ammoCount);
            result += ",\"ammoCounts\":{";
            bool firstAmmo = true;
            for (size_t i = 0; i < ShipAmmoNames.size(); ++i)
                if (frame_.ammoCounts[i])
                {
                    if (!firstAmmo) result += ',';
                    firstAmmo = false;
                    result += gameplayJsonString(ShipAmmoNames[i]) + ':' + std::to_string(*frame_.ammoCounts[i]);
                }
            result += '}';
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
