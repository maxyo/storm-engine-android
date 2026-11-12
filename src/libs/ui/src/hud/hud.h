// The in-game HUD: state channels of the game become the props of the HUD elements that
// hud-layout.json of the pack places in a scene, and component events become engine input.
// See README.md in ../screens ("The HUD") for how to port a scene.
#pragma once

#include "../screens/screen.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace storm::ui::hud
{

// A message shown again while its toast is still up: one toast, "text ×N". Shared by the
// HUD's event log and the runtime's toast stack (every game log line comes through these).
inline std::string RepeatedText(const std::string &text, int count)
{
    return count > 1 ? text + " \u00D7" + std::to_string(count) : text;
}


using screens::Event;

// --- states: one struct per channel, filled by its consumer (states.cpp) ---------------

// Channel "gameplay": the engine's mode and the controls bound in it.
struct Gameplay
{
    uint64_t revision = 0;
    std::string mode = "unknown"; // explore, melee, sailing, naval, worldmap, unknown
    // Mode "unknown" only: why (the engine's GameplaySnapshot::blocked): starting, paused,
    // loading, interface, dialogue, cutscene, video, commands, no-scene, no-land-interface,
    // no-player, group, error; empty before the first state.
    std::string blocked;
    std::string language;
    std::string input_owner;      // land only: one player in one loaded scene
    bool analog_movement = false;
    uint64_t movement_generation = 0;
    std::set<std::string> capabilities;

    bool Has(const std::string &control) const
    {
        return capabilities.count(control) != 0;
    }
};

// Channel "combat": the land actions the engine offers and the pistol.
struct Combat
{
    bool known = false;
    uint64_t gameplay_revision = 0, activity = 0;
    std::string player, mode = "unknown", interact_kind = "interact"; // interact_kind: dialog | boarding | interact
    std::string interact_name; // whom the talk button talks to ("" = the engine has no name)
    // Offered action -> its availability generation, which a command for it carries.
    std::map<std::string, uint64_t> actions;
    std::string pistol = "unknown"; // unknown, no_weapon, unloaded, loaded
    int pistol_progress = 0;        // 0…20 while unloaded

    bool Has(const std::string &action) const
    {
        return actions.count(action) != 0;
    }
};

// Channel "vitals": the land hero's bars and loaded charges, and his status
// (battle_interface land_status.hpp): effects, potions, officers, dash cooldown.
// A picture of the player's own game files for c-game-image: a texture-namespace name and the
// rectangle "left,top,right,bottom" (0…1) of it, "" for the whole texture. Empty texture = none.
struct Picture
{
    std::string texture, uv;
};

struct Vitals
{
    struct Officer
    {
        int id = -1;
        std::string name, face;            // face: the game's FaceId
        Picture picture;                   // what the land interface draws for him (two.tga without a face)
        double health = 0.0, energy = 0.0; // 0…1
    };
    bool known = false;
    int hero = -1;    // the hero's character index (-1: not given)
    std::string name, face; // the hero's name and FaceId
    Picture picture;        // the hero's land interface picture
    double health = 0.0, energy = 0.0, reload = 0.0; // 0…1
    int loaded = 0, capacity = 0;
    std::vector<std::string> effects; // poison, drunk, healing
    int potions = 0;                  // health potions carried
    std::vector<Officer> officers;
    double dash = -1.0;               // backstep cooldown left 0…1; -1 outside fight mode
    // The enemy captain of a boarding cabin fight while alive (V2-Land-Boarding-Cabin).
    std::string opponent;             // name; empty = none
    double opponent_health = 0.0;     // 0…1
};

// Channel "landCommands": the engine's land action menu.
struct LandCommands
{
    struct Item
    {
        std::string id, kind, label, source;
        bool enabled = false;
        double cooldown = 0.0;
    };
    bool known = false;
    uint64_t scene = 0, revision = 0, activity = 0;
    bool active = false, available = false, can_back = false, can_close = false;
    bool complete = false; // everything is known and drawable; otherwise the engine keeps its own menu
    std::string stage = "commands", title, selected;
    std::vector<Item> items;

    const Item *Find(const std::string &id) const
    {
        for (const Item &item : items)
            if (item.id == id)
                return &item;
        return nullptr;
    }
};

// Channel "ship": the player's ship at sea, its companions and what the engine's own sea
// indicators read. Ratios are 0…1, negative = the engine does not know the value.
struct Ship
{
    struct Vessel
    {
        int character = -1;
        std::string entity, name, type;
        int ship_class = 0;
        double hull = -1.0, crew = -1.0, sails = -1.0;
        Picture picture; // the battle interface's ship icon
    };
    bool known = false; // a valid frame of the current sea scene
    uint64_t gameplay_revision = 0, sequence = 0, hud_revision = 0;
    Vessel own;
    std::vector<Vessel> fleet; // companions, the flagship is `own`
    bool captions = false;     // the engine draws its text readouts now: ours may replace them
    bool navigator = false, zoom_in = false, zoom_out = false;
    uint64_t navigator_generation = 0, navigator_layout = 0; // layout = the aperture the engine drew last
    double speed = -1.0, wind = -1.0, cannon_charge = -1.0;
    double time_scale = -1.0; // the world's time scale the scripts set (x1 = 1)
    // Battle abilities (instant repair, immediate reload) the ship's people can use.
    struct Ability
    {
        std::string id;          // InstantRepair, ImmediateReload
        std::string state;       // ready, active, recovery, blocked
        double fill = 0.0;       // active: what is left of the effect; recovery: how far it recovered
    };
    std::vector<Ability> abilities;
    std::string date_time, location;
    std::string ammo;        // balls, grapes, knippels, bombs; "" = unknown
    double ammo_count = -1.0;
    bool ammo_low = false;   // the engine's own "not enough shot" warning
    bool powder_out = false; // … and its "no powder" warning
    double ammo_counts[4] = {-1.0, -1.0, -1.0, -1.0}; // by kind, in the order above
    double sail_state = -1.0; // 0, 0.5, 1
    bool can_set_sails = false;
};

// Channel "seaOrders": the engine's sea command menu (orders to the own ship and companions).
struct SeaOrders
{
    struct Recipient
    {
        std::string id, name, entity;
        int character = -1;
        bool main = false;
    };
    struct Item
    {
        std::string id, kind, label, source;
        bool enabled = false;
        double cooldown = 0.0;
    };
    bool known = false;
    uint64_t scene = 0, revision = 0, activity = 0;
    bool available = false, active = false, can_back = false, can_close = false;
    bool complete = false; // drawable here as a whole; otherwise the engine keeps its own menu
    std::string stage = "commands", recipient, title, selected; // stage: commands, options, targets
    std::vector<Recipient> recipients;
    std::vector<Item> items;

    const Item *Find(const std::string &id) const
    {
        for (const Item &item : items)
            if (item.id == id)
                return &item;
        return nullptr;
    }
};

// Channel "seaQuickAction": the one quick action the engine offers at sea.
struct SeaQuick
{
    bool known = false;
    uint64_t activity = 0, revision = 0;
    bool available = false;
    std::string action, label; // Map, Moor, Board, LandTroops, Reload; the engine's caption
};

// Channel "worldMap": the world map's instruments, fleet and commands. Absent numbers are
// negative / empty; angles are radians as the engine measures them.
struct WorldMap
{
    struct Vessel
    {
        std::string id, name, type;
        double hull = -1.0, sails = -1.0; // 0…100
        double crew = -1.0;               // 0…1 of the ship's crew capacity
        Picture picture;                  // the map sign's ship icon
    };
    struct Command
    {
        std::string id, label; // EnterToSea, EnterToIsland, EnterToShip, EnterToStorm, EnterToAttack, EnterToEnemy
    };
    bool known = false, available = false;
    uint64_t revision = 0, activity = 0, gameplay_revision = 0;
    unsigned mask = 0; // what the engine lets a touch HUD replace (instruments 1, ships 2, commands 4)
    bool instruments = false, ships_visible = false, commands_ready = false;
    std::string date, coordinates;
    double hour = -1.0, wind_force = -1.0, morale = -1.0, speed = -1.0;
    double time_scale = -1.0; // the world's time scale the scripts set (x1 = 1)
    double wind_angle = 0.0, heading = 0.0;
    bool angles = false; // wind_angle and heading are known
    int food_days = -1, rum_days = -1, flag = -1, storm = -1;
    std::vector<Vessel> fleet;
    std::vector<Command> commands;
};

// Channel "presentation": what the platform host shows over the game and whether the
// player can act at all. Published by the host, not by the engine.
struct Presentation
{
    bool known = false;
    std::string screen;    // interface id open in the game ("" = none), ported or not
    bool loading = false, cinematic = false, dialogue = false;
    bool live = false;     // the game is in front and running: input may be sent
};

// Channel "spyglass": the telescope and what it shows of its target (spyglass.cpp). A field
// the telescope cannot show at its level is -1 / empty.
struct Spyglass
{
    bool known = false, on = false, target = false;
    uint64_t generation = 0;
    std::string kind, name, type, captain, cannon_type, portrait; // kind: ship | fort
    int ship_class = -1, hull = -1, sails = -1, crew = -1, cannons = -1, cannons_max = -1, charge = -1, nation = -1, sail = -1;
    double speed = -1.0, distance = -1.0;
    int skills[5] = {-1, -1, -1, -1, -1}; // fencing, cannons, accuracy, navigation, boarding
};

struct States
{
    Gameplay gameplay;
    Combat combat;
    Vitals vitals;
    std::string world_info;     // channel "worldInfo": lines of the location text
    std::string context_action; // channel "contextAction": the script's quick action, "" = none
    std::string reload_place;   // channel "reloadPlace": where the reload locator the hero is in leads, "" = none
    LandCommands land_commands;
    Ship ship;
    SeaOrders sea_orders;
    SeaQuick sea_quick;
    WorldMap world_map;
    Presentation presentation;
    Spyglass spyglass;
};

// --- view -----------------------------------------------------------------------------

// What a scene shows. `elements` holds the live props of the layout elements by id; an
// element of the layout scene that is left out is not mounted at all.
struct View
{
    std::string layout;                   // scene of hud-layout.json
    Json elements = Json::object();
    std::vector<std::string> dimmed;      // elements drawn subdued (under an open wheel)
    std::string sheet;                    // pack part over the scene ("s-hud-sheet"), "" = none
    Json sheet_props = Json::object();
    bool modal = false;                   // the sheet takes every touch and the back key
    bool log = false;                     // event lines are shown now (Hud::LogLines in the `log` element)
    std::string overlay;                  // pack part over the scene and under the sheet, takes no touches
    Json overlay_props = Json::object();  // (the spyglass target card, laid by the Hud over any scene)
    bool operator==(const View &) const = default;
};

// Connects element ids of the view to what happens on their events (Scene::Build).
class Input
{
  public:
    using Handler = std::function<void(const Event &)>;
    // An empty verb takes every verb of the element.
    void On(const std::string &id, const std::string &verb, Handler handler);
    // Expected and needs no action. An event that is neither bound nor ignored is an error.
    void Ignore(const std::string &id, const std::string &verb);

  private:
    friend class Hud;
    std::unordered_map<std::string, Handler> handlers_;
};

class Hud;

// One ported gameplay zone (land, sea, world map). The instance lives while the gameplay
// mode stays in its zone, so members hold local state (an open wheel, a held stick).
class Scene
{
  public:
    virtual ~Scene() = default;
    // Called for every state change while the HUD is visible and after Hud::Rebuild().
    virtual void Build(const States &states, View &view, Input &input) = 0;
    // The states changed while the HUD cannot be seen (a menu, loading, a cutscene).
    virtual void Hidden(const States &)
    {
    }
    // Timed work (heartbeats); returns the seconds until it must be called again.
    virtual double Tick(const States &, double /*now*/)
    {
        return 3600.0;
    }
    // A frame with the current view was drawn (coverage acknowledgements).
    virtual void Drawn(const States &)
    {
    }
    // Every held control was let go (the platform cancelled input).
    virtual void Released(const States &)
    {
    }
    // The player cannot act any more (the game lost the screen): give up what the HUD owns.
    virtual void Suspended(const States &)
    {
    }
    // The engine reports no gameplay mode ("unknown") while something of this scene is
    // still open (the land menu blocks gameplay): true keeps the scene until it closes.
    virtual bool Holds(const States &) const
    {
        return false;
    }
    // The back key; true when the scene used it (an open sheet).
    virtual bool Back(const States &)
    {
        return false;
    }
};

struct SceneEntry
{
    const char *name;
    std::span<const char *const> modes; // gameplay modes it draws
    std::unique_ptr<Scene> (*create)(Hud &);
    // The engine's name for this HUD in "hudowner:<name>", said once when the HUD starts: the
    // engine then keeps that HUD's legacy rendering off (android_hud_presenter.hpp).
    const char *presents = nullptr;
};
// The migration switch of the HUD (scenes.cpp): gameplay modes whose HUD is drawn here.
// Every other mode still belongs to the platform host.
std::span<const SceneEntry> Scenes();
bool Ported(std::string_view mode);

// Where a part of a mounted HUD element is on the screen, in physical pixels.
struct Placement
{
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f; // the box
    float layer_x = 0.0f, layer_y = 0.0f;                  // the origin of the HUD layer (the safe area)
    float screen_width = 0.0f, screen_height = 0.0f;
    float density = 1.0f; // pixels per dp
    float layer_width = 0.0f, layer_height = 0.0f; // the size of the HUD layer (the safe area)
};

// What the HUD needs from the host; unit tests give it a fake.
class Mount
{
  public:
    virtual ~Mount() = default;
    // The box of the mounted element `id`, or of the part inside it that `selector` names,
    // as the last frame laid it out. False while it is not on the screen.
    virtual bool Measure(const std::string &id, const std::string &selector, Placement &out) const = 0;
    // Mounts the layout scene with these element props, patching what is already there.
    // False when the pack cannot draw it (unknown scene or element); the reason is reported.
    virtual bool ShowHud(const View &view) = 0;
    virtual void HideHud() = 0;
    virtual void Send(const Action &action) = 0;
    // The game language of the HUD ("russian"): tr() in templates and Text() follow it.
    virtual void SetLanguage(const std::string &game_language) = 0;
    virtual std::string Text(const std::string &key) = 0;
    virtual void Log(const std::string &line) = 0;
    virtual void Error(const std::string &line) = 0;
    virtual double Now() const = 0;
    // A game interface is mounted over the game: the HUD stays out of the way.
    virtual bool InterfaceActive() const = 0;
};

using screens::SubjectRequest;

class Hud
{
  public:
    // `scenes`: the ported scenes; unit tests give their own.
    explicit Hud(Mount &mount, std::span<const SceneEntry> scenes = Scenes());
    ~Hud();

    // --- for consumers ---
    States &Edit()
    {
        changed_ = true;
        return states_;
    }
    // A channel carried something it must not: logged once per distinct text, shown in debug hosts.
    void Reject(const std::string &channel, const std::string &why);

    // --- for scenes ---
    const States &Current() const
    {
        return states_;
    }
    // Engine input: `verb` names the entry point ("move", "combat", "landcommands",
    // "context", "shortcut"), `value` is its payload. A `slot` marks a latest-value input
    // (stick position, coverage acknowledgement): only the newest one of a slot matters.
    void Send(const std::string &verb, const std::string &value, const std::string &slot = {});
    void Rebuild()
    {
        changed_ = true;
    }
    double Now() const
    {
        return mount_.Now();
    }
    bool Measure(const std::string &id, const std::string &selector, Placement &out) const
    {
        return mount_.Measure(id, selector, out);
    }
    // Logged once per distinct text.
    void Log(const std::string &line)
    {
        if (reported_.insert(line).second)
            mount_.Log(line);
    }
    // A string of the pack's table in the game language of the gameplay state. Every text
    // of the HUD that the game does not send comes from here.
    std::string Text(const std::string &key)
    {
        return mount_.Text(key);
    }
    // Reported once per distinct text.
    void Error(const std::string &line);
    // Event lines shown top-centre by the scene's `log` element; newest first.
    Json LogLines() const;

    // A tap on a party portrait or a squadron ship opens a game screen on that subject: the
    // scene sends the opening shortcut and leaves the subject here; the screen that opens takes
    // it once (screens/hero_character.cpp: the officer switch, hero_ship.cpp: the squadron
    // switch). A request nobody takes lapses after kSubjectSeconds.
    static constexpr double kSubjectSeconds = 5.0;
    void RequestSubject(const SubjectRequest &request);
    // The pending subject of `screen` ("character" | "ship"), cleared by taking it.
    std::optional<SubjectRequest> TakeSubject(const std::string &screen);

    // --- for the host ---
    // An event line for the top-centre log; false while no scene shows one.
    bool Notice(const std::string &text, const std::string &tone);
    // False when the event belongs to nothing bound.
    bool OnEvent(const Event &event);
    // Applies pending state changes and timed work; returns seconds until the next call.
    double Tick();
    void Drawn();
    // Every pointer was released by the platform.
    void Released();
    bool Back();
    bool Visible() const
    {
        return visible_;
    }
    // What is shown is only the game-menu button (layout "menu"): no scene draws this mode.
    bool MenuOnly() const
    {
        return visible_ && menu_only_;
    }
    // The player can act: touches on HUD elements belong to the HUD.
    bool Interactive() const
    {
        return visible_ && states_.presentation.live;
    }
    const View &Shown() const
    {
        return view_;
    }
    // The game is in a gameplay mode, or its scene holds through an "unknown" one (an open
    // sheet that blocks gameplay): from the states as they are now, not the last refresh.
    bool InGameplay() const
    {
        return states_.gameplay.mode != "unknown" || (scene_ && scene_->Holds(states_));
    }

  private:
    struct Line
    {
        std::string text, tone;
        double until = 0.0;
        std::string base; // the message itself; `text` adds the repeat count
        int count = 1;
        unsigned key = 0;     // the line's own element in the log (data-key)
        double leaving = -1.0; // when it started to animate out (-1 = shown); removed kLeaveSeconds later
    };
    void Refresh();
    void Conceal();
    void Present(bool visible, bool menu_only);

    Mount &mount_;
    std::optional<SubjectRequest> subject_;
    std::span<const SceneEntry> scenes_;
    States states_;
    const SceneEntry *entry_ = nullptr;
    std::unique_ptr<Scene> scene_;
    Input input_;
    View view_;
    std::vector<Line> lines_;
    unsigned next_line_key_ = 0;
    std::set<std::string> reported_;
    std::string language_; // game language last given to the mount
    bool changed_ = true;
    std::string spyglass_answer_; // the last word said about the spyglass overlay
    bool visible_ = false;
    bool menu_only_ = false; // shown is only the layout "menu" (the game-menu button)
    bool live_ = false;
    bool interface_ = false;
    bool declared_ = false; // the "hudowner" declarations were sent
};

// The spyglass overlay (spyglass.cpp): its channel's consumer and the props of its part.
std::unique_ptr<screens::StateConsumer> CreateSpyglassConsumer();
Json SpyglassProps(const Spyglass &spyglass, Hud &hud);
// ASCII lower case: engine identifiers are matched without regard to case.
std::string Lower(std::string text);

// The game's time acceleration in four steps (owner decision), the same at sea and on the world
// map: the "time" button shows the scale the engine runs at (`scale`, x1 = 1; negative: unknown,
// no button) and a tap asks for the next of x1, x2, x4, x8. The engine applies it through the
// scripts, which clamp it to their limits (android_time_scale.hpp). Offered only when the
// gameplay binds all four steps.
void BuildTimeScale(Hud &hud, const Gameplay &gameplay, double scale, View &view, Input &input);

// The fleet token of a ship by a word of the engine's ship model name ("Lugger" → ship-lugger).
const char *ShipIcon(const std::string &type);

// One member of a party or squadron column (c-hud-plate variant officers / fleet): the game
// picture and its bars, drawn as arcs around it in this order from the outside in, {kind,
// share 0…1}; a share below 0 (unknown) is left out. `tap` makes the cell report its index (verb `member` / `ship`).
struct MemberBar
{
    const char *kind;
    double share;
};
Json MemberCell(const std::string &name, const Picture &picture, std::initializer_list<MemberBar> bars, bool tap);
// The picture of a state channel: {texture, uv: [left, top, right, bottom]}; an error text
// when it has another shape.
std::string ReadPicture(const Json &value, Picture &out);

// Consumers of the HUD channels, added to the router's list (screens/registry.cpp).
void AddStateConsumers(std::vector<std::unique_ptr<screens::StateConsumer>> &consumers);

// One element of a layout scene as the pack holds it (hud-layout.json with the `rcss` the
// pack build adds).
struct LayoutElement
{
    std::string id, component, rcss, behavior;
    Json props = Json::object();
    Json data = Json::object(); // data-* attributes: configuration of the behaviour
    std::vector<LayoutElement> children;
};
using Layout = std::unordered_map<std::string, std::vector<LayoutElement>>;
// False with `error` set when the file is not a layout this runtime understands.
bool ParseLayout(const Json &file, Layout &out, std::string *error);
// Empty when `view` can be drawn with `layout`; otherwise what is wrong.
std::string CheckView(const Layout &layout, const View &view);

} // namespace storm::ui::hud
