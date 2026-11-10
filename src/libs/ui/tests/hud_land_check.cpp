// Host checks for the land HUD. First the scene alone (a recording mount): what each
// synthetic state shows and which engine input each event sends. Then the whole runtime
// with a renderer that draws nothing and a small synthetic pack (tests/pack): which
// touches the UI claims over the game, per pointer, and what real gestures send.
// No GL context, no game data.
#include "../src/hud/hud.h"
#include "storm_ui/render_backend.h"

#include <RmlUi/Core/RenderInterface.h>

#include <cstdio>
#include <fstream>
#include <iterator>

using namespace storm::ui;
using namespace storm::ui::hud;

static int failures = 0;
static std::string states_directory;

static void Expect(bool ok, const std::string &what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

static void ExpectEqual(const std::string &actual, const std::string &expected, const std::string &what)
{
    if (actual != expected)
    {
        std::fprintf(stderr, "FAIL: %s\n  expected: %s\n  actual:   %s\n", what.c_str(), expected.c_str(), actual.c_str());
        ++failures;
    }
}

static std::string Text(const std::string &name)
{
    std::ifstream file(states_directory + "/" + name + ".json", std::ios::binary);
    Expect(static_cast<bool>(file), "state file " + name + ".json exists");
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

static Json State(const std::string &name)
{
    return Json::parse(Text(name), nullptr, false);
}

// --- the scene alone --------------------------------------------------------------------

struct FakeMount final : Mount
{
    View shown;
    bool visible = false;
    std::vector<std::string> commands, slots, logs, errors;
    double now = 0.0;

    bool ShowHud(const View &view) override
    {
        shown = view;
        visible = true;
        return true;
    }
    void HideHud() override
    {
        visible = false;
    }
    void Send(const Action &action) override
    {
        commands.push_back(EngineCommand(action));
        slots.push_back(action.node);
    }
    void SetLanguage(const std::string &) override
    {
    }
    std::string Text(const std::string &key) override
    {
        return key;
    }
    void Log(const std::string &line) override
    {
        logs.push_back(line);
    }
    void Error(const std::string &line) override
    {
        errors.push_back(line);
    }
    double Now() const override
    {
        return now;
    }
    bool InterfaceActive() const override
    {
        return false;
    }
    std::string Last() const
    {
        return commands.empty() ? std::string() : commands.back();
    }
    bool Has(const char *id) const
    {
        return shown.elements.contains(id);
    }
};

struct QuietSurface final : screens::Surface
{
    void Show(const std::string &, const Json &) override
    {
    }
    void Hide() override
    {
    }
    void Send(const Action &) override
    {
    }
    void RequestText(TextRequest, const std::string &) override
    {
    }
    void Toast(const std::string &) override
    {
    }
    void SetLanguage(const std::string &) override
    {
    }
    std::string Text(const std::string &key) override
    {
        return key;
    }
    void Log(const std::string &) override
    {
    }
    void Error(const std::string &) override
    {
    }
    double Now() const override
    {
        return 0.0;
    }
};

struct Bench
{
    FakeMount mount;
    Hud hud{mount};
    QuietSurface surface;
    screens::Router router{surface};

    Bench()
    {
        router.AttachHud(&hud);
        Feed("presentation", "presentation-game");
    }
    void Feed(const std::string &channel, const std::string &file)
    {
        router.SetState(channel, State(file));
        hud.Tick();
    }
    void Set(const std::string &channel, const Json &state)
    {
        router.SetState(channel, state);
        hud.Tick();
    }
    void Explore()
    {
        Feed("gameplay", "gameplay-explore");
        Feed("combat", "combat-explore");
        Feed("vitals", "vitals");
        Feed("worldInfo", "world-info");
        Feed("landCommands", "land-commands-idle");
    }
    void Melee()
    {
        Feed("gameplay", "gameplay-melee");
        Feed("combat", "combat-melee");
        Feed("vitals", "vitals");
        Feed("landCommands", "land-commands-idle");
    }
    // A frame: timed work, then the drawn-frame notice.
    void Frame(double seconds = 0.0)
    {
        mount.now += seconds;
        hud.Tick();
        hud.Drawn();
    }
    void Press(const std::string &id, const std::string &verb = "click", const std::string &value = {})
    {
        hud.OnEvent({id, verb, value});
        hud.Tick();
    }
};

static void CheckExplore()
{
    Bench bench;
    FakeMount &mount = bench.mount;
    bench.Explore();
    ExpectEqual(mount.shown.layout, "land", "explore uses the land layout");
    Expect(mount.shown.elements["health"]["value"] == 84 && mount.shown.elements["energy"]["value"] == 62 && mount.Has("vitals"), "vitals in percent");
    ExpectEqual(mount.shown.elements["location"]["text"].get<std::string>(), "Сен-Пьер, улицы · 11:15", "the location lines on one line");
    Expect(mount.Has("hero") && mount.shown.elements["hero"]["disabled"] == false && mount.Has("pause") && mount.Has("move") && mount.Has("actions") &&
               mount.Has("potion") && mount.Has("weapon") && !mount.Has("context") && !mount.Has("strike") && !mount.shown.modal,
           "the peace set; no context without an action");
    ExpectEqual(mount.shown.elements["weapon"]["label"].get<std::string>(), "hud.weapon.draw", "the weapon button names what it will do");

    bench.Press("hero");
    ExpectEqual(mount.Last(), "shortcut:character", "Персонаж opens the character");
    bench.Press("pause");
    ExpectEqual(mount.Last(), "shortcut:mainmenu", "Меню opens the menu");
    bench.Press("weapon");
    ExpectEqual(mount.Last(), "combat:1\t7\t5\t1\tweapon\ttap", "the weapon tap carries the revision, the activity and a sequence");
    bench.Press("potion");
    ExpectEqual(mount.Last(), "combat:1\t7\t5\t2\tpotion\ttap", "the sequence grows");
    bench.Press("actions");
    ExpectEqual(mount.Last(), "landcommands:1\t4\t10\t2\topen\t", "Действия asks the engine to open its menu");

    // the script's quick action
    bench.Feed("contextAction", "context-door");
    Expect(mount.Has("context") && mount.shown.elements["context"]["icon"] == "door" && mount.shown.elements["context"]["label"] == "hud.context.enter",
           "a door in front: the context slot");
    bench.Press("context");
    ExpectEqual(mount.Last(), "context:Reload", "the context tap names the action the engine published");
    bench.Set("contextAction", Json("Talk"));
    Expect(!mount.Has("context"), "the script's Talk never shows: talking belongs to the engine's target");
    bench.Set("contextAction", Json("Nothing"));
    Expect(!mount.Has("context"), "Nothing is no action");
    bench.Set("contextAction", Json("Levitate"));
    Expect(mount.Has("context") && mount.shown.elements["context"]["icon"] == "hand" && !mount.logs.empty() &&
               mount.logs.back() == "land HUD: context action 'Levitate' has no icon of its own",
           "an unknown action keeps its button with the generic look, and is logged");
    bench.Press("context");
    ExpectEqual(mount.Last(), "context:Levitate", "… and still acts");

    // talking: the engine's own target (U0): appears and disappears with the combat state
    bench.Feed("combat", "combat-explore-talk");
    Expect(mount.Has("context") && mount.shown.elements["context"]["icon"] == "talk" && mount.shown.elements["context"]["label"] == "hud.context.talk",
           "somebody to talk to: the talk button takes the slot");
    bench.Press("context");
    ExpectEqual(mount.Last(), "combat:1\t7\t5\t3\tinteract\ttap", "talk is the engine's interact action");
    const size_t sent = mount.commands.size();
    bench.router.SetState("combat", State("combat-explore")); // the target is lost; the tap arrives before the next build
    bench.hud.OnEvent({"context", "click", ""});
    Expect(mount.commands.size() == sent, "a tap after the target is lost sends nothing");
    bench.hud.Tick();
    bench.Set("contextAction", Json(""));
    Expect(!mount.Has("context"), "the target left: no button");

    // a combat state of another revision is not ready: buttons stay, disabled, and send nothing
    Json stale = State("combat-explore");
    stale["gameplayRevision"] = 9;
    stale["activityGeneration"] = 6;
    bench.Set("combat", stale);
    Expect(mount.Has("weapon") && mount.shown.elements["weapon"]["disabled"] == true, "another revision: the weapon button is disabled");
    const size_t before = mount.commands.size();
    bench.Press("weapon");
    Expect(mount.commands.size() == before, "… and its tap sends nothing");

    // no vitals, no location, no menu
    bench.Set("vitals", Json(nullptr));
    bench.Set("worldInfo", Json(""));
    bench.Set("landCommands", Json(nullptr));
    Expect(!mount.Has("vitals") && !mount.Has("health") && !mount.Has("location") && !mount.Has("actions"), "absent states take their elements away");

    // without analog movement there is no stick, and that is said
    Json keyboard = State("gameplay-explore");
    keyboard["analogMovement"] = false;
    bench.Set("gameplay", keyboard);
    Expect(!mount.Has("move") && !mount.errors.empty() && mount.errors.back().find("no analog movement") != std::string::npos, "no analog movement: no stick, an error");
}

static void CheckMovement()
{
    Bench bench;
    FakeMount &mount = bench.mount;
    bench.Explore();
    mount.commands.clear();
    bench.Press("move", "change", "0,-100");
    ExpectEqual(mount.Last(), "move:1\ttown:42\t3\t1\t1\t0\t1000", "stick up = forward, full travel");
    Expect(mount.slots.back() == "move", "the stick uses its latest-value slot");
    bench.Press("move", "change", "50,50");
    ExpectEqual(mount.Last(), "move:1\ttown:42\t3\t2\t1\t500\t-500", "right and back");
    const size_t count = mount.commands.size();
    bench.Frame(0.05);
    Expect(mount.commands.size() == count, "no heartbeat before 100 ms");
    bench.Frame(0.06);
    ExpectEqual(mount.Last(), "move:1\ttown:42\t3\t3\t1\t500\t-500", "a held stick repeats itself for the engine's 350 ms watchdog");
    Expect(bench.hud.Tick() <= 0.11, "… and asks for the frame of the next heartbeat");

    // explore → melee keeps the stick: the next heartbeat goes on under the same owner
    bench.Melee();
    bench.Frame(0.11);
    ExpectEqual(mount.Last(), "move:1\ttown:42\t3\t4\t1\t500\t-500", "the stick survives the stance change");

    bench.Press("move", "change", "0,0");
    ExpectEqual(mount.Last(), "move:1\ttown:42\t3\t5\t0\t0\t0", "release stops the hero");
    const size_t stopped = mount.commands.size();
    bench.Frame(0.5);
    Expect(mount.commands.size() == stopped, "no heartbeat after the release");

    // the platform cancels input while the stick is held
    bench.Press("move", "change", "100,0");
    bench.hud.Released();
    Expect(mount.commands.size() >= 2 && mount.commands[mount.commands.size() - 2] == "move:1\ttown:42\t3\t7\t0\t0\t0", "cancelled input stops the hero");
    ExpectEqual(mount.Last(), "combat:1\t8\t6\t1\tall\tcancel", "… and cancels every combat hold");

    // in the background nothing moves
    bench.Feed("presentation", "presentation-background");
    const size_t quiet = mount.commands.size();
    bench.Press("move", "change", "100,0");
    bench.Frame(0.3);
    Expect(mount.commands.size() == quiet, "not live: the stick sends nothing");

    bench.Feed("presentation", "presentation-game");
    bench.Press("move", "change", "sideways");
    Expect(!mount.errors.empty() && mount.errors.back().find("expected 'x,y'") != std::string::npos, "a malformed stick value is an error");
}

static void CheckCombat()
{
    Bench bench;
    FakeMount &mount = bench.mount;
    bench.Melee();
    ExpectEqual(mount.shown.layout, "land-combat", "melee uses the combat layout");
    Expect(mount.Has("strike") && mount.Has("block") && mount.Has("shot") && mount.Has("actions") && mount.Has("potion") && mount.Has("weapon") &&
               !mount.Has("context") && !mount.Has("moves") && !mount.Has("dash") && !mount.Has("officers"),
           "the combat set; no wheel until the strike is held; nothing without data");
    Expect(mount.shown.elements["hero"]["disabled"] == true, "no character screen in a fight");
    ExpectEqual(mount.shown.elements["weapon"]["label"].get<std::string>(), "hud.weapon.sheathe", "the weapon button sheathes now");
    Expect(mount.shown.elements["shot"]["badge"] == "1" && mount.shown.elements["shot"]["disabled"] == false && !mount.shown.elements["shot"].contains("fill"),
           "a loaded pistol: charges as the badge, no fill");

    mount.commands.clear();
    bench.Press("shot");
    ExpectEqual(mount.Last(), "combat:1\t8\t6\t1\tpistol\ttap", "the shot");
    bench.Press("block", "down");
    ExpectEqual(mount.Last(), "combat:1\t8\t6\t2\tblock\tdown", "block is held …");
    bench.Press("block", "up");
    ExpectEqual(mount.Last(), "combat:1\t8\t6\t3\tblock\tup", "… and let go");
    const size_t count = mount.commands.size();
    bench.Press("block", "click");
    bench.Press("strike", "click");
    Expect(mount.commands.size() == count && mount.errors.empty(), "the press of a held or swiped button needs no action");

    // the wheel
    bench.Press("strike", "open", "fast");
    Expect(mount.Has("moves") && mount.shown.elements["moves"]["selected"] == "centre" && mount.shown.elements["moves"]["caption"] == "hud.move.fast" &&
               mount.shown.elements["moves"]["moves"].size() == 6,
           "holding the strike opens the wheel on the fast attack");
    Expect(mount.shown.dimmed == std::vector<std::string>({"strike", "block", "shot", "potion", "weapon"}), "the combat cluster that is shown is dimmed under the wheel");
    bench.Press("strike", "preview", "break");
    Expect(mount.shown.elements["moves"]["selected"] == "2" && mount.shown.elements["moves"]["caption"] == "hud.move.break", "a drag picks a sector and names the move");
    bench.Press("strike", "commit", "break");
    ExpectEqual(mount.Last(), "combat:1\t8\t6\t4\tbreak\ttap", "the release commits the move");
    Expect(!mount.Has("moves") && mount.shown.dimmed.empty(), "the wheel closes");
    bench.Press("strike", "open", "fast");
    bench.Press("strike", "cancel");
    Expect(!mount.Has("moves") && mount.Last() == "combat:1\t8\t6\t4\tbreak\ttap", "a cancelled gesture commits nothing");

    // a move the engine does not offer is drawn dim and cannot be committed
    Json fewer = State("combat-melee");
    fewer["actions"] = Json::array({"weapon", "fast", "force", "block"});
    bench.Set("combat", fewer);
    bench.Press("strike", "open", "fast");
    Expect(mount.shown.elements["moves"]["moves"][0]["disabled"] == true && mount.shown.elements["moves"]["moves"][4]["disabled"] == false, "unavailable moves are marked");
    const size_t sent = mount.commands.size();
    bench.Press("strike", "commit", "feint");
    Expect(mount.commands.size() == sent, "an unavailable move is not sent and nothing else is sent instead");
    Expect(!mount.Has("shot") && !mount.Has("potion"), "no pistol and no potion action: no buttons");

    // reloading
    bench.Feed("combat", "combat-melee-reload");
    const Json &shot = mount.shown.elements["shot"];
    Expect(shot["disabled"] == true && shot["reloading"] == true && shot["fill"] == 40 && shot["fillTone"] == "reload", "reloading: the fill rises, no shot");
    const size_t reloading = mount.commands.size();
    bench.Press("shot");
    Expect(mount.commands.size() == reloading, "an unloaded pistol does not fire");
    Json holster = State("combat-melee");
    holster["pistol"] = Json{{"state", "no_weapon"}, {"progress", 0}};
    bench.Set("combat", holster);
    Expect(!mount.Has("shot"), "no pistol equipped: no shot button");

    // a new activity restarts the sequence
    Json next = State("combat-melee");
    next["activityGeneration"] = 9;
    bench.Set("combat", next);
    bench.Press("weapon");
    ExpectEqual(mount.Last(), "combat:1\t8\t9\t1\tweapon\ttap", "a new activity generation counts from one");
}

static void CheckSheet()
{
    Bench bench;
    FakeMount &mount = bench.mount;
    bench.Explore();
    mount.commands.clear();

    bench.Feed("landCommands", "land-commands-root");
    Expect(mount.shown.sheet == "s-hud-sheet" && mount.shown.modal && mount.Has("vitals") && !mount.Has("move") && !mount.Has("hero"),
           "an open menu: the sheet over the bars, no controls");
    const Json &props = mount.shown.sheet_props;
    Expect(props["title"] == "hud.sheet.actions" && props["back"] == false && props["layout"] == "rows" && props["rows"].size() == 6,
           "the root stage: ✕ only, six rows, the cancel item is not a row");
    Expect(props["rows"][0]["id"] == "item-c1" && props["rows"][0]["icon"] == "compass" && props["rows"][0]["chevron"] == true &&
               props["rows"][2]["icon"] == "thought" && props["rows"][2]["chevron"] == false && props["rows"][3]["disabled"] == true,
           "rows: icon by command, › for stages, disabled as the engine says");
    ExpectEqual(mount.Last(), "landcommands:1\t4\t11\t2\tack\t2", "before the sheet is drawn the engine hears that it is coming");
    bench.Set("vitals", Json{{"health", 0.5}, {"energy", 0.5}});
    Expect(mount.commands.size() == 1, "… once");
    bench.Frame();
    ExpectEqual(mount.Last(), "landcommands:1\t4\t11\t2\tack\t1", "a drawn sheet covers the engine's menu");
    Expect(mount.slots.back() == "landcommands-ack", "acknowledgements use their latest-value slot");
    bench.Frame(0.1);
    Expect(mount.commands.size() == 2, "no repeat before 250 ms");
    bench.Frame(0.2);
    Expect(mount.commands.size() == 3 && mount.Last() == "landcommands:1\t4\t11\t2\tack\t1", "the coverage is repeated while the sheet stays (750 ms watchdog)");

    // the engine reports no gameplay mode while its menu is open: the sheet stays, nothing is closed
    Json blocked = State("gameplay-explore");
    blocked["mode"] = "unknown";
    blocked["revision"] = 20;
    const size_t open = mount.commands.size();
    bench.Set("gameplay", blocked);
    Expect(mount.visible && mount.shown.modal && mount.commands.size() == open, "gameplay blocked by the open menu: the sheet stays as it is");
    bench.Feed("gameplay", "gameplay-explore");

    // a tap acts at once: select, then activate against the revision that reports the selection
    bench.Press("item-c3");
    ExpectEqual(mount.Last(), "landcommands:1\t4\t11\t2\tselectItem\tc3", "a tap selects the row …");
    Expect(mount.shown.sheet_props["rows"][2]["pressed"] == true, "… which is drawn pressed");
    bench.Press("item-c1");
    ExpectEqual(mount.Last(), "landcommands:1\t4\t11\t2\tselectItem\tc3", "another tap waits for the first");
    bench.Feed("landCommands", "land-commands-root-selected");
    bench.Frame();
    ExpectEqual(mount.Last(), "landcommands:1\t4\t12\t2\tactivateItem\tc3", "… and the selected revision is activated");
    Expect(mount.commands[mount.commands.size() - 2] == "landcommands:1\t4\t12\t2\tack\t1", "after its coverage was acknowledged");
    bench.Press("item-c4");
    ExpectEqual(mount.Last(), "landcommands:1\t4\t12\t2\tactivateItem\tc3", "a disabled row does nothing");

    // the next stage
    bench.Feed("landCommands", "land-commands-travel");
    Expect(mount.shown.sheet_props["layout"] == "tiles" && mount.shown.sheet_props["back"] == true && mount.shown.sheet_props["title"] == "Переход" &&
               mount.shown.sheet_props["rows"][0]["icon"] == "tavern" && mount.shown.sheet_props["rows"][5]["icon"] == "usurer" &&
               mount.shown.sheet_props["rows"][4]["icon"] == "compass" &&
               mount.shown.sheet_props["rows"][0]["pressed"] == false,
           "travel: tiles, ←, place icons by fast-travel slot (the travel icon outside the table), the engine's title; the old tap is over");
    bench.Frame();
    bench.Press("sheet-back");
    ExpectEqual(mount.Last(), "landcommands:1\t4\t13\t2\tback\t", "← goes back a stage");
    Expect(bench.hud.Back() && mount.Last() == "landcommands:1\t4\t13\t2\tback\t" && mount.commands.size() >= 2, "the back key does the same in a nested stage");
    bench.Press("sheet", "close");
    ExpectEqual(mount.Last(), "landcommands:1\t4\t13\t2\tclose\t", "a tap beside the sheet closes it");

    bench.Feed("landCommands", "land-commands-potions");
    const Json &rows = mount.shown.sheet_props["rows"];
    Expect(mount.shown.sheet_props["layout"] == "dense" && rows.size() == 4 && rows[0]["heading"] == "hud.sheet.potions" && !rows[1].contains("heading") &&
               rows[3]["heading"] == "hud.sheet.abilities" && rows[3]["cooldown"] == 60 && rows[3]["disabled"] == true && rows[0]["cooldown"] == 0,
           "potions and abilities under headings; a recovering ability shows what is left");

    // a tap that the engine never answers ends by itself
    bench.Frame();
    bench.Press("item-p1");
    Expect(mount.shown.sheet_props["rows"][0]["pressed"] == true, "pressed while waiting");
    bench.Frame(1.6);
    Expect(mount.shown.sheet_props["rows"][0]["pressed"] == false, "the wait ends after 1.5 s");

    // the root stage closes on the back key
    bench.Set("landCommands", Json(nullptr));
    bench.Feed("landCommands", "land-commands-root");
    bench.Frame();
    Expect(bench.hud.Back() && mount.Last() == "landcommands:1\t4\t11\t2\tclose\t", "the back key closes the root stage");
    bench.Press("sheet-close");
    ExpectEqual(mount.Last(), "landcommands:1\t4\t11\t2\tclose\t", "✕ closes");

    // losing the screen: no coverage, the menu is closed
    mount.commands.clear();
    bench.Feed("presentation", "presentation-background");
    Expect(mount.commands.size() >= 2 && mount.commands[mount.commands.size() - 2] == "landcommands:1\t4\t11\t2\tack\t0" &&
               mount.Last() == "landcommands:1\t4\t11\t2\tclose\t",
           "in the background the coverage is withdrawn and the menu closed");
    bench.Frame(0.3);
    Expect(mount.Last() == "landcommands:1\t4\t11\t2\tclose\t", "… and nothing is acknowledged while not live");
    bench.Feed("presentation", "presentation-game");

    // a covered menu hidden by a game menu withdraws its coverage
    bench.Frame();
    ExpectEqual(mount.Last(), "landcommands:1\t4\t11\t2\tack\t1", "live again: covered again");
    bench.Feed("presentation", "presentation-menu");
    ExpectEqual(mount.Last(), "landcommands:1\t4\t11\t2\tack\t0", "hidden: not covered");
    bench.Feed("presentation", "presentation-game");

    // the menu closed while gameplay is still blocked: the scene has nothing to hold on to
    bench.Set("gameplay", blocked);
    bench.Set("landCommands", Json(nullptr));
    Expect(!mount.visible && mount.Last() == "presence:hidden", "no menu and no mode: the HUD goes and tells the platform host");
    bench.Feed("gameplay", "gameplay-explore");
    Expect(mount.visible && mount.Last() == "presence:shown", "explore again: the HUD is back and says so");

    // a menu only the engine can draw stays the engine's: no sheet, no coverage, an error
    bench.Set("landCommands", Json(nullptr));
    mount.commands.clear();
    bench.Feed("landCommands", "land-commands-unknown-kind");
    bench.Frame();
    Expect(mount.shown.sheet.empty() && !mount.shown.modal && !mount.Has("move") && mount.commands.empty() && !mount.errors.empty() &&
               mount.errors.back().find("not drawable here") != std::string::npos && !bench.hud.Back(),
           "an undrawable menu: nothing is claimed, the engine's own menu stays, and that is reported");
}

// --- the whole runtime ------------------------------------------------------------------

class NoRenderer final : public Rml::RenderInterface
{
  public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override
    {
        return 1;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override
    {
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override
    {
    }
    Rml::TextureHandle LoadTexture(Rml::Vector2i &, const Rml::String &) override
    {
        return 0;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override
    {
        return 1;
    }
    void ReleaseTexture(Rml::TextureHandle) override
    {
    }
    void EnableScissorRegion(bool) override
    {
    }
    void SetScissorRegion(Rml::Rectanglei) override
    {
    }
};

class NoBackend final : public RenderBackend
{
  public:
    Rml::RenderInterface &Interface() override
    {
        return renderer_;
    }
    void SetViewport(int, int) override
    {
    }
    void BeginFrame(bool) override
    {
    }
    void EndFrame() override
    {
    }

  private:
    NoRenderer renderer_;
};

struct Runtime
{
    std::unique_ptr<FileSource> files;
    NoBackend backend;
    std::unique_ptr<Host> host;
    std::vector<std::string> commands;
    double clock = 0.0;

    explicit Runtime(const std::string &pack)
    {
        files = DirectoryFiles(pack);
        Config config;
        config.files = files.get();
        config.backend = &backend;
        config.width = 800; // density 1: pixels are dp
        config.height = 400;
        config.clock = [this] { return clock; };
        config.on_action = [this](const Action &action) { commands.push_back(EngineCommand(action)); };
        std::string error;
        host = Host::Create(config, &error);
        Expect(host != nullptr, "the runtime starts with the test pack: " + error);
    }
    void Feed(const std::string &channel, const std::string &file)
    {
        Expect(host->SetState(channel, Text(file)), "state " + file + " is accepted");
    }
    void Frame(double seconds = 1.0 / 60.0)
    {
        clock += seconds;
        host->Frame();
    }
    void Explore()
    {
        Feed("presentation", "presentation-game");
        Feed("gameplay", "gameplay-explore");
        Feed("combat", "combat-explore");
        Feed("vitals", "vitals");
        Feed("worldInfo", "world-info");
        Feed("contextAction", "context-door");
        Feed("landCommands", "land-commands-idle");
        Frame();
        Frame();
    }
    bool Sent(const std::string &command) const
    {
        for (const std::string &sent : commands)
            if (sent == command)
                return true;
        return false;
    }
    std::string Last() const
    {
        return commands.empty() ? std::string() : commands.back();
    }
};

static void CheckTouches(const std::string &pack)
{
    Runtime run(pack);
    if (!run.host)
        return;
    Host &host = *run.host;
    Expect(!host.WantsTouch(400, 200) && !host.WantsBack(), "nothing shown: every touch is the game's");
    run.Explore();

    // The test layout (800 × 400): stick at 40…140 × 270…370, weapon at 728…780 × 338…390,
    // context at 716…780 × 256…320, hero at 682…730 × 10…58, vitals at 10…210 × 10…50.
    Expect(host.WantsTouch(90, 320), "the stick is the UI's");
    Expect(host.WantsTouch(750, 360) && host.WantsTouch(740, 290) && host.WantsTouch(700, 30), "buttons are the UI's");
    Expect(!host.WantsTouch(400, 200) && !host.WantsTouch(500, 380), "the free scene is the game's");
    Expect(!host.WantsTouch(100, 30) && !host.WantsTouch(550, 20), "the bars and the location line show, they do not take touches");
    Expect(!host.WantsTouch(240, 320) && !host.WantsTouch(20, 320), "just beside the stick is the game's");

    // two pointers at once: the stick held, a button tapped
    host.Touch(TouchPhase::Down, 1, 90, 320);
    host.Touch(TouchPhase::Move, 1, 90, 280);
    run.Frame();
    Expect(run.Last() == "move:1\ttown:42\t3\t1\t1\t0\t1000", "the stick moves the hero forward: " + run.Last());
    Expect(host.WantsTouch(750, 360) && !host.WantsTouch(400, 200), "a held stick changes nothing for the other pointers");
    host.Touch(TouchPhase::Down, 2, 750, 360);
    host.Touch(TouchPhase::Up, 2, 750, 360);
    Expect(run.Sent("combat:1\t7\t5\t1\tweapon\ttap"), "a second finger taps the weapon while the stick is held");
    run.Frame(0.12);
    Expect(run.Last() == "move:1\ttown:42\t3\t2\t1\t0\t1000", "the stick is still held (heartbeat): " + run.Last());

    // the platform takes the gestures away
    host.CancelInput();
    Expect(run.Sent("move:1\ttown:42\t3\t3\t0\t0\t0"), "cancelled input releases the stick");
    const size_t count = run.commands.size();
    host.Touch(TouchPhase::Move, 1, 90, 300);
    host.Touch(TouchPhase::Up, 1, 90, 300);
    Expect(run.commands.size() == count, "the cancelled pointer is over");

    // combat: a held block and a swiped strike, each with its own finger
    run.Feed("gameplay", "gameplay-melee");
    run.Feed("combat", "combat-melee");
    run.Frame();
    run.Frame();
    Expect(host.WantsTouch(672, 282) && host.WantsTouch(756, 198), "strike and block are the UI's");
    run.commands.clear();
    host.Touch(TouchPhase::Down, 1, 756, 198);
    Expect(run.Last() == "combat:1\t8\t6\t1\tblock\tdown", "block goes down with the finger: " + run.Last());
    host.Touch(TouchPhase::Move, 1, 700, 150); // far beyond a tap's slop: still held
    host.Touch(TouchPhase::Down, 2, 672, 282);
    host.Touch(TouchPhase::Move, 2, 720, 290); // right of the start: the thrust sector
    run.Frame();
    host.Touch(TouchPhase::Up, 2, 720, 290);
    Expect(run.Last() == "combat:1\t8\t6\t2\tbreak\ttap", "a swipe from the strike picks the move: " + run.Last());
    host.Touch(TouchPhase::Up, 1, 700, 150);
    Expect(run.Last() == "combat:1\t8\t6\t3\tblock\tup", "block is let go with its finger: " + run.Last());
    run.Frame();
    host.Touch(TouchPhase::Down, 2, 672, 282);
    host.Touch(TouchPhase::Up, 2, 672, 282);
    Expect(run.Last() == "combat:1\t8\t6\t4\tfast\ttap", "a plain tap on the strike is the fast attack: " + run.Last());

    // a stick held through the stance change keeps working (the element is not remounted)
    host.Touch(TouchPhase::Down, 3, 90, 320);
    host.Touch(TouchPhase::Move, 3, 130, 320);
    run.Feed("gameplay", "gameplay-explore");
    run.Feed("combat", "combat-explore");
    run.Frame(0.12);
    Expect(run.Last().rfind("move:1\ttown:42\t3\t", 0) == 0 && run.Last().find("\t1\t1000\t0") != std::string::npos, "the stick survives melee → explore: " + run.Last());
    host.Touch(TouchPhase::Up, 3, 130, 320);

    // an open sheet is modal: every touch and the back key are the UI's
    run.Feed("landCommands", "land-commands-root");
    run.Frame();
    run.Frame();
    Expect(host.WantsTouch(400, 200) && host.WantsTouch(5, 5) && host.WantsBack(), "an open sheet takes every touch and the back key");
    Expect(run.Sent("landcommands:1\t4\t11\t2\tack\t1"), "the drawn sheet is acknowledged");
    host.Touch(TouchPhase::Down, 1, 700, 20); // the first row
    host.Touch(TouchPhase::Up, 1, 700, 20);
    Expect(run.Last() == "landcommands:1\t4\t11\t2\tselectItem\tc1", "a tap on a row: " + run.Last());
    host.Touch(TouchPhase::Down, 1, 100, 300); // beside the rows
    host.Touch(TouchPhase::Up, 1, 100, 300);
    Expect(run.Last() == "landcommands:1\t4\t11\t2\tclose\t", "a tap beside the sheet closes it: " + run.Last());
    host.Back();
    Expect(run.Last() == "landcommands:1\t4\t11\t2\tclose\t", "the back key closes the root stage");
    run.Feed("landCommands", "land-commands-idle");
    Expect(host.SetState("landCommands", "null"), "the menu is gone");
    run.Frame();
    Expect(!host.WantsTouch(400, 200) && !host.WantsBack(), "the sheet closed: the scene is the game's again");

    // what covers the game, or a game in the background, leaves nothing to touch
    run.Feed("presentation", "presentation-menu");
    run.Frame();
    Expect(!host.WantsTouch(90, 320) && !host.WantsTouch(750, 360), "under a menu of the platform host the HUD is gone");
    run.Feed("presentation", "presentation-background");
    run.Frame();
    Expect(!host.WantsTouch(90, 320), "not live: drawn, but nothing is claimed");
    run.Feed("presentation", "presentation-game");
    run.Frame();
    Expect(host.WantsTouch(90, 320), "live again");

    // a mode of the platform host
    run.Feed("gameplay", "gameplay-sailing");
    run.Frame();
    Expect(!host.WantsTouch(90, 320) && HostOwnsHud("sailing") && !HostOwnsHud("explore") && !HostOwnsHud("melee"), "at sea the platform host draws the HUD");

    // safe area: the layer moves in, and the claimed places with it
    run.Feed("gameplay", "gameplay-explore");
    host.SetSafeArea(50, 0, 30, 10);
    run.Frame();
    run.Frame();
    Expect(host.WantsTouch(140, 310) && !host.WantsTouch(60, 380) && host.WantsTouch(720, 350) && !host.WantsTouch(775, 385),
           "the safe area shifts the elements and what the UI claims");

    // errors are loud
    Expect(!host.SetState("vitals", "{broken"), "malformed JSON is refused");
}

int main(int argc, char **argv)
{
    states_directory = argc > 1 ? argv[1] : "tests/states";
    const std::string pack = argc > 2 ? argv[2] : "tests/pack";
    CheckExplore();
    CheckMovement();
    CheckCombat();
    CheckSheet();
    CheckTouches(pack);
    if (failures)
        std::fprintf(stderr, "%d land HUD check(s) failed\n", failures);
    else
        std::printf("land HUD checks passed\n");
    return failures ? 1 : 0;
}
