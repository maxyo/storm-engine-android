// The sea HUD: sailing and naval battle, the ammunition fan and the orders panel.
//
// Layout scenes `sea` (sailing) and `sea-battle` (naval) of hud-layout.json. Input goes back
// through the engine's entry points:
//   hold:<gameplayRevision>\t<control>\t<0|1>         helm (Ship_TurnLeft / Ship_TurnRight), spyglass (TelescopeIn)
//   sails:<gameplayRevision>\t<0|1|2>                 the sail lever
//   gameplay:<gameplayRevision>\t<control>            Ship_Fire, Sea_CameraSwitch, hk_charge1…4
//   seaquick:1\t<activity>\t<revision>\t<ack|activate>\t<0|1|action>
//   orders:1\t<sceneEpoch>\t<revision>\t<activityGeneration>\t<verb>\t<id>
//   shiphud:<gameplayRevision>\t<mask>\t<hudRevision>   which of the engine's indicators our elements replace
//   navigator:<gameplayRevision>\t<hudRevision>\t<navigatorGeneration>\t<prepare …|visible <id>|in|out|clear>
//   shortcut:<mainmenu|character>
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
constexpr double kHoldHeartbeat = 0.1;   // the engine lets go of a held control after 350 ms of silence
constexpr double kLeaseHeartbeat = 0.25; // … of the navigator aperture after 750 ms (it keeps the place 2 s), of the quick action coverage after 1.5 s
constexpr double kChoiceSeconds = 1.5;   // a tapped order waits this long for the engine to select it
constexpr double kSailSeconds = 1.5;     // the lever shows the position asked for until the sails follow
constexpr double kFanSeconds = 6.0;      // an untouched ammunition fan folds by itself
constexpr int kHelmDead = 22;            // of ±100: the helm turns the ship beyond this

using screens::StringKey;

// The engine's indicator groups (android_ship_telemetry.hpp, ShipHudMask).
enum Indicator : unsigned
{
    kHull = 1,
    kCrew = 2,
    kAmmoCount = 4,
    kSailPosition = 8,
    kSailIntegrity = 16,
    kFleet = 32,
    kSpeed = 64,
    kPowder = 128,
    kDateTime = 256,
    kLocation = 512,
    kWind = 1024,
};

struct Named
{
    const char *key, *icon, *label;
};

// Ammunition kinds in the engine's order: hk_charge<index + 1> loads kind `index`.
const Named kAmmo[] = {
    {"balls", "ammo", StringKey("hud.sea.ammo.balls")},
    {"grapes", "grape", StringKey("hud.sea.ammo.grapes")},
    {"knippels", "chain", StringKey("hud.sea.ammo.knippels")},
    {"bombs", "bomb", StringKey("hud.sea.ammo.bombs")},
};

// The quick action; `label` holds the button look here.
const Named kQuick[] = {
    {"Map", "map", "primary"}, {"Moor", "moor", "primary"}, {"Board", "grapple", "combat"},
    {"LandTroops", "boat", "combat"}, {"Reload", "exchange", "primary"},
};

// Orders by the script command behind them (lower case).
const Named kOrders[] = {
    {"bi_moor", "moor", ""},          {"bi_board", "grapple", ""},     {"bi_abordage", "grapple", ""},   {"bi_instantboarding", "grapple", ""},
    {"bi_landtroops", "boat", ""},    {"bi_map", "map", ""},           {"bi_sailto", "sail-to", ""},     {"bi_saildir", "wheel", ""},
    {"bi_sailaway", "sail-away", ""}, {"bi_heavetodrift", "drift", ""}, {"bi_attack", "swords", ""},     {"bi_defend", "shield", ""},
    {"bi_reload", "exchange", ""},    {"bi_charge", "ammo", ""},       {"bi_speed", "speed", ""},        {"bi_ccommand", "follow", ""},
    {"bi_ability", "energy", ""},     {"bi_speak", "talk", ""},        {"bi_sneak", "sneak", ""},        {"bi_immediatedeath", "skull", ""},
    {"bi_cabin", "cabin", ""},        {"bi_boat", "boat", ""},         {"bi_brander", "brander", ""},    {"bi_bomb", "bomb", ""},
};

// Ship tokens by a word of the engine's ship model name (lower case).
const Named kShips[] = {
    {"lugger", "ship-lugger", ""},   {"sloop", "ship-sloop", ""},         {"schooner", "ship-schooner", ""}, {"barkentine", "ship-barkentine", ""},
    {"brigantine", "ship-brigantine", ""}, {"brig", "ship-brig", ""},     {"caravel", "ship-caravel", ""},   {"fleut", "ship-fluyt", ""},
    {"fluyt", "ship-fluyt", ""},     {"galeon", "ship-galleon", ""},      {"galleon", "ship-galleon", ""},   {"frigate", "ship-frigate", ""},
    {"ketch", "ship-ketch", ""},     {"tartane", "ship-tartane", ""},     {"xebec", "ship-xebec", ""},       {"manowar", "ship-manowar", ""},
    {"lineship", "ship-manowar", ""},
};

int Percent(double share)
{
    return static_cast<int>(std::lround(std::clamp(share, 0.0, 1.0) * 100.0));
}

// Knots as the minimap reads them: whole numbers (V2-Sea-Sailing).
std::string Knots(double value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f", value);
    return text;
}

std::string Count(double value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f", std::floor(value));
    return text;
}

} // namespace

const char *ShipIcon(const std::string &type)
{
    const std::string name = Lower(type);
    for (const Named &ship : kShips)
        if (name.find(ship.key) != std::string::npos)
            return ship.icon;
    return "ship"; // a model without a token of its own: the generic ship
}

namespace
{

// The engine's date line ends with the time of day ("12 мая 1665 11:42"): the minimap shows
// the time only; a line without a time is shown as it is.
std::string TimeOf(const std::string &date_time)
{
    const size_t colon = date_time.rfind(':');
    if (colon == std::string::npos || colon == 0)
        return date_time;
    size_t from = colon, to = colon + 1;
    while (from > 0 && date_time[from - 1] >= '0' && date_time[from - 1] <= '9')
        --from;
    while (to < date_time.size() && date_time[to] >= '0' && date_time[to] <= '9')
        ++to;
    return from == colon || to == colon + 1 ? date_time : date_time.substr(from, to - from);
}

class SeaScene final : public Scene
{
  public:
    explicit SeaScene(Hud &hud) : hud_(hud)
    {
    }

    void Build(const States &states, View &view, Input &input) override
    {
        const Gameplay &gameplay = states.gameplay;
        if (gameplay.mode == "sailing" || gameplay.mode == "naval")
            layout_ = gameplay.mode == "naval" ? "sea-battle" : "sea";
        view.layout = layout_; // an open orders panel blocks gameplay (mode "unknown"): the last layout stays under it
        const Ship *ship = Current(states);

        if (ship)
            BuildVitals(*ship, view);

        const SeaOrders &orders = states.sea_orders;
        if (orders.known && orders.active)
        {
            fan_open_ = false;
            if (orders.complete)
            {
                if (ship)
                    BuildMinimap(*ship, view, input); // the radar stays beside the panel, as in V2-Sea-Orders
                BuildOrders(states, view, input);
                return;
            }
            // The engine marked its menu as something only it can draw. It stays on the
            // game's own surface (no coverage is acknowledged); the HUD keeps out of it.
            hud_.Error("sea orders: the engine's menu is not drawable here (stage " + orders.stage + "); the game's own menu stays");
            return;
        }
        choice_.clear();

        view.log = true;
        const Json lines = hud_.LogLines();
        if (!lines.empty())
        {
            view.elements["log"] = Json{{"lines", lines}};
            if (band_left_ > 0)
                view.elements["log"]["safeLeft"] = band_left_;
        }

        view.elements["hero"] = Json::object();
        input.On("hero", "click", [this](const Event &) { hud_.Send("shortcut", "character"); });
        view.elements["pause"] = Json::object();
        input.On("pause", "click", [this](const Event &) { hud_.Send("shortcut", "mainmenu"); });
        if (orders.known && orders.available)
        {
            view.elements["orders"] = Json::object();
            input.On("orders", "click", [this](const Event &) { Order("open", {}); });
        }

        if (ship)
        {
            BuildFleet(*ship, orders, view, input);
            BuildMinimap(*ship, view, input);
            BuildSails(*ship, view, input);
        }
        if (gameplay.Has("Ship_TurnLeft") || gameplay.Has("Ship_TurnRight"))
        {
            view.elements["helm"] = Json::object();
            input.On("helm", "change", [this](const Event &event) { Helm(event.value); });
        }
        else
            hud_.Error("sea HUD: the engine binds no helm (Ship_TurnLeft / Ship_TurnRight) in mode " + gameplay.mode + "; no helm");

        BuildGuns(states, ship, view, input);
        if (fan_open_)
            return; // the fan takes the place of the buttons around the ammunition
        BuildQuick(states.sea_quick, view, input);
        if (gameplay.Has("Sea_CameraSwitch"))
        {
            view.elements["camera"] = Json::object();
            input.On("camera", "click", [this](const Event &) { Control("Sea_CameraSwitch"); });
        }
        if (gameplay.Has("TelescopeIn")) // bound on deck only: the spyglass is held to the eye
        {
            view.elements["spyglass"] = Json::object();
            input.On("spyglass", "down", [this](const Event &) { Hold("TelescopeIn", true); });
            input.On("spyglass", "up", [this](const Event &) { Hold("TelescopeIn", false); });
            input.Ignore("spyglass", "click");
        }
    }

    void Hidden(const States &states) override
    {
        fan_open_ = false;
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
                hold.second.down = false; // its gameplay is gone: the engine has let go already
                continue;
            }
            if (now - hold.second.sent >= kHoldHeartbeat)
                SendHold(hold.first, hold.second, states.gameplay);
            next = std::min(next, std::max(0.0, hold.second.sent + kHoldHeartbeat - now));
        }
        const auto expire = [&](bool active, double until, const std::function<void()> &end) {
            if (!active)
                return;
            if (now >= until)
            {
                end();
                hud_.Rebuild();
            }
            else
                next = std::min(next, until - now);
        };
        expire(!choice_.empty(), choice_until_, [this] { choice_.clear(); });
        expire(sail_target_ >= 0, sail_until_, [this] { sail_target_ = -1; });
        expire(fan_open_, fan_until_, [this] { fan_open_ = false; });
        if (quick_covered_ || navigator_prepared_)
            next = std::min(next, kLeaseHeartbeat); // Drawn renews the leases
        return next;
    }

    void Drawn(const States &states) override
    {
        const bool live = hud_.Interactive();
        const View &shown = hud_.Shown();
        const Ship *ship = live ? Current(states) : nullptr;
        FollowFleet(shown);
        CoverShip(states, ship, shown);
        CoverNavigator(ship, shown);
        // The ammunition fan takes the quick action's place for a few seconds: the offer stays
        // the HUD's, so the engine's own icon does not flash up over the top buttons meanwhile.
        // Under the orders panel the offer waits for the panel to close; it stays the HUD's too.
        CoverQuick(states.sea_quick, live && (shown.elements.contains("context") || fan_open_ || shown.sheet == "s-hud-orders"));
        CoverOrders(states.sea_orders, live && shown.modal);
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
        const SeaOrders &orders = states.sea_orders;
        Uncover(states);
        if (orders.known && orders.active)
            hud_.Send("orders", Owner(orders) + "\tclose\t");
        choice_.clear();
        fan_open_ = false;
    }

    bool Holds(const States &states) const override
    {
        return states.sea_orders.known && states.sea_orders.active; // the open panel blocks gameplay
    }

    bool Back(const States &states) override
    {
        const SeaOrders &orders = states.sea_orders;
        if (!orders.known || !orders.active || !orders.complete)
            return false;
        Order(orders.stage == "commands" ? "close" : "back", {});
        return true;
    }

  private:
    // The telemetry of the gameplay the HUD shows; an older or newer scene's ship is not ours.
    static const Ship *Current(const States &states)
    {
        const Ship &ship = states.ship;
        return ship.known && ship.gameplay_revision == states.gameplay.revision ? &ship : nullptr;
    }

    static std::string Owner(const SeaOrders &orders)
    {
        return "1\t" + std::to_string(orders.scene) + "\t" + std::to_string(orders.revision) + "\t" + std::to_string(orders.activity);
    }

    static std::string Owner(const SeaQuick &quick)
    {
        return "1\t" + std::to_string(quick.activity) + "\t" + std::to_string(quick.revision);
    }

    static std::string Owner(const Ship &ship)
    {
        return std::to_string(ship.gameplay_revision) + "\t" + std::to_string(ship.hud_revision) + "\t" + std::to_string(ship.navigator_generation);
    }

    // --- bars, fleet, minimap, sails ---

    static void BuildVitals(const Ship &ship, View &view)
    {
        const Ship::Vessel &own = ship.own;
        if (own.hull < 0.0 && own.crew < 0.0 && own.sails < 0.0)
            return;
        view.elements["vitals"] = Json::object();
        if (own.hull >= 0.0)
            view.elements["hull"] = Json{{"value", Percent(own.hull)}};
        if (own.crew >= 0.0)
            view.elements["crew"] = Json{{"value", Percent(own.crew)}};
        if (own.sails >= 0.0)
            view.elements["sails"] = Json{{"value", Percent(own.sails)}};
    }

    void BuildFleet(const Ship &ship, const SeaOrders &orders, View &view, Input &input)
    {
        if (ship.fleet.empty())
            return;
        Json ships = Json::array();
        for (const Ship::Vessel &vessel : ship.fleet)
        {
            Json token = {{"icon", ShipIcon(vessel.type)}, {"label", vessel.name}};
            if (vessel.hull >= 0.0)
                token["hp"] = Percent(vessel.hull);
            ships.push_back(std::move(token));
        }
        view.elements["fleet"] = Json{{"ships", ships}};
        // A tap on a companion opens the orders for it.
        input.On("fleet", "ship", [this](const Event &event) {
            const States &states = hud_.Current();
            const Ship *current = Current(states);
            const size_t index = static_cast<size_t>(std::max(0, std::atoi(event.value.c_str())));
            if (!current || index >= current->fleet.size())
                return;
            for (const SeaOrders::Recipient &recipient : states.sea_orders.recipients)
                if (recipient.character == current->fleet[index].character && recipient.entity == current->fleet[index].entity)
                {
                    Order("open", recipient.id);
                    return;
                }
            hud_.Log("sea HUD: the engine offers no orders for " + current->fleet[index].name);
        });
        (void)orders;
    }

    void BuildMinimap(const Ship &ship, View &view, Input &input)
    {
        if (!ship.captions && !ship.navigator)
            return;
        Json minimap = {{"native", ship.navigator}};
        if (!ship.captions) // the engine hides its text readouts now (a command menu): so do we, the radar stays
        {
            minimap["readout"] = false;
            view.elements["minimap"] = std::move(minimap);
            input.On("minimap", "zoom", [this](const Event &) { Zoom(); });
            return;
        }
        if (!ship.location.empty())
            minimap["place"] = ship.location;
        if (!ship.date_time.empty())
            minimap["time"] = TimeOf(ship.date_time);
        if (ship.speed >= 0.0 && ship.wind >= 0.0)
        {
            minimap["speed"] = Knots(ship.speed);
            minimap["wind"] = Knots(ship.wind);
        }
        else
            minimap["readout"] = false;
        view.elements["minimap"] = std::move(minimap);
        input.On("minimap", "zoom", [this](const Event &) { Zoom(); });
    }

    void BuildSails(const Ship &ship, View &view, Input &input)
    {
        if (!ship.can_set_sails || ship.sail_state < 0.0)
            return;
        const int position = sail_target_ >= 0 ? sail_target_ : static_cast<int>(std::lround(ship.sail_state * 2.0));
        view.elements["sail"] = Json{{"value", position}};
        input.On("sail", "change", [this](const Event &event) { Sails(event.value); });
    }

    // On a short screen the fleet wraps into further columns towards the middle of the top
    // edge: the band of event lines then begins to the right of the last column.
    void FollowFleet(const View &shown)
    {
        int left = 0;
        Placement first, last;
        if (shown.elements.contains("fleet") && hud_.Measure("fleet", ".item.ship:first-child", first) &&
            hud_.Measure("fleet", ".item.ship:last-child", last) && last.x > first.x)
            left = static_cast<int>(std::ceil((last.x + last.width - last.layer_x) / last.density)) + 8;
        if (left == band_left_)
            return;
        band_left_ = left;
        if (shown.elements.contains("log"))
            hud_.Rebuild();
    }

    // --- guns ---

    void BuildGuns(const States &states, const Ship *ship, View &view, Input &input)
    {
        const Gameplay &gameplay = states.gameplay;
        if (gameplay.Has("Ship_Fire"))
        {
            Json fire = Json::object();
            if (ship && ship->cannon_charge >= 0.0 && ship->cannon_charge < 1.0)
                fire = Json{{"fill", Percent(ship->cannon_charge)}, {"fillTone", "reload"}, {"reloading", true}};
            view.elements["fire"] = std::move(fire);
            input.On("fire", "click", [this](const Event &) { Control("Ship_Fire"); });
        }
        int loaded = -1, others = 0;
        for (size_t i = 0; i < std::size(kAmmo); ++i)
        {
            if (ship && ship->ammo == kAmmo[i].key)
                loaded = static_cast<int>(i);
            else if (gameplay.Has("hk_charge" + std::to_string(i + 1)))
                ++others;
        }
        if (loaded < 0 || others == 0)
            fan_open_ = false; // nothing confirmed to choose from
        if (loaded < 0)
            return;
        Json ammo = {{"icon", kAmmo[loaded].icon}, {"label", hud_.Text(kAmmo[loaded].label)}, {"variant", fan_open_ ? "active" : "normal"}};
        if (ship->ammo_count >= 0.0)
        {
            ammo["badge"] = Count(ship->ammo_count);
            // No powder fires nothing either: the badge warns for both, the engine's powder line steps aside.
            ammo["badgeTone"] = ship->ammo_low || ship->powder_out ? "danger" : "gold";
        }
        view.elements["ammo"] = std::move(ammo);
        if (others == 0)
        {
            input.Ignore("ammo", "click");
            return;
        }
        input.On("ammo", "click", [this](const Event &) {
            fan_open_ = !fan_open_;
            fan_until_ = hud_.Now() + kFanSeconds;
            hud_.Rebuild();
        });
        if (!fan_open_)
            return;
        // The other kinds fan out around the loaded one, in the engine's order.
        int slot = 0;
        for (size_t i = 0; i < std::size(kAmmo); ++i)
        {
            if (static_cast<int>(i) == loaded)
                continue;
            const std::string element = "ammo-" + std::to_string(++slot), control = "hk_charge" + std::to_string(i + 1);
            Json option = {{"icon", kAmmo[i].icon}, {"label", hud_.Text(kAmmo[i].label)}, {"disabled", !gameplay.Has(control)}};
            if (ship->ammo_counts[i] >= 0.0)
                option["badge"] = Count(ship->ammo_counts[i]);
            view.elements[element] = std::move(option);
            input.On(element, "click", [this, control](const Event &) {
                fan_open_ = false;
                Control(control);
                hud_.Rebuild();
            });
        }
    }

    // A tap control: the engine runs it once, in the gameplay revision it was drawn for.
    void Control(const std::string &control)
    {
        const Gameplay &gameplay = hud_.Current().gameplay;
        if (!hud_.Interactive() || !gameplay.Has(control))
            return;
        hud_.Send("gameplay", std::to_string(gameplay.revision) + "\t" + control);
    }

    // --- quick action ---

    void BuildQuick(const SeaQuick &quick, View &view, Input &input)
    {
        if (!quick.known || !quick.available)
            return;
        const Named *look = nullptr;
        for (const Named &entry : kQuick)
            if (quick.action == entry.key)
                look = &entry;
        if (!look) // the consumer admits the five actions of the table only
            return;
        view.elements["context"] = Json{{"icon", look->icon}, {"label", quick.label}, {"variant", look->label}};
        input.On("context", "click", [this](const Event &) {
            const SeaQuick &current = hud_.Current().sea_quick;
            // Exactly the action that is drawn and acknowledged, once: a second tap on the same
            // offer and a tap on an offer the engine has replaced since do nothing.
            if (!hud_.Interactive() || !current.known || !current.available || quick_covered_owner_ != Owner(current) || quick_tapped_ == Owner(current))
                return;
            quick_tapped_ = Owner(current);
            hud_.Send("seaquick", Owner(current) + "\tactivate\t" + current.action);
        });
    }

    void CoverQuick(const SeaQuick &quick, bool drawn)
    {
        const double now = hud_.Now();
        if (drawn && quick.known && quick.available)
        {
            const std::string owner = Owner(quick);
            if (quick_covered_ && owner == quick_covered_owner_ && now - quick_covered_at_ < kLeaseHeartbeat)
                return;
            quick_covered_ = true;
            quick_covered_owner_ = owner;
            quick_covered_at_ = now;
            hud_.Send("seaquick", owner + "\tack\t1", "seaquick-ack");
            return;
        }
        if (!quick_covered_)
            return;
        quick_covered_ = false;
        // An offer the engine has replaced needs no word: its coverage ended with it.
        if (quick.known && Owner(quick) == quick_covered_owner_)
            hud_.Send("seaquick", quick_covered_owner_ + "\tack\t0", "seaquick-ack");
        quick_covered_owner_.clear();
    }

    // --- helm, sails, held buttons ---

    struct Held
    {
        bool down = false;
        double sent = 0.0;
    };

    void Helm(const std::string &value)
    {
        const int position = static_cast<int>(std::clamp(std::strtod(value.c_str(), nullptr), -100.0, 100.0));
        const int turn = position <= -kHelmDead ? -1 : position >= kHelmDead ? 1 : 0;
        if (turn == turn_)
            return;
        turn_ = turn;
        // The side let go first, so that the engine never sees both held.
        Hold(turn < 0 ? "Ship_TurnRight" : "Ship_TurnLeft", false);
        Hold(turn < 0 ? "Ship_TurnLeft" : "Ship_TurnRight", turn != 0);
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
        held.sent = hud_.Now();
        hud_.Send("hold", std::to_string(gameplay.revision) + "\t" + control + "\t" + (held.down ? "1" : "0"), "hold-" + control);
    }

    void Sails(const std::string &value)
    {
        const States &states = hud_.Current();
        const Ship *ship = Current(states);
        const int position = static_cast<int>(std::lround(std::strtod(value.c_str(), nullptr)));
        if (!hud_.Interactive() || !ship || !ship->can_set_sails || position < 0 || position > 2)
            return;
        sail_target_ = position;
        sail_until_ = hud_.Now() + kSailSeconds;
        hud_.Rebuild();
        hud_.Send("sails", std::to_string(states.gameplay.revision) + "\t" + std::to_string(position), "sails");
    }

    // --- minimap ---

    // One tap = the next scale: closer until the engine has no closer one, then back out.
    void Zoom()
    {
        const Ship *ship = Current(hud_.Current());
        if (!hud_.Interactive() || !ship || !navigator_prepared_ || navigator_owner_ != Owner(*ship))
            return;
        if (zoom_in_ ? !ship->zoom_in : !ship->zoom_out)
            zoom_in_ = !zoom_in_;
        if (zoom_in_ ? !ship->zoom_in : !ship->zoom_out)
            return; // the engine has one scale only
        hud_.Send("navigator", navigator_owner_ + "\t" + (zoom_in_ ? "in" : "out"));
    }

    // The engine draws its radar into the disc of the minimap. It needs the place of the disc
    // (a lease of 750 ms) and, once it has drawn there, the word that our readouts
    // around it are on the screen: only then does it drop its own.
    void CoverNavigator(const Ship *ship, const View &shown)
    {
        Placement disc;
        if (!ship || !ship->navigator || !shown.elements.contains("minimap") || !hud_.Measure("minimap", ".mm-disc", disc))
        {
            ClearNavigator();
            return;
        }
        const std::string owner = Owner(*ship);
        const long width = std::lround(disc.screen_width), height = std::lround(disc.screen_height);
        const long radius = std::lround(std::min(disc.width, disc.height) * 0.5f);
        const std::string geometry = std::to_string(width) + "\t" + std::to_string(height) + "\t" + std::to_string(std::lround(disc.x + disc.width * 0.5f)) +
                                     "\t" + std::to_string(std::lround(disc.y + disc.height * 0.5f)) + "\t" + std::to_string(radius) + "\t" +
                                     std::to_string(std::lround(disc.density * 1000.0f)) + "\t" + (hud_.Current().gameplay.language == "russian" ? "1" : "0");
        const double now = hud_.Now();
        if (owner != navigator_owner_ || geometry != navigator_geometry_)
        {
            if (navigator_prepared_ && owner != navigator_owner_)
                navigator_prepared_ = false; // the old owner's lease ended with it
            navigator_owner_ = owner;
            navigator_geometry_ = geometry;
            ++navigator_layout_;
            navigator_sent_ = -1.0;
        }
        if (navigator_sent_ >= 0.0 && now - navigator_sent_ < kLeaseHeartbeat)
            return;
        navigator_sent_ = now;
        navigator_prepared_ = true;
        hud_.Send("navigator", owner + "\tprepare\t" + std::to_string(navigator_layout_) + "\t" + geometry, "navigator-prepare");
        if (ship->navigator_layout == navigator_layout_) // the engine has drawn into this very aperture
            hud_.Send("navigator", owner + "\tvisible\t" + std::to_string(navigator_layout_), "navigator-visible");
    }

    void ClearNavigator()
    {
        if (!navigator_prepared_)
            return;
        navigator_prepared_ = false;
        navigator_sent_ = -1.0;
        hud_.Send("navigator", navigator_owner_ + "\tclear", "navigator-prepare");
    }

    // --- the engine's own indicators ---

    // Tells the engine which of its indicators the drawn elements replace. It keeps the
    // word until the telemetry owner changes, so it is said once per change.
    void CoverShip(const States &states, const Ship *ship, const View &shown)
    {
        unsigned mask = 0;
        if (ship)
        {
            const auto has = [&shown](const char *id) { return shown.elements.contains(id); };
            const auto prop = [&shown](const char *id, const char *key) { return shown.elements.contains(id) && shown.elements[id].contains(key); };
            mask |= has("hull") ? kHull : 0;
            mask |= has("crew") ? kCrew : 0;
            mask |= has("sails") ? kSailIntegrity : 0;
            mask |= prop("ammo", "badge") ? kAmmoCount | kPowder : 0;
            mask |= has("sail") ? kSailPosition : 0;
            // the orders panel lists the fleet itself
            mask |= has("vitals") && (ship->fleet.empty() || has("fleet") || shown.sheet == "s-hud-orders") ? kFleet : 0;
            mask |= prop("minimap", "speed") ? kSpeed | kWind : 0;
            mask |= prop("minimap", "time") ? kDateTime : 0;
            mask |= prop("minimap", "place") ? kLocation : 0;
        }
        const Ship &current = states.ship;
        if (!current.known)
        {
            ship_covered_.clear(); // the engine dropped every acknowledgement with the scene
            return;
        }
        const std::string word = std::to_string(current.gameplay_revision) + "\t" + std::to_string(mask) + "\t" + std::to_string(current.hud_revision);
        if (word == ship_covered_ || (mask == 0 && ship_covered_.empty()))
            return;
        ship_covered_ = mask ? word : std::string();
        hud_.Send("shiphud", word, "shiphud");
    }

    // Nothing of the HUD is on the screen (any more): every native indicator comes back.
    void Uncover(const States &states)
    {
        CoverShip(states, nullptr, View());
        ClearNavigator();
        CoverQuick(states.sea_quick, false);
        CoverOrders(states.sea_orders, false);
    }

    // --- orders panel ---

    void Order(const std::string &verb, const std::string &id)
    {
        const SeaOrders &orders = hud_.Current().sea_orders;
        if (!hud_.Interactive() || !orders.known)
            return;
        if (verb == "open" ? !orders.available || orders.active : !orders.active)
            return;
        if ((verb == "back" && !orders.can_back) || (verb == "close" && !orders.can_close))
            return;
        if (verb == "open")
            Released(hud_.Current()); // the panel takes the screen: the helm is let go first
        hud_.Send("orders", Owner(orders) + "\t" + verb + "\t" + id);
    }

    // A tap acts at once. The engine wants the item selected first and activated second,
    // each against the menu revision it answers to.
    void Choose(const std::string &id)
    {
        const SeaOrders &orders = hud_.Current().sea_orders;
        const SeaOrders::Item *item = orders.Find(id);
        if (!item || !item->enabled || !choice_.empty())
            return;
        choice_ = id;
        choice_until_ = hud_.Now() + kChoiceSeconds;
        choice_revision_ = 0;
        choice_activated_ = false;
        hud_.Rebuild();
        Follow(orders);
    }

    void Follow(const SeaOrders &orders)
    {
        if (choice_.empty() || choice_revision_ == orders.revision)
            return;
        const SeaOrders::Item *item = orders.Find(choice_);
        if (!item || !item->enabled || choice_activated_)
        {
            choice_.clear(); // activated (the menu moved on) or gone: the tap is over
            hud_.Rebuild();
            return;
        }
        choice_revision_ = orders.revision;
        choice_activated_ = orders.selected == choice_;
        Order(choice_activated_ ? "activateItem" : "selectItem", choice_);
    }

    const char *ItemIcon(const SeaOrders::Item &item)
    {
        if (item.kind == "ship")
            return "ship";
        if (item.kind == "fort")
            return "fort";
        if (item.kind == "land" || item.kind == "town")
            return "port";
        if (item.kind == "charge")
        {
            // The engine names a charge by its goods ("balls"): the ammunition icons.
            for (const Named &ammo : kAmmo)
                if (Lower(item.source).find(ammo.key) != std::string::npos)
                    return ammo.icon;
            return "ammo";
        }
        const std::string source = Lower(item.source);
        for (const Named &order : kOrders)
            if (source == order.key)
                return order.icon;
        // Documented fallback: a command this table lacks keeps its row, with the generic icon.
        hud_.Log("sea orders: command '" + item.source + "' (" + item.kind + ") has no icon of its own");
        return item.kind == "ability" ? "energy" : "orders";
    }

    void BuildOrders(const States &states, View &view, Input &input)
    {
        const SeaOrders &orders = states.sea_orders;
        const Ship *ship = Current(states);
        const bool root = orders.stage == "commands";
        Json recipients = Json::array();
        for (const SeaOrders::Recipient &recipient : orders.recipients)
        {
            const Ship::Vessel *vessel = nullptr;
            if (ship && recipient.character == ship->own.character)
                vessel = &ship->own;
            for (size_t i = 0; ship && i < ship->fleet.size(); ++i)
                if (ship->fleet[i].character == recipient.character)
                    vessel = &ship->fleet[i];
            const std::string element = "recipient-" + recipient.id;
            const bool selected = recipient.id == orders.recipient;
            Json row = {{"id", element},
                        {"title", recipient.name},
                        {"meta", recipient.main ? hud_.Text("hud.orders.flagship") : std::string()},
                        {"image", Json{{"icon", vessel ? ShipIcon(vessel->type) : "ship"}, {"enamel", "teal"}}},
                        {"value", vessel && vessel->hull >= 0.0 ? std::to_string(Percent(vessel->hull)) + "%" : std::string()},
                        {"selected", selected}};
            if (selected && vessel && !recipient.main && vessel->hull >= 0.0 && vessel->crew >= 0.0 && vessel->sails >= 0.0)
                row["bars"] = Json{{"hull", Percent(vessel->hull)}, {"crew", Percent(vessel->crew)}, {"sails", Percent(vessel->sails)}};
            recipients.push_back(std::move(row));
            if (root) // another ship is chosen from the first stage only, as in the engine's menu
                input.On(element, "click", [this, id = recipient.id](const Event &) {
                    if (hud_.Current().sea_orders.recipient != id)
                        Order("selectRecipient", id);
                });
            else
                input.Ignore(element, "click");
        }
        Json tiles = Json::array(), rows = Json::array();
        for (const SeaOrders::Item &item : orders.items)
        {
            if (item.kind == "cancel")
                continue;
            const std::string element = "item-" + item.id;
            // Ships and forts are objects: enamel tokens (red = the target of an attack order is
            // whoever the engine lists; the engine does not say whose side a target is on).
            const bool object = item.kind == "ship" || item.kind == "fort";
            Json entry = {{"id", element},
                          {"title", item.label},
                          {"image", object ? Json{{"icon", ItemIcon(item)}, {"enamel", "iron"}} : Json{{"icon", ItemIcon(item)}}},
                          {"disabled", !item.enabled},
                          {"pressed", item.id == choice_},
                          {"cooldown", item.enabled ? 0 : std::lround((1.0 - item.cooldown) * 100.0)}};
            // Commands and their options are tiles; abilities and targets are rows.
            const bool tile = item.kind == "command" || item.kind == "charge" || item.kind == "option";
            (tile ? tiles : rows).push_back(std::move(entry));
            input.On(element, "click", [this, id = item.id](const Event &) { Choose(id); });
        }
        view.sheet = "s-hud-orders";
        view.modal = true;
        view.sheet_props = Json{{"title", hud_.Text("hud.orders.title")}, {"back", !root && orders.can_back}, {"crumb", root ? std::string() : orders.title},
                                {"recipients", recipients},              {"tiles", tiles},                   {"rows", rows}};
        input.On("sheet-back", "click", [this](const Event &) { Order("back", {}); });
        input.On("sheet-close", "click", [this](const Event &) { Order("close", {}); });
        input.On("sheet", "close", [this](const Event &) { Order("close", {}); });
    }

    // The panel is on the screen with exactly this menu revision: only now may the engine
    // stop drawing its own. The engine keeps the word until the menu changes.
    void CoverOrders(const SeaOrders &orders, bool drawn)
    {
        if (drawn && orders.known && orders.active && orders.complete)
        {
            const std::string owner = Owner(orders);
            if (owner != orders_covered_)
            {
                orders_covered_ = owner;
                hud_.Send("orders", owner + "\tack\t1", "orders-ack");
            }
            Follow(orders);
            return;
        }
        if (orders_covered_.empty())
            return;
        // A menu revision the engine has replaced needs no word: its coverage ended with it.
        if (orders.known && Owner(orders) == orders_covered_)
            hud_.Send("orders", orders_covered_ + "\tack\t0", "orders-ack");
        orders_covered_.clear();
    }

    Hud &hud_;
    std::string layout_ = "sea";
    int band_left_ = 0; // dp from the safe area; 0 = the layout's own band
    // held controls
    std::map<std::string, Held> holds_;
    int turn_ = 0;
    // sails
    int sail_target_ = -1;
    double sail_until_ = 0.0;
    // ammunition fan
    bool fan_open_ = false;
    double fan_until_ = 0.0;
    // quick action
    bool quick_covered_ = false;
    std::string quick_covered_owner_, quick_tapped_;
    double quick_covered_at_ = 0.0;
    // minimap
    bool zoom_in_ = true;
    bool navigator_prepared_ = false;
    std::string navigator_owner_, navigator_geometry_;
    uint64_t navigator_layout_ = 0;
    double navigator_sent_ = -1.0;
    // the engine's indicators
    std::string ship_covered_;
    // orders panel
    std::string orders_covered_;
    std::string choice_;
    double choice_until_ = 0.0;
    uint64_t choice_revision_ = 0;
    bool choice_activated_ = false;
};

} // namespace

std::unique_ptr<Scene> CreateSeaScene(Hud &hud)
{
    return std::make_unique<SeaScene>(hud);
}

} // namespace storm::ui::hud
