// The world-map HUD: fleet, compass (a tap shows the details beside it), helm slider, sail
// lever, camera, menu and the map's commands (go to sea, enter a port or a ship, attack, the
// storm). Layout scene `world-map` of hud-layout.json.
//
// Input goes back through the engine's entry points:
//   hold:<gameplayRevision>\t<control>\t<0|1>    WMapShipTurnLeft/Right (helm), WMapShipSailUp/Down (lever), WMapCameraSwitch
//   worldmap:<revision>\t<activity>\tack\t<mask>  which of the engine's instruments, fleet and command icon are replaced
//   worldmap:<revision>\t<activity>\tactivate\t<command id>
//   shortcut:<mainmenu|shipscreen>
#include "hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace storm::ui::hud
{

namespace
{

constexpr double kIdle = 3600.0;
constexpr double kTapSeconds = 1.5;      // a tapped command waits this long for the engine to act
constexpr int kHelmDead = 22;            // of ±100: the helm turns the ship beyond this
constexpr double kPi = 3.14159265358979323846;

using screens::StringKey;

enum Cover : unsigned
{
    kInstruments = 1,
    kShips = 2,
    kCommands = 4,
};

struct Slot
{
    const char *element, *icon, *variant;
};

// Where each command sits (kit HUD-ANCHORS, V2-WorldMap-Encounter): going on (to sea, a port,
// a ship) in the corner, the fight beside it, the storm further in.
Slot SlotOf(const std::string &id)
{
    if (id == "EnterToAttack" || id == "EnterToEnemy")
        return {"attack", "swords", "combat"};
    if (id == "EnterToStorm")
        return {"storm", "wind", "normal"};
    if (id == "EnterToIsland")
        return {"context", "port", "primary"};
    if (id == "EnterToShip")
        return {"context", "ship", "primary"};
    return {"context", "sea-exit", "primary"};
}

const char *const kPoints[] = {
    StringKey("hud.map.dir.n"), StringKey("hud.map.dir.ne"), StringKey("hud.map.dir.e"), StringKey("hud.map.dir.se"),
    StringKey("hud.map.dir.s"), StringKey("hud.map.dir.sw"), StringKey("hud.map.dir.w"), StringKey("hud.map.dir.nw"),
};

// The engine's flag index (WorldMap nationFlagIndex).
const char *const kFlags[] = {StringKey("hud.map.flag.holland"), StringKey("hud.map.flag.england"), StringKey("hud.map.flag.france"),
                              StringKey("hud.map.flag.spain"), StringKey("hud.map.flag.pirates")};

int Degrees(double radians)
{
    const long whole = std::lround(radians * 180.0 / kPi);
    return static_cast<int>(((whole % 360) + 360) % 360);
}

std::string Clock(double hour)
{
    const int minutes = static_cast<int>(std::floor(hour * 60.0)) % 1440;
    char text[16];
    std::snprintf(text, sizeof(text), "%02d:%02d", minutes / 60, minutes % 60);
    return text;
}

std::string Percent(double share)
{
    return std::to_string(static_cast<int>(std::lround(std::clamp(share, 0.0, 100.0)))) + "%";
}

class WorldMapScene final : public Scene
{
  public:
    explicit WorldMapScene(Hud &hud) : hud_(hud)
    {
    }

    void Build(const States &states, View &view, Input &input) override
    {
        const Gameplay &gameplay = states.gameplay;
        const WorldMap *map = Current(states);
        view.layout = "world-map";
        view.log = true;
        const Json lines = hud_.LogLines();
        if (!lines.empty())
            view.elements["log"] = Json{{"lines", lines}};

        view.elements["pause"] = Json::object();
        input.On("pause", "click", [this](const Event &) { hud_.Send("shortcut", "mainmenu"); });
        if (gameplay.Has("WMapCameraSwitch"))
        {
            view.elements["camera"] = Json::object();
            input.On("camera", "down", [this](const Event &) { Hold("WMapCameraSwitch", true); });
            input.On("camera", "up", [this](const Event &) { Hold("WMapCameraSwitch", false); });
            input.Ignore("camera", "click");
        }
        if (gameplay.Has("WMapShipTurnLeft") || gameplay.Has("WMapShipTurnRight"))
        {
            view.elements["helm"] = Json::object();
            input.On("helm", "change", [this](const Event &event) { Helm(event.value); });
        }
        else
            hud_.Error("world-map HUD: the engine binds no helm (WMapShipTurnLeft / WMapShipTurnRight); no helm");
        if (gameplay.Has("WMapShipSailUp") || gameplay.Has("WMapShipSailDown"))
        {
            // The lever shows how fast the ship sails; pushed up or down it holds SailUp / SailDown.
            Json lever = Json::object();
            if (map && map->speed >= 0.0)
                lever["value"] = static_cast<int>(std::lround(map->speed * 100.0));
            view.elements["sail"] = std::move(lever);
            input.On("sail", "hold", [this](const Event &event) { Sails(event.value); });
        }
        if (!map)
        {
            details_ = false;
            return;
        }
        BuildFleet(*map, view, input);
        BuildCompass(*map, view, input);
        BuildCommands(*map, view, input);
    }

    void Hidden(const States &states) override
    {
        Uncover(states);
    }

    double Tick(const States &states, double now) override
    {
        double next = kIdle;
        for (auto &hold : holds_)
        {
            if (!hold.second.down)
                continue;
            if (!hud_.Interactive() || !states.gameplay.Has(hold.first))
            {
                hold.second.down = false;
                continue;
            }
        }
        if (!tapped_.empty())
        {
            if (now >= tapped_until_)
            {
                tapped_.clear();
                hud_.Rebuild();
            }
            else
                next = std::min(next, tapped_until_ - now);
        }
        return next;
    }

    void Drawn(const States &states) override
    {
        const View &shown = hud_.Shown();
        const WorldMap *map = hud_.Interactive() ? Current(states) : nullptr;
        unsigned mask = 0;
        if (map)
        {
            mask |= shown.elements.contains("compass") ? kInstruments : 0;
            mask |= shown.elements.contains("fleet") ? kShips : 0;
            bool all = map->commands_ready && !map->commands.empty();
            for (const WorldMap::Command &command : map->commands)
                all = all && shown.elements.contains(SlotOf(command.id).element);
            mask |= all ? kCommands : 0;
            mask &= map->mask;
        }
        Cover(states.world_map, mask);
    }

    void Released(const States &states) override
    {
        for (auto &hold : holds_)
            if (hold.second.down)
            {
                hold.second.down = false;
                SendHold(hold.first, hold.second, states.gameplay);
            }
        turn_ = 0;
    }

    void Suspended(const States &states) override
    {
        Uncover(states);
        tapped_.clear();
    }

  private:
    // The frame of the gameplay the HUD shows.
    static const WorldMap *Current(const States &states)
    {
        const WorldMap &map = states.world_map;
        return map.known && map.available && map.gameplay_revision == states.gameplay.revision ? &map : nullptr;
    }

    static std::string Owner(const WorldMap &map)
    {
        return std::to_string(map.revision) + "\t" + std::to_string(map.activity);
    }

    // The squadron column, as the map's own ship signs: the flagship first, each ship's picture
    // ringed by its hull, crew and sails (sea.cpp draws the same cells). A tap opens the ship
    // screen on that ship.
    void BuildFleet(const WorldMap &map, View &view, Input &input)
    {
        if (!map.ships_visible || map.fleet.empty())
            return;
        Json ships = Json::array();
        for (const WorldMap::Vessel &vessel : map.fleet)
        {
            if (vessel.picture.texture.empty())
                hud_.Log("world-map HUD: the engine gives no picture for the ship " + vessel.name + " (" + vessel.type + "): its placeholder stays");
            ships.push_back(MemberCell(vessel.name, vessel.picture,
                                       {{"health", vessel.hull < 0.0 ? -1.0 : vessel.hull / 100.0},
                                        {"crew", vessel.crew},
                                        {"sails", vessel.sails < 0.0 ? -1.0 : vessel.sails / 100.0}},
                                       true));
        }
        view.elements["fleet"] = Json{{"ships", ships}};
        input.On("fleet", "ship", [this](const Event &event) {
            const WorldMap *now = Current(hud_.Current());
            const int index = std::atoi(event.value.c_str());
            if (!now || index < 0 || static_cast<size_t>(index) >= now->fleet.size())
                return;
            hud_.RequestSubject({"ship", -1, index});
            hud_.Send("shortcut", "shipscreen");
        });
    }

    void BuildCompass(const WorldMap &map, View &view, Input &input)
    {
        if (!map.instruments || map.hour < 0.0 || !map.angles || map.wind_force < 0.0)
        {
            details_ = false;
            return;
        }
        const int course = Degrees(map.heading), from = Degrees(map.wind_angle + kPi);
        Json compass = {{"date", map.date},
                        {"time", Clock(map.hour)},
                        {"course", course},
                        {"windFrom", from},
                        {"wind", static_cast<int>(std::lround(map.wind_force * 100.0))},
                        {"expanded", details_}};
        // The sun rises in the east at six and sets in the west at eighteen.
        compass["sun"] = map.hour >= 6.0 && map.hour <= 18.0 ? std::to_string(static_cast<int>(std::lround(90.0 + (map.hour - 6.0) * 15.0))) : "";
        if (map.food_days >= 0)
        {
            compass["days"] = std::to_string(map.food_days);
            compass["daysTone"] = map.food_days <= 3 ? "danger" : "normal";
        }
        view.elements["compass"] = std::move(compass);
        input.On("compass", "toggle", [this](const Event &event) {
            details_ = event.value == "1";
            hud_.Rebuild();
        });
        if (!details_)
            return;
        // V2-WorldMap-Details: the readings the compass draws as pictures, in words.
        const std::string days = " " + hud_.Text("common.days_short");
        Json items = Json::array();
        items.push_back(Json{{"k", hud_.Text("hud.map.course")}, {"v", std::to_string(course) + "°"}});
        std::string wind = hud_.Text("hud.map.windFrom");
        const size_t at = wind.find("{dir}");
        if (at != std::string::npos)
            wind.replace(at, 5, hud_.Text(kPoints[((from + 22) % 360) / 45]));
        items.push_back(Json{{"k", hud_.Text("hud.map.wind")}, {"v", wind + " · " + Percent(map.wind_force * 100.0)}});
        if (map.food_days >= 0)
            items.push_back(Json{{"k", hud_.Text("hud.map.food")}, {"v", std::to_string(map.food_days) + days}, {"tone", map.food_days <= 3 ? "danger" : ""}});
        if (map.rum_days >= 0)
            items.push_back(Json{{"k", hud_.Text("hud.map.rum")}, {"v", std::to_string(map.rum_days) + days}});
        if (map.morale >= 0.0)
            items.push_back(Json{{"k", hud_.Text("hud.map.morale")}, {"v", Percent(map.morale)}});
        if (map.flag >= 0 && map.flag < static_cast<int>(std::size(kFlags)))
            items.push_back(Json{{"k", hud_.Text("hud.map.flag")}, {"v", hud_.Text(kFlags[map.flag])}});
        // The engine writes dashes while the fleet has no sextant.
        const bool known = !map.coordinates.empty() && map.coordinates.find("--") == std::string::npos;
        items.push_back(Json{{"k", hud_.Text("hud.map.coordinates")}, {"v", known ? map.coordinates : hud_.Text("hud.map.noSextant")}});
        view.elements["details"] = Json::object();
        view.elements["detail-rows"] = Json{{"items", items}};
    }

    void BuildCommands(const WorldMap &map, View &view, Input &input)
    {
        if (!map.commands_ready)
            return;
        for (const WorldMap::Command &command : map.commands)
        {
            const Slot slot = SlotOf(command.id);
            view.elements[slot.element] = Json{{"icon", slot.icon}, {"label", command.label}, {"variant", slot.variant}, {"pressed", tapped_ == command.id}};
            input.On(slot.element, "click", [this, id = command.id](const Event &) { Activate(id); });
        }
    }

    // Exactly the command that is drawn and covered, once per offer.
    void Activate(const std::string &id)
    {
        const WorldMap *map = Current(hud_.Current());
        if (!hud_.Interactive() || !map || !map->commands_ready || !tapped_.empty() || covered_owner_ != Owner(*map) || !(covered_ & kCommands))
            return;
        tapped_ = id;
        tapped_until_ = hud_.Now() + kTapSeconds;
        Released(hud_.Current());
        hud_.Send("worldmap", Owner(*map) + "\tactivate\t" + id);
        hud_.Rebuild();
    }

    void Cover(const WorldMap &map, unsigned mask)
    {
        if (!map.known || !map.available)
        {
            covered_ = 0; // the engine dropped every acknowledgement with the map
            covered_owner_.clear();
            return;
        }
        // Every revision is answered once, also with nothing covered: the engine keeps the word
        // until the revision changes and its own parts off until then (android_hud_presenter.hpp).
        const std::string owner = Owner(map);
        if (owner == covered_owner_ && mask == covered_)
            return;
        covered_ = mask;
        covered_owner_ = owner;
        hud_.Send("worldmap", owner + "\tack\t" + std::to_string(mask), "worldmap-ack");
        if (owner != tapped_owner_)
        {
            tapped_owner_ = owner;
            if (!tapped_.empty()) // the engine acted: a new revision
            {
                tapped_.clear();
                hud_.Rebuild();
            }
        }
    }

    void Uncover(const States &states)
    {
        Cover(states.world_map, 0);
    }

    struct Held
    {
        bool down = false; // held until the pointer's up or cancel: the engine keeps it till then
    };

    void Helm(const std::string &value)
    {
        const int position = static_cast<int>(std::clamp(std::strtod(value.c_str(), nullptr), -100.0, 100.0));
        const int turn = position <= -kHelmDead ? -1 : position >= kHelmDead ? 1 : 0;
        if (turn == turn_)
            return;
        turn_ = turn;
        Hold(turn < 0 ? "WMapShipTurnRight" : "WMapShipTurnLeft", false);
        Hold(turn < 0 ? "WMapShipTurnLeft" : "WMapShipTurnRight", turn != 0);
    }

    void Sails(const std::string &dir)
    {
        Hold(dir == "up" ? "WMapShipSailDown" : "WMapShipSailUp", false);
        if (dir == "up" || dir == "down")
            Hold(dir == "up" ? "WMapShipSailUp" : "WMapShipSailDown", true);
        else
            Hold("WMapShipSailDown", false);
    }

    void Hold(const std::string &control, bool down)
    {
        const Gameplay &gameplay = hud_.Current().gameplay;
        Held &held = holds_[control];
        if (down && (!hud_.Interactive() || !gameplay.Has(control)))
            down = false;
        if (held.down == down)
            return;
        held.down = down;
        SendHold(control, held, gameplay);
    }

    void SendHold(const std::string &control, Held &held, const Gameplay &gameplay)
    {
        hud_.Send("hold", std::to_string(gameplay.revision) + "\t" + control + "\t" + (held.down ? "1" : "0"), "hold-" + control);
    }

    Hud &hud_;
    std::map<std::string, Held> holds_;
    int turn_ = 0;
    bool details_ = false;
    unsigned covered_ = 0;
    std::string covered_owner_, tapped_owner_;
    std::string tapped_;
    double tapped_until_ = 0.0;
};

} // namespace

std::unique_ptr<Scene> CreateWorldMapScene(Hud &hud)
{
    return std::make_unique<WorldMapScene>(hud);
}

} // namespace storm::ui::hud
