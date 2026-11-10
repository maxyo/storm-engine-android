// The in-game HUD: state channels of the game become the props of the HUD elements that
// hud-layout.json of the pack places in a scene, and component events become engine input.
// See README.md in ../screens ("The HUD") for how to port a scene.
#pragma once

#include "../screens/screen.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace storm::ui::hud
{

using screens::Event;

// --- states: one struct per channel, filled by its consumer (states.cpp) ---------------

// Channel "gameplay": the engine's mode and the controls bound in it.
struct Gameplay
{
    uint64_t revision = 0;
    std::string mode = "unknown"; // explore, melee, sailing, naval, worldmap, unknown
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
    std::string player, mode = "unknown", interact_kind = "interact";
    std::set<std::string> actions;
    std::string pistol = "unknown"; // unknown, no_weapon, unloaded, loaded
    int pistol_progress = 0;        // 0…20 while unloaded

    bool Has(const std::string &action) const
    {
        return actions.count(action) != 0;
    }
};

// Channel "vitals": the land hero's bars and loaded charges.
struct Vitals
{
    bool known = false;
    double health = 0.0, energy = 0.0, reload = 0.0; // 0…1
    int loaded = 0, capacity = 0;
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
    LandCommands land_commands;
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
};
// The migration switch of the HUD (scenes.cpp): gameplay modes whose HUD is drawn here.
// Every other mode still belongs to the platform host.
std::span<const SceneEntry> Scenes();
bool Ported(std::string_view mode);

// What the HUD needs from the host; unit tests give it a fake.
class Mount
{
  public:
    virtual ~Mount() = default;
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
    // The player can act: touches on HUD elements belong to the HUD.
    bool Interactive() const
    {
        return visible_ && states_.presentation.live;
    }
    const View &Shown() const
    {
        return view_;
    }

  private:
    struct Line
    {
        std::string text, tone;
        double until = 0.0;
    };
    void Refresh();
    void Conceal();
    void Present(bool visible);

    Mount &mount_;
    std::span<const SceneEntry> scenes_;
    States states_;
    const SceneEntry *entry_ = nullptr;
    std::unique_ptr<Scene> scene_;
    Input input_;
    View view_;
    std::vector<Line> lines_;
    std::set<std::string> reported_;
    std::string language_; // game language last given to the mount
    bool changed_ = true;
    double spyglass_ack_at_ = -1.0;  // last coverage acknowledgement of the spyglass overlay
    uint64_t spyglass_ack_generation_ = 0;
    bool visible_ = false;
    bool live_ = false;
    bool interface_ = false;
};

// The spyglass overlay (spyglass.cpp): its channel's consumer and the props of its part.
std::unique_ptr<screens::StateConsumer> CreateSpyglassConsumer();
Json SpyglassProps(const Spyglass &spyglass, Hud &hud);

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
