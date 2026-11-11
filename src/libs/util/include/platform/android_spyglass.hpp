#pragma once
// The spyglass (ISPYGLASS) for the portable UI: what the telescope shows about its target
// as the `spyglass` state channel, and the UI's coverage answer that lets the engine hide
// its own info icons and texts while the UI draws them: "<generation>\t1" once its overlay
// is drawn for this raise, "<generation>\t0" when it is not (any more). The word lasts until
// the next raise; while the presenting UI has not answered a raise the native info stays
// off (android_hud_presenter.hpp, no lease).
#include "android_gameplay.hpp"
#include "android_hud_presenter.hpp"
#include "android_ship_telemetry.hpp"

#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>
#include <cstdlib>
#include <string>
#include <string_view>

namespace storm::android
{

// One target as the game's script describes it (MSG_ISG_UPDATE). A field the telescope
// cannot show at its level arrives as -1 or empty and is left out of the state.
struct SpyglassTarget
{
    bool fort = false;
    std::string name, type, captain, cannonType, portrait;
    int shipClass = -1, hull = -1, sails = -1, crew = -1, cannons = -1, cannonsMax = -1;
    int charge = -1, nation = -1, sail = -1;
    double speed = -1.0, distance = -1.0;
    int skills[5] = {-1, -1, -1, -1, -1}; // fencing, cannons, accuracy, navigation, boarding
    bool operator==(const SpyglassTarget &) const = default;
};

// The script's captain line is "<cannon type>  [<musket volley>]  <name>": parts apart by
// runs of two or more spaces. Returns {cannon type, captain name}.
inline std::pair<std::string, std::string> spyglassCaptainLine(std::string_view line)
{
    std::vector<std::string> parts;
    size_t i = 0;
    while (i < line.size())
    {
        while (i < line.size() && line[i] == ' ')
            ++i;
        size_t end = i;
        while (end < line.size() && !(line[end] == ' ' && end + 1 < line.size() && line[end + 1] == ' '))
            ++end;
        std::string part(line.substr(i, end - i));
        while (!part.empty() && part.back() == ' ')
            part.pop_back();
        if (!part.empty())
            parts.push_back(std::move(part));
        i = end;
    }
    if (parts.empty())
        return {};
    if (parts.size() == 1) // a line without its cannon type: only the name was sent
        return {std::string(), parts.front()};
    return {parts.front(), parts.back()};
}

class SpyglassState
{
  public:
    void turn(bool on)
    {
        if (on == on_)
            return;
        on_ = on;
        known_ = false;
        covering_ = false;
        target_ = SpyglassTarget();
        ++generation_; // every raise of the telescope is a new view: old answers lapse
        ++revision_;
    }
    void target(const SpyglassTarget &next)
    {
        if (known_ && next == target_)
            return;
        target_ = next;
        known_ = true;
        ++revision_;
    }
    void clear()
    {
        if (!known_)
            return;
        known_ = false;
        target_ = SpyglassTarget();
        ++revision_;
    }
    bool on() const
    {
        return on_;
    }
    // A target is being shown (the telescope is up and found one).
    bool shows() const
    {
        return on_ && known_;
    }
    uint64_t generation() const
    {
        return generation_;
    }

    // "<generation>\t<1|0>" from the UI once a frame with (1) or without (0) the overlay is drawn.
    bool acknowledge(std::string_view value)
    {
        const auto tab = value.find('\t');
        if (tab == std::string_view::npos) return false;
        const std::string text(value.substr(0, tab));
        const auto word = value.substr(tab + 1);
        char *end = nullptr;
        const unsigned long long generation = std::strtoull(text.c_str(), &end, 10);
        if (text.empty() || !end || *end != '\0' || generation != generation_ || !on_ || (word != "0" && word != "1"))
            return false;
        answered_ = generation_;
        covering_ = word == "1";
        return true;
    }
    bool awaiting() const { return on_ && answered_ != generation_; }
    bool covered() const
    {
        // One coverage rule (android_hud_presenter.hpp): the UI presents the spyglass info.
        return on_ && (hudPresenters.spyglass || (answered_ == generation_ && covering_));
    }

    std::string json() const
    {
        std::string out = "{\"schemaVersion\":1,\"revision\":" + std::to_string(revision_) + ",\"on\":" + (on_ ? "true" : "false") +
                          ",\"generation\":" + std::to_string(generation_) + ",\"target\":";
        if (!on_ || !known_)
            return out + "null}";
        const SpyglassTarget &t = target_;
        const auto text = [](const char *key, const std::string &value) {
            const std::string clean = shipTextValue(value);
            return clean.empty() ? std::string() : std::string(",\"") + key + "\":" + gameplayJsonString(clean);
        };
        const auto number = [](const char *key, int value) {
            return value < 0 ? std::string() : std::string(",\"") + key + "\":" + std::to_string(value);
        };
        const auto decimal = [](const char *key, double value) {
            if (!(value >= 0.0) || value > 1e6)
                return std::string();
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.1f", value);
            return std::string(",\"") + key + "\":" + buffer;
        };
        out += std::string("{\"kind\":\"") + (t.fort ? "fort" : "ship") + "\"";
        out += text("name", t.name) + text("type", t.type) + number("class", t.shipClass) + number("hull", t.hull) +
               number("sails", t.sails) + number("crew", t.crew) + number("cannons", t.cannons) +
               number("cannonsMax", t.cannonsMax) + number("charge", t.charge) + number("nation", t.nation) +
               number("sail", t.sail) + decimal("speed", t.speed) + decimal("distance", t.distance) +
               text("captain", t.captain) + text("cannonType", t.cannonType) + text("portrait", t.portrait);
        static constexpr const char *kSkills[] = {"fencing", "cannons", "accuracy", "navigation", "boarding"};
        std::string skills;
        for (int i = 0; i < 5; ++i)
            skills += number(kSkills[i], t.skills[i]);
        if (!skills.empty())
            out += ",\"skills\":{" + skills.substr(1) + "}";
        return out + "}}";
    }

  private:
    bool on_ = false, known_ = false, covering_ = false;
    uint64_t generation_ = 0, revision_ = 0, answered_ = 0;
    SpyglassTarget target_;
};

inline SpyglassState spyglass;

} // namespace storm::android
