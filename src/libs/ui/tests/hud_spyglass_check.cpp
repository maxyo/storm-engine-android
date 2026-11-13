// Host checks for the spyglass overlay: its channel's consumer with good and bad data, the
// card's props, and how the Hud lays it over a scene, alone where no scene is ported, and
// acknowledges drawn frames. Synthetic states only (tests/states); no GL, no pack.
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
    std::vector<std::string> commands, slots, errors;
    double now = 0.0;
    bool Measure(const std::string &, const std::string &, Placement &) const override
    {
        return false;
    }
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
        return "<" + key + ">";
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
        return now;
    }
    bool InterfaceActive() const override
    {
        return false;
    }
    bool ScreenMounted() const override
    {
        return false;
    }
    void Alert(const std::string &) override
    {
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
    void Error(const std::string &) override
    {
    }
    double Now() const override
    {
        return 0.0;
    }
};

// A land scene stand-in: the overlay must lie over whatever a scene builds.
class ProbeScene final : public Scene
{
  public:
    explicit ProbeScene(Hud &)
    {
    }
    void Build(const States &, View &view, Input &) override
    {
        view.layout = "probe";
        view.elements["button"] = Json::object();
    }
};
static const char *const kProbeModes[] = {"explore"};
static const SceneEntry kProbe[] = {{"probe", kProbeModes, [](Hud &hud) -> std::unique_ptr<Scene> { return std::make_unique<ProbeScene>(hud); }}};

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
};

static void CheckConsumer()
{
    Bench bench;
    const Spyglass &s = bench.hud.Current().spyglass;
    bench.Feed("spyglass", State("spyglass-ship"));
    Expect(s.known && s.on && s.target && s.generation == 2 && s.kind == "ship" && s.name == "Санта-Каталина" && s.ship_class == 2 &&
               s.hull == 64 && s.sails == 80 && s.cannons == 32 && s.cannons_max == 36 && s.charge == 1 && s.nation == 2 &&
               s.skills[0] == 7 && s.skills[1] == 8 && s.skills[2] == -1 && s.crew == -1 && s.captain == "Мигель де Сандоваль",
           "ship: every field, hidden ones -1");
    Expect(bench.mount.errors.empty(), "a good state is no error");
    Json bad = State("spyglass-ship");
    bad["target"]["kind"] = "boat";
    bench.Feed("spyglass", bad);
    Expect(!s.known && !s.target && !bench.mount.errors.empty() && bench.mount.errors.back().find("state channel spyglass") != std::string::npos,
           "a target kind outside the contract: rejected aloud, nothing half-read");
    bad = State("spyglass-ship");
    bad["target"]["hull"] = 140;
    bench.Feed("spyglass", bad);
    Expect(!s.known, "a hull outside 0…100 is rejected");
    bad = State("spyglass-down");
    bad["target"] = State("spyglass-fort")["target"];
    bench.Feed("spyglass", bad);
    Expect(!s.known, "a target while the telescope is down is rejected");
    bench.Feed("spyglass", State("spyglass-down"));
    Expect(s.known && !s.on && !s.target, "the telescope down");
}

static void CheckProps()
{
    FakeMount mount;
    Hud hud{mount, kProbe};
    Spyglass ship;
    ship.target = true;
    ship.kind = "ship";
    ship.name = "A";
    ship.type = "галеон";
    ship.ship_class = 2;
    ship.nation = 2;
    ship.hull = 64;
    ship.cannons = 32;
    ship.cannons_max = 36;
    ship.captain = "B";
    ship.skills[1] = 8;
    const Json p = SpyglassProps(ship, hud);
    Expect(p["empty"] == false && p["nation"] == "nation-spain" && p["sub"] == "<menu.nation_spain> · галеон, 2 <yard.class>",
           "ship: flag, nation · type, class");
    Expect(p["bars"].size() == 1 && p["bars"][0]["value"] == 64, "only the bars the telescope shows");
    Expect(p["facts"].size() == 1 && p["facts"][0]["v"] == "32 <common.of> 36", "cannons «32 из 36»");
    Expect(p["captain"]["skills"].size() == 1 && p["captain"]["skills"][0]["icon"] == "cannon" && p["unknown"] == "", "captain with the skills shown");
    Spyglass fort;
    fort.target = true;
    fort.kind = "fort";
    fort.name = "F";
    const Json f = SpyglassProps(fort, hud);
    Expect(f["fort"] == true && f["icon"] == "fort" && f["captain"].is_null() && f["unknown"] == "<spy.no_commandant>", "fort without a commandant: one line");
    Expect(SpyglassProps(Spyglass(), hud)["empty"] == true, "no target: nothing drawn");
}

static void CheckOverlay()
{
    Bench bench;
    FakeMount &mount = bench.mount;
    Hud &hud = bench.hud;
    // At sea no scene is ported on this branch: the raised telescope alone makes the HUD.
    bench.Feed("gameplay", State("gameplay-sailing"));
    bench.Feed("presentation", State("presentation-game"));
    hud.Tick();
    Expect(hud.MenuOnly(), "sailing without a scene and the telescope down: only the game-menu button");
    bench.Feed("spyglass", State("spyglass-ship"));
    hud.Tick();
    Expect(hud.Visible() && mount.shown.layout == "sea" && mount.shown.elements.empty() && mount.shown.overlay == "s-hud-spyglass" &&
               mount.shown.overlay_props["name"] == "Санта-Каталина",
           "the card alone over the sea, no scene elements");
    hud.Drawn();
    Expect(!mount.commands.empty() && mount.commands.back() == "spyglass:2\t1" && mount.slots.back() == "spyglass-ack",
           "a drawn card answers this raise of the telescope (latest-value slot)");
    Expect(std::count(mount.commands.begin(), mount.commands.end(), "hudowner:spyglass") == 1, "the HUD declares that it presents the spyglass");
    const size_t sent = mount.commands.size();
    hud.Drawn();
    mount.now += 5.0;
    hud.Drawn();
    Expect(mount.commands.size() == sent && hud.Tick() > 5.0, "said once per raise: no heartbeat, however far apart frames are");

    bench.Feed("presentation", State("presentation-menu"));
    hud.Tick();
    hud.Drawn();
    Expect(!hud.Visible() && mount.commands.back() == "spyglass:2\t0", "under a menu: hidden, and the engine hears it does not cover (it draws its own)");
    bench.Feed("presentation", State("presentation-game"));
    bench.Feed("spyglass", State("spyglass-down"));
    hud.Tick();
    Expect(hud.MenuOnly(), "the telescope down: only the game-menu button again");

    // Over a ported scene the card lies on top of its elements.
    bench.Feed("gameplay", State("gameplay-explore"));
    bench.Feed("spyglass", State("spyglass-fort"));
    hud.Tick();
    Expect(hud.Visible() && mount.shown.layout == "probe" && mount.shown.elements.contains("button") && mount.shown.overlay == "s-hud-spyglass" &&
               mount.shown.overlay_props["fort"] == true,
           "over a scene: its elements and the card");
    bench.Feed("spyglass", State("spyglass-down"));
    hud.Tick();
    Expect(hud.Visible() && mount.shown.overlay.empty(), "the telescope down: the scene stays, the card goes");
}

int main(int argc, char **argv)
{
    states_directory = argc > 1 ? argv[1] : "tests/states";
    CheckConsumer();
    CheckProps();
    CheckOverlay();
    if (failures)
        std::fprintf(stderr, "%d spyglass check(s) failed\n", failures);
    else
        std::printf("spyglass checks passed\n");
    return failures ? 1 : 0;
}
