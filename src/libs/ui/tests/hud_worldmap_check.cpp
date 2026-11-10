// Host checks for the world-map HUD: the consumer of channel "worldMap" and the scene alone
// (what each synthetic state shows, which engine input each event sends, what is
// acknowledged as drawn), then the touches of the runtime with the small synthetic pack.
#include "hud_bench.h"

static bool Sent(const FakeMount &mount, const std::string &command)
{
    for (const std::string &sent : mount.commands)
        if (sent == command)
            return true;
    return false;
}

static void CheckConsumer()
{
    Bench bench;
    const WorldMap &map = bench.hud.Current().world_map;
    bench.Feed("worldMap", "world-map");
    Expect(map.known && map.available && map.revision == 41 && map.activity == 2 && map.mask == 7 && map.fleet.size() == 2 && map.commands.size() == 1,
           "worldMap: the frame is read");
    Expect(map.food_days == 18 && map.rum_days == 4 && map.flag == 2 && std::abs(map.speed - 0.7) < 1e-9 && map.angles && map.coordinates == "--",
           "worldMap: the instruments");
    size_t errors = 0;
    const auto rejected = [&](Json state, const std::string &what) {
        bench.Set("worldMap", state);
        Expect(bench.mount.errors.size() == ++errors && !bench.hud.Current().world_map.known, what + " is rejected aloud");
    };
    Json bad = State("world-map");
    bad["commands"][0]["id"] = "EnterToMoon";
    rejected(bad, "worldMap: an unknown command");
    bad = State("world-map");
    bad["windForce"] = 3;
    rejected(bad, "worldMap: a wind force out of range");
    bad = State("world-map");
    bad.erase("ships");
    rejected(bad, "worldMap: a frame without its fleet list");
    rejected(Json(4), "worldMap: a number");
    Json late = State("world-map");
    bench.Feed("worldMap", "world-map-encounter");
    late["revision"] = 40;
    bench.Set("worldMap", late);
    Expect(bench.hud.Current().world_map.revision == 44, "worldMap: an older revision is not news");
}

static void CheckScene()
{
    Bench bench;
    FakeMount &mount = bench.mount;
    bench.Feed("gameplay", "gameplay-worldmap");
    bench.Feed("worldMap", "world-map");
    const Json &shown = mount.shown.elements;
    Expect(mount.visible && mount.shown.layout == "world-map", "the world-map layout");
    for (const char *id : {"fleet", "pause", "camera", "helm", "sail", "compass", "context"})
        Expect(mount.Has(id), std::string("the world map shows ") + id);
    Expect(!mount.Has("details") && !mount.Has("attack") && !mount.Has("storm"), "no details, no encounter buttons");
    Expect(shown["fleet"]["ships"][0]["icon"] == "ship-frigate" && shown["fleet"]["ships"][1]["hp"] == 74, "the fleet");
    Expect(shown["compass"]["course"] == 130 && shown["compass"]["windFrom"] == 140 && shown["compass"]["wind"] == 82 && shown["compass"]["time"] == "12:31" &&
               shown["compass"]["days"] == "18" && shown["compass"]["sun"] == "188" && shown["sail"]["value"] == 70,
           "compass and lever: " + shown["compass"].dump());
    Expect(shown["context"]["icon"] == "sea-exit" && shown["context"]["variant"] == "primary", "going to sea in the corner");

    // nothing is covered or activated before it is drawn
    bench.Press("context");
    Expect(!Sent(mount, "worldmap:41\t2\tactivate\tEnterToSea"), "a command that was not drawn cannot be tapped");
    bench.Frame();
    Expect(mount.Last() == "worldmap:41\t2\tack\t7" && mount.slots.back() == "worldmap-ack", "the drawn instruments, fleet and command are acknowledged: " + mount.Last());
    const size_t acked = mount.commands.size();
    bench.Frame(0.1);
    Expect(mount.commands.size() == acked, "nothing is repeated before its time");
    bench.Frame(0.2);
    Expect(mount.commands.size() == acked + 1 && mount.Last() == "worldmap:41\t2\tack\t7", "the coverage lease is renewed");

    // the compass shows the details beside it
    bench.Press("compass", "toggle", "1");
    Expect(mount.Has("details") && shown["compass"]["expanded"] == true && shown["detail-rows"]["items"].size() == 7, "a tap on the compass: the details");
    const Json &rows = shown["detail-rows"]["items"];
    Expect(rows[0]["v"] == "130°" && rows[1]["v"] == "hud.map.windFrom · 82%" && rows[6]["v"] == "hud.map.noSextant", "the details read the compass in words: " + rows.dump());
    bench.Press("compass", "toggle", "0");
    Expect(!mount.Has("details"), "a second tap hides them");

    // helm, lever, camera are held
    bench.Press("helm", "change", "70");
    Expect(mount.Last() == "hold:9\tWMapShipTurnRight\t1", "the helm: " + mount.Last());
    bench.Press("helm", "change", "-70");
    Expect(Sent(mount, "hold:9\tWMapShipTurnRight\t0") && mount.Last() == "hold:9\tWMapShipTurnLeft\t1", "over to the other side: " + mount.Last());
    bench.Press("helm", "change", "0");
    bench.Press("sail", "hold", "up");
    Expect(mount.Last() == "hold:9\tWMapShipSailUp\t1", "lever up: " + mount.Last());
    bench.Press("sail", "hold", "down");
    Expect(Sent(mount, "hold:9\tWMapShipSailUp\t0") && mount.Last() == "hold:9\tWMapShipSailDown\t1", "lever down: " + mount.Last());
    bench.Press("sail", "hold", "");
    Expect(mount.Last() == "hold:9\tWMapShipSailDown\t0", "lever let go: " + mount.Last());
    bench.Press("camera", "down");
    bench.Press("camera", "up");
    Expect(Sent(mount, "hold:9\tWMapCameraSwitch\t1") && mount.Last() == "hold:9\tWMapCameraSwitch\t0", "the camera button");
    bench.Press("pause");
    Expect(mount.Last() == "shortcut:mainmenu", "menu: " + mount.Last());

    // a command, once
    bench.Press("context");
    Expect(mount.Last() == "worldmap:41\t2\tactivate\tEnterToSea", "going to sea: " + mount.Last());
    const size_t sent = mount.commands.size();
    bench.Press("context");
    Expect(mount.commands.size() == sent, "a second tap waits for the engine");

    // an encounter: three commands in their places
    bench.Feed("worldMap", "world-map-encounter");
    Expect(mount.Has("attack") && mount.Has("storm") && shown["attack"]["variant"] == "combat" && shown["storm"]["icon"] == "wind", "the encounter's buttons");
    Expect(shown["compass"]["daysTone"] == "danger" && shown["compass"]["sun"] == "", "two days of food warn; no sun at night");
    bench.Frame();
    Expect(mount.Last() == "worldmap:44\t2\tack\t7", "the new revision is acknowledged: " + mount.Last());
    bench.Press("attack");
    Expect(mount.Last() == "worldmap:44\t2\tactivate\tEnterToAttack", "attack: " + mount.Last());

    // leaving the game takes the coverage back
    mount.commands.clear();
    bench.Feed("presentation", "presentation-menu");
    Expect(Sent(mount, "worldmap:44\t2\tack\t0"), "under a menu the engine's instruments come back");
}

int main(int argc, char **argv)
{
    states_directory = argc > 1 ? argv[1] : "tests/states";
    CheckConsumer();
    CheckScene();
    if (failures)
        std::fprintf(stderr, "%d world-map HUD check(s) failed\n", failures);
    else
        std::printf("world-map HUD checks passed\n");
    return failures ? 1 : 0;
}
