// Host checks for the HUD core: every state channel consumer with good and bad data, the
// layout file, which scene draws which gameplay mode and when it is visible, event
// dispatch, the event log and the calls a scene gets. Synthetic states only
// (tests/states); no GL context, no pack, no game data.
#include <algorithm>
#include "../src/hud/hud.h"

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

static Json State(const std::string &name)
{
    std::ifstream file(states_directory + "/" + name + ".json", std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const Json parsed = Json::parse(text, nullptr, false);
    Expect(!parsed.is_discarded(), "state file " + name + ".json is JSON");
    return parsed;
}

struct FakeMount final : Mount
{
    View shown;
    bool visible = false;
    bool refuse = false; // the pack cannot draw the view
    bool interface = false;
    int shows = 0;
    std::vector<std::string> commands, slots, logs, errors;
    std::string language;
    double now = 0.0;

    bool ShowHud(const View &view) override
    {
        ++shows;
        if (refuse)
            return false;
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
    void SetLanguage(const std::string &game_language) override
    {
        language = game_language;
    }
    std::string Text(const std::string &key) override
    {
        return "<" + key + ">";
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
        return interface;
    }
    bool ScreenMounted() const override
    {
        return interface;
    }
    void Alert(const std::string &text) override
    {
        alerts.push_back(text);
    }
    std::vector<std::string> alerts;
    bool Measure(const std::string &, const std::string &, Placement &) const override
    {
        return false;
    }
};

// What the scene under test was told, in order.
static std::vector<std::string> calls;

class ProbeScene final : public Scene
{
  public:
    explicit ProbeScene(Hud &hud) : hud_(hud)
    {
        calls.push_back("create");
    }
    ~ProbeScene() override
    {
        calls.push_back("destroy");
    }
    void Build(const States &states, View &view, Input &input) override
    {
        calls.push_back("build");
        view.layout = "probe";
        view.elements["button"] = Json{{"label", states.context_action}};
        view.log = true;
        const Json lines = hud_.LogLines();
        if (!lines.empty())
            view.elements["log"] = Json{{"lines", lines}};
        input.On("button", "click", [this](const Event &) { hud_.Send("context", "Open"); });
        input.On("stick", "", [this](const Event &event) { hud_.Send("move", event.verb + "=" + event.value, "move"); });
        input.Ignore("button", "hover");
    }
    void Hidden(const States &) override
    {
        calls.push_back("hidden");
    }
    void Drawn(const States &) override
    {
        calls.push_back("drawn");
    }
    void Released(const States &) override
    {
        calls.push_back("released");
    }
    void Suspended(const States &) override
    {
        calls.push_back("suspended");
    }
    bool Back(const States &) override
    {
        calls.push_back("back");
        return true;
    }

  private:
    Hud &hud_;
};

static const char *const kProbeModes[] = {"explore", "melee"};
static const SceneEntry kProbe[] = {{"probe", kProbeModes, [](Hud &hud) -> std::unique_ptr<Scene> { return std::make_unique<ProbeScene>(hud); }}};

// A router is what consumers are given; it needs a surface, which only collects errors here.
struct QuietSurface final : screens::Surface
{
    void Refuse(const std::string &) override
    {
    }
    std::vector<std::string> errors;
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
    void Alert(const std::string &text) override
    {
        Toast(text);
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
    void Shown(const std::string &, const std::string &, const std::string &) const override
    {
    }
    void Log(const std::string &) override
    {
    }
    void Error(const std::string &line) override
    {
        errors.push_back(line);
    }
    double Now() const override
    {
        return 0.0;
    }
};

struct Bench
{
    FakeMount mount;
    Hud hud{mount, kProbe};
    QuietSurface surface;
    screens::Router router{surface};

    Bench()
    {
        router.AttachHud(&hud);
    }
    void Feed(const std::string &channel, const Json &state)
    {
        router.SetState(channel, state);
    }
    void Game()
    {
        Feed("presentation", State("presentation-game"));
    }
    bool LastErrorHas(const std::string &part) const
    {
        return !mount.errors.empty() && mount.errors.back().find(part) != std::string::npos;
    }
};

static void CheckConsumers()
{
    Bench bench;
    const States &states = bench.hud.Current();

    // gameplay
    bench.Feed("gameplay", State("gameplay-explore"));
    Expect(states.gameplay.mode == "explore" && states.gameplay.revision == 7 && states.gameplay.Has("ChrFightMode"), "gameplay: mode, revision, capabilities");
    Expect(states.gameplay.analog_movement && states.gameplay.input_owner == "town:42" && states.gameplay.movement_generation == 3,
           "gameplay: analog movement owner and generation");
    bench.Feed("gameplay", State("gameplay-sailing"));
    Expect(states.gameplay.mode == "sailing" && !states.gameplay.analog_movement && states.gameplay.input_owner.empty(), "gameplay: no land owner at sea");
    Json unknown = State("gameplay-explore");
    unknown["mode"] = "unknown";
    unknown["blocked"] = "loading";
    bench.Feed("gameplay", unknown);
    Expect(states.gameplay.mode == "unknown" && states.gameplay.blocked == "loading" && states.gameplay.capabilities.empty() && bench.mount.errors.empty(),
           "gameplay: the engine's own 'unknown' with its reason is no error");
    unknown.erase("blocked");
    bench.Feed("gameplay", unknown);
    Expect(!bench.mount.errors.empty(), "gameplay: 'unknown' without a reason is reported (the routing of touches depends on it)");
    bench.mount.errors.clear();
    Json strange = State("gameplay-explore");
    strange["mode"] = "flying";
    bench.Feed("gameplay", strange);
    Expect(states.gameplay.mode == "unknown" && bench.LastErrorHas("unknown gameplay mode 'flying'"), "gameplay: a mode outside the contract is reported");
    bench.Feed("gameplay", Json{{"schemaVersion", 2}, {"revision", 3}, {"mode", "explore"}});
    Expect(states.gameplay.mode == "unknown" && bench.LastErrorHas("state channel gameplay"), "gameplay: another schema is rejected");
    Json half = State("gameplay-explore");
    half["movementGeneration"] = 0;
    bench.Feed("gameplay", half);
    Expect(states.gameplay.mode == "explore" && !states.gameplay.analog_movement, "gameplay: analog movement needs a generation");

    // combat
    bench.Feed("combat", State("combat-melee-reload"));
    Expect(states.combat.known && states.combat.mode == "melee" && states.combat.Has("block") && states.combat.pistol == "unloaded" &&
               states.combat.pistol_progress == 8 && states.combat.interact_kind == "dialog",
           "combat: actions, pistol, interact kind");
    Json older = State("combat-melee");
    older["gameplayRevision"] = 5;
    bench.Feed("combat", older);
    Expect(states.combat.gameplay_revision == 8, "combat: an older revision of the same activity is not news");
    older["activityGeneration"] = 4;
    bench.Feed("combat", older);
    Expect(states.combat.activity == 6, "combat: an older activity is not news");
    const size_t quiet = bench.mount.errors.size();
    Json bad = State("combat-melee");
    bad["actions"]["teleport"] = 30;
    bench.Feed("combat", bad);
    Expect(!states.combat.known && bench.LastErrorHas("unknown action 'teleport'") && bench.mount.errors.size() == quiet + 1, "combat: an unknown action rejects the state");
    bad = State("combat-explore");
    bad["actions"]["weapon"] = 0;
    bench.Feed("combat", bad);
    Expect(!states.combat.known && bench.LastErrorHas("'weapon' lacks its generation"), "combat: an action without its generation is rejected");
    bad = State("combat-explore");
    bad["actions"]["block"] = 22;
    bench.Feed("combat", bad);
    Expect(!states.combat.known && bench.LastErrorHas("does not belong to explore"), "combat: a melee action in explore is rejected");
    bad = State("combat-melee");
    bad["pistol"] = Json{{"state", "loaded"}, {"progress", 3}};
    bench.Feed("combat", bad);
    Expect(!states.combat.known && bench.LastErrorHas("pistol state and progress"), "combat: an impossible pistol is rejected");
    bench.Feed("combat", Json("fight"));
    Expect(!states.combat.known && bench.LastErrorHas("state channel combat"), "combat: not an object");

    // vitals
    bench.Feed("vitals", State("vitals"));
    Expect(states.vitals.known && states.vitals.health == 0.84 && states.vitals.energy == 0.62 && states.vitals.loaded == 1 && states.vitals.capacity == 2,
           "vitals: bars and charges");
    bench.Feed("vitals", Json{{"health", 7.5}, {"energy", -1}, {"loaded", 9}, {"capacity", 2}});
    Expect(states.vitals.health == 1.0 && states.vitals.energy == 0.0 && states.vitals.loaded == 2, "vitals: values are clamped");
    bench.Feed("vitals", State("vitals-status"));
    Expect(states.vitals.effects == std::vector<std::string>{"poison", "healing"} && states.vitals.potions == 3 && states.vitals.officers.size() == 2 &&
               states.vitals.officers[0].face == "205" && states.vitals.officers[1].health == 0.12 && states.vitals.dash == 0.4,
           "vitals: the hero's status");
    for (const Json &bad : {Json{{"health", 1}, {"energy", 1}, {"effects", {"cursed"}}}, Json{{"health", 1}, {"energy", 1}, {"potions", -1}},
                            Json{{"health", 1}, {"energy", 1}, {"officers", {Json{{"id", 1}}}}}, Json{{"health", 1}, {"energy", 1}, {"dash", "x"}}})
    {
        const size_t errors = bench.mount.errors.size();
        bench.Feed("vitals", bad);
        Expect(bench.mount.errors.size() == errors + 1, "vitals: a malformed status is rejected aloud: " + bad.dump());
    }
    bench.Feed("vitals", nullptr);
    Expect(!states.vitals.known, "vitals: null hides the bars");
    const size_t before = bench.mount.errors.size();
    bench.Feed("vitals", Json::array());
    Expect(!states.vitals.known && bench.mount.errors.size() == before + 1, "vitals: anything else is rejected");

    // worldInfo, contextAction
    bench.Feed("worldInfo", State("world-info"));
    ExpectEqual(states.world_info, "Мартиника\nСен-Пьер\nулицы\n1 октября 1654 11:15", "worldInfo: the engine's lines");
    bench.Feed("worldInfo", Json{{"text", "x"}});
    Expect(states.world_info.empty() && bench.LastErrorHas("state channel worldInfo"), "worldInfo: only a string");
    bench.Feed("contextAction", State("context-door"));
    ExpectEqual(states.context_action, "Reload", "contextAction: the action name");
    bench.Feed("contextAction", 5);
    Expect(states.context_action.empty() && bench.LastErrorHas("state channel contextAction"), "contextAction: only a string");

    // landCommands
    bench.Feed("landCommands", State("land-commands-root"));
    const LandCommands &commands = states.land_commands;
    Expect(commands.known && commands.active && commands.complete && commands.items.size() == 7 && commands.scene == 4 && commands.revision == 11 &&
               commands.activity == 2 && commands.Find("c4") && !commands.Find("c4")->enabled,
           "landCommands: the open menu");
    bench.Feed("landCommands", State("land-commands-idle"));
    Expect(commands.revision == 11, "landCommands: an older revision is not news");
    bench.Feed("landCommands", State("land-commands-unknown-kind"));
    Expect(commands.known && commands.active && !commands.complete, "landCommands: an unknown kind leaves the menu to the engine");
    Json broken = State("land-commands-travel");
    broken["items"][0]["id"] = "bad id";
    bench.Feed("landCommands", broken);
    Expect(!commands.known && bench.LastErrorHas("state channel landCommands"), "landCommands: a bad item id rejects the state");
    broken = State("land-commands-travel");
    broken["selectedItemId"] = "nowhere";
    bench.Feed("landCommands", broken);
    Expect(!commands.known && bench.LastErrorHas("selectedItemId names no item"), "landCommands: the selection must exist");
    broken = State("land-commands-travel");
    broken["stage"] = "dreams";
    bench.Feed("landCommands", broken);
    Expect(!commands.known && bench.LastErrorHas("unknown stage 'dreams'"), "landCommands: an unknown stage is rejected");
    Json uncancellable = State("land-commands-travel");
    uncancellable["items"].erase(uncancellable["items"].size() - 1);
    bench.Feed("landCommands", uncancellable);
    Expect(commands.known && !commands.complete, "landCommands: an open menu without its cancel item is not drawable");
    bench.Feed("landCommands", nullptr);
    Expect(!commands.known, "landCommands: null = no menu");

    // presentation
    bench.Feed("presentation", State("presentation-menu"));
    Expect(states.presentation.known && states.presentation.screen == "game_menu" && states.presentation.live, "presentation: an open menu");
    bench.Feed("presentation", Json{{"screen", ""}, {"loading", false}});
    Expect(!states.presentation.known && bench.LastErrorHas("state channel presentation"), "presentation: every field is required");

    // the same complaint is reported once
    const size_t count = bench.mount.errors.size();
    bench.Feed("presentation", Json{{"screen", ""}, {"loading", false}});
    Expect(bench.mount.errors.size() == count, "a repeated complaint is not repeated");

    // a channel nobody consumes, and a router without a HUD
    bench.Feed("weather", Json::object());
    Expect(!bench.surface.errors.empty() && bench.surface.errors.back() == "state channel weather has no consumer", "an unknown channel is an error");
    QuietSurface lonely;
    screens::Router bare(lonely);
    bare.SetState("vitals", State("vitals"));
    Expect(!lonely.errors.empty() && lonely.errors.back() == "state channel vitals: this host has no HUD", "HUD states without a HUD are an error");
}

static void CheckLayout()
{
    Layout layout;
    std::string error;
    const Json file = Json::parse(R"({"version": 2, "scenes": {"probe": {"elements": [
        {"id": "button", "component": "c-hud-button", "rcss": "position: absolute;", "props": {"icon": "talk"}, "behavior": "hold", "data": {"swipe-dead": 20}},
        {"id": "plate", "component": "c-hud-plate", "children": [{"id": "bar", "component": "c-bar", "props": {"mode": "hud"}}]},
        {"id": "log", "component": "c-notice"}, {"id": "stick", "component": "c-stick"}]}}})");
    Expect(ParseLayout(file, layout, &error) && layout.count("probe") && layout["probe"].size() == 4, "layout: scenes and elements");
    const LayoutElement &button = layout["probe"][0];
    Expect(button.rcss == "position: absolute;" && button.behavior == "hold" && button.props["icon"] == "talk" && button.data["swipe-dead"] == 20,
           "layout: place, props, behaviour and data of an element");
    Expect(layout["probe"][1].children.size() == 1 && layout["probe"][1].children[0].id == "bar", "layout: children");

    View view;
    view.layout = "probe";
    view.elements["button"] = Json::object();
    view.elements["bar"] = Json{{"value", 5}};
    ExpectEqual(CheckView(layout, view), "", "a view of known elements is drawable");
    view.elements["ghost"] = Json::object();
    ExpectEqual(CheckView(layout, view), "HUD scene probe: the layout has no element 'ghost'", "an element outside the layout is an error");
    view.elements.erase("ghost");
    view.dimmed = {"nothing"};
    ExpectEqual(CheckView(layout, view), "HUD scene probe: the layout has no element 'nothing' to dim", "a dimmed element must exist");
    view.dimmed.clear();
    view.layout = "moon";
    ExpectEqual(CheckView(layout, view), "HUD: the pack has no layout scene 'moon'", "an unknown layout scene is an error");

    Expect(!ParseLayout(Json::parse(R"({"version": 1, "scenes": {}})"), layout, &error) && error.find("version 2") != std::string::npos && layout.empty(),
           "layout: another version is refused");
    Expect(!ParseLayout(Json::parse(R"({"version": 2, "scenes": {"a": {"elements": [{"id": "x"}]}}})"), layout, &error) &&
               error.find("needs 'id' and 'component'") != std::string::npos,
           "layout: an element without a component is refused");
    Expect(!ParseLayout(Json::parse(R"({"version": 2, "scenes": {"a": {"elements": [{"id": "x", "component": "c", "props": 4}]}}})"), layout, &error) &&
               error.find("'props' must be an object") != std::string::npos,
           "layout: props must be an object");
}

static void CheckScenes()
{
    calls.clear();
    {
        Bench bench;
        FakeMount &mount = bench.mount;
        Hud &hud = bench.hud;

        // Nothing is drawn before the platform said what covers the game.
        bench.Feed("gameplay", State("gameplay-explore"));
        hud.Tick();
        Expect(!hud.Visible() && !mount.visible && calls == std::vector<std::string>({"create", "hidden"}), "no presentation: the scene exists but is hidden");

        bench.Game();
        bench.Feed("contextAction", State("context-door"));
        hud.Tick();
        Expect(hud.Visible() && hud.Interactive() && mount.visible && mount.shown.layout == "probe" && mount.shown.elements["button"]["label"] == "Reload",
               "explore with a free screen: the scene is shown with its props");
        hud.Drawn();
        Expect(calls.back() == "drawn", "a drawn frame reaches the scene");
        const int shows = mount.shows;
        hud.Tick();
        Expect(mount.shows == shows, "no state change, no rebuild");

        // events
        Expect(hud.OnEvent({"button", "click", ""}) && mount.commands.back() == "context:Open" && mount.slots.back().empty(), "a bound event sends engine input");
        Expect(hud.OnEvent({"stick", "change", "3,4"}) && mount.commands.back() == "move:change=3,4" && mount.slots.back() == "move",
               "an element bound for every verb; a latest-value slot is named");
        Expect(hud.OnEvent({"button", "hover", ""}) && mount.errors.empty(), "an ignored event is fine");
        Expect(!hud.OnEvent({"button", "twist", ""}) && !mount.errors.empty() &&
                   mount.errors.back() == "HUD scene probe: event 'button twist' has no binding",
               "an unbound event is an error");

        // the event log: newest first, three at most, each for a while
        Expect(hud.Notice("one", "") && hud.Notice("two", "ok") && hud.Notice("three", "warn") && hud.Notice("four", "danger"), "the scene has a log");
        hud.Tick();
        const Json lines = mount.shown.elements["log"]["lines"];
        Expect(lines.size() == 4 && lines[0]["text"] == "four" && lines[0]["tone"] == "danger" && lines[2]["text"] == "two" &&
                   lines[2]["leaving"] == false && lines[3]["text"] == "one" && lines[3]["leaving"] == true && lines[0]["key"] != lines[1]["key"],
               "three newest lines, newest first, each with its own key; the oldest animates out");
        Expect(hud.Tick() <= 0.22, "the log asks for the frame that removes the leaving line");
        mount.now = 0.3;
        hud.Tick();
        Expect(mount.shown.elements["log"]["lines"].size() == 3, "the leaving line goes after its exit");
        Expect(hud.Tick() <= 3.0, "the log asks for the frame that ends a line (3 s for a short one)");
        mount.now = 3.0;
        hud.Tick();
        const Json leaving = mount.shown.elements["log"]["lines"];
        Expect(leaving.size() == 3 && leaving[0]["leaving"] == true && leaving[2]["leaving"] == true, "expired lines animate out first");
        mount.now = 3.3;
        hud.Tick();
        Expect(!mount.shown.elements.contains("log"), "then they are gone");

        // the back key
        Expect(hud.Back() && calls.back() == "back", "the back key reaches a visible scene");

        // what covers the game hides the HUD; the scene keeps hearing about states
        calls.clear();
        bench.Feed("presentation", State("presentation-menu"));
        hud.Tick();
        Expect(!hud.Visible() && !mount.visible && calls == std::vector<std::string>({"released", "hidden"}), "an open menu hides the HUD and releases its controls");
        Expect(!hud.Notice("lost", "") && !hud.OnEvent({"button", "click", ""}) && !hud.Back(), "a hidden HUD takes no notice, event or back key");
        bench.Game();
        hud.Tick();
        Expect(hud.Visible(), "the menu closed");
        {
            // A dialogue: the game says so before the dialogue panel's first state arrives; the HUD
            // stays until the panel covers the game, so both change in the same frame.
            Json talking = State("presentation-game");
            talking["dialogue"] = true;
            bench.Feed("presentation", talking);
            hud.Tick();
            Expect(hud.Visible(), "a dialogue without its panel yet: the HUD stays (no frame with neither)");
            mount.interface = true; // the panel's first state: the dialogue screen covers the game
            hud.Tick();
            Expect(!hud.Visible(), "the panel came: the HUD goes in the same frame");
            mount.interface = false;
            bench.Game();
            hud.Tick();
            Expect(hud.Visible(), "the dialogue ended");
            bench.Feed("presentation", talking);
            hud.Tick();
            mount.now += 0.3;
            hud.Tick();
            Expect(hud.Visible(), "still waiting for the panel");
            mount.now += 0.3;
            hud.Tick();
            Expect(!hud.Visible() && !mount.errors.empty() && mount.errors.back().find("no dialogue panel") != std::string::npos,
                   "a dialogue whose panel never comes hides the HUD after the wait, said");
            // The softlock net (#18b: the chest's late close took the owner's dialogue panel):
            // 3 s in a dialogue no screen shows ends it, loudly, once.
            const auto lost = [&] { return std::count_if(mount.commands.begin(), mount.commands.end(), [](const std::string &c) { return c.starts_with("dialogue-lost:"); }); };
            mount.now += 2.3; // 2.9 s
            hud.Tick();
            Expect(lost() == 0, "a dialogue unseen for 2.9 s: not yet");
            mount.now += 0.2; // 3.1 s
            hud.Tick();
            Expect(lost() == 1 && mount.commands.back().find("no dialogue panel for 3.1 s (panel state active") != std::string::npos,
                   "unseen for 3 s: the engine is told to end it, with what was seen: " + mount.commands.back());
            Expect(!mount.errors.empty() && mount.errors.back().starts_with("dialogue watchdog: the game is in a dialogue nobody sees"),
                   "as an error");
            Expect(mount.alerts.size() == 1 && mount.alerts.back() == "<dialogue.lost>", "and a notice for the player");
            mount.now += 10.0;
            hud.Tick();
            Expect(lost() == 1 && mount.alerts.size() == 1, "once per dialogue");
            bench.Game();
            hud.Tick();

            // The game blocked by its dialogue (dialogRun) whose panel state never came at all.
            Json blocked = State("gameplay-explore");
            blocked["mode"] = "unknown";
            blocked["blocked"] = "dialogue";
            bench.Feed("gameplay", blocked);
            hud.Tick();
            mount.now += 3.1;
            hud.Tick();
            Expect(lost() == 2 && mount.commands.back().find("panel state none, gameplay blocked 'dialogue'") != std::string::npos,
                   "a dialogue that published no panel at all is ended too: " + mount.commands.back());
            bench.Feed("gameplay", State("gameplay-explore"));
            hud.Tick();

            // A normal dialogue: its panel within 0.6 s, a long talk, a game interface it opens, its end.
            bench.Feed("presentation", talking);
            hud.Tick();
            mount.now += 0.6;
            hud.Tick();
            mount.interface = true;
            for (int i = 0; i < 20; ++i)
            {
                mount.now += 1.0;
                hud.Tick();
            }
            Json store = talking;
            store["screen"] = "store";
            bench.Feed("presentation", store);
            mount.interface = false; // the store between its close and the panel's return
            mount.now += 5.0;
            hud.Tick();
            bench.Feed("presentation", talking);
            mount.interface = true;
            mount.now += 1.0;
            hud.Tick();
            mount.interface = false;
            bench.Game();
            mount.now += 5.0;
            hud.Tick();
            Expect(lost() == 2 && mount.alerts.size() == 2, "a normal dialogue never trips it");
        }
        for (const char *cover : {"loading", "cinematic"})
        {
            Json covered = State("presentation-game");
            covered[cover] = true;
            bench.Feed("presentation", covered);
            hud.Tick();
            // A cut-scene keeps the one game-menu button; loading and a dialogue hide everything.
            if (std::string(cover) == "cinematic")
                Expect(hud.MenuOnly() && mount.shown.layout == "menu" && hud.OnEvent({"pause", "click", ""}) &&
                           mount.commands.back() == "shortcut:mainmenu",
                       "a cut-scene: only the game-menu button, which opens the game menu");
            else
                Expect(!hud.Visible(), std::string(cover) + " hides the HUD");
        }
        bench.Game();
        mount.interface = true;
        hud.Tick();
        Expect(!hud.Visible(), "an interface of the UI hides the HUD");
        mount.interface = false;
        hud.Tick();
        Expect(hud.Visible(), "the interface closed");

        // the game in the background: still drawn, nothing to press, everything given up
        calls.clear();
        bench.Feed("presentation", State("presentation-background"));
        hud.Tick();
        Expect(hud.Visible() && !hud.Interactive() && calls == std::vector<std::string>({"released", "suspended", "build"}),
               "not live: controls released, ownership given up, still drawn");
        bench.Game();
        hud.Tick();

        // the platform cancels input
        calls.clear();
        hud.Released();
        Expect(calls == std::vector<std::string>({"released"}), "cancelled input releases the scene's controls");

        // the same zone keeps its scene; another mode ends it
        calls.clear();
        bench.Feed("gameplay", State("gameplay-melee"));
        hud.Tick();
        Expect(calls == std::vector<std::string>({"build"}), "explore → melee stays in the scene");
        bench.Feed("gameplay", State("gameplay-sailing"));
        hud.Tick();
        Expect(hud.MenuOnly() && mount.shown.layout == "menu" && calls == std::vector<std::string>({"build", "released", "suspended", "destroy"}),
               "a mode of another zone ends the scene after releasing what it held; the game-menu button stays");

        // a view the pack cannot draw is not half-shown
        bench.Feed("gameplay", State("gameplay-explore"));
        mount.refuse = true;
        hud.Tick();
        Expect(!hud.Visible() && !mount.visible, "a refused view leaves the HUD hidden");
    }
    Expect(!Ported("unknown") && !Ported("flying"), "no scene draws an unknown mode");
    Expect(HostOwnsHud("unknown"), "the platform host keeps modes without a scene");
}

int main(int argc, char **argv)
{
    states_directory = argc > 1 ? argv[1] : "tests/states";
    CheckConsumers();
    CheckLayout();
    CheckScenes();
    if (failures)
        std::fprintf(stderr, "%d HUD check(s) failed\n", failures);
    else
        std::printf("HUD checks passed\n");
    return failures ? 1 : 0;
}
