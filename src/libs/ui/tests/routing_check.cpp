// Who gets a touch over the game, per state of the game: the whole runtime with the synthetic
// pack (hud_bench.h). For every gameplay mode and every reason the engine gives for "unknown"
// (GameplaySnapshot::blocked), with a cutscene, a full-screen video or the raised telescope on
// top, a touch beside every HUD control goes to the game (camera look, the scripts' own taps)
// unless the game is between scenes (starting, loading, no scene, an interface launched but not
// shown yet): then the UI holds it for what comes next. Interfaces (the card and dice games
// among them) and modal sheets own every touch; press_check and the scene checks cover them.
#include "hud_bench.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace
{

struct Case
{
    const char *what;
    const char *mode;
    const char *blocked; // "" for a gameplay mode
    const char *extra;   // "", "cinematic", "video", "spyglass"
    bool ui;             // the UI owns (holds) the touch
};

// Every state of the list in the engine's android_gameplay_state.hpp and the scripts' control
// groups (controls.c GetCurControlGroup): VideoPlayer, MainInterface, DialogControls,
// WorldMapControls, BattleInterfaceControls, Sailing1Pers (deck camera), Sailing3Pers,
// FightModeControls, PrimaryLand.
const Case kCases[] = {
    {"land, walking (PrimaryLand)", "explore", "", "", false},
    {"land, fighting (FightModeControls)", "melee", "", "", false},
    {"sea, ship camera (Sailing3Pers)", "sailing", "", "", false},
    {"sea, deck camera (Sailing1Pers: first person, walking the deck)", "sailing", "", "", false},
    {"sea battle", "naval", "", "", false},
    {"world map (WorldMapControls)", "worldmap", "", "", false},
    {"telescope raised", "sailing", "", "spyglass", false},
    {"telescope over a control group no mode knows", "unknown", "group", "spyglass", false},
    {"cutscene: the hero is an actor (skip by tap)", "unknown", "cutscene", "", false},
    {"cutscene with the platform's cinematic flag", "unknown", "cutscene", "cinematic", false},
    {"full-screen video (VideoPlayer, tap to skip)", "unknown", "video", "video", false},
    {"video entity before the video state", "unknown", "video", "", false},
    {"dialogue (DialogControls) before its screen", "unknown", "dialogue", "", false},
    {"command menu (BattleInterfaceControls) without a sheet", "unknown", "commands", "", false},
    {"a control group no mode knows", "unknown", "group", "", false},
    {"land without the native player", "unknown", "no-player", "", false},
    {"the app not in front", "unknown", "paused", "", false},
    {"scripts that could not be read", "unknown", "error", "", false},
    {"engine starting (the title load)", "unknown", "starting", "", true},
    {"loading screen (a save, a location)", "unknown", "loading", "", true},
    {"no land, sea or world map yet", "unknown", "no-scene", "", true},
    {"an interface launched, not shown yet (MainInterface)", "unknown", "interface", "", true},
    {"the engine said nothing yet", "unknown", "", "", false},
};

std::string GameplayJson(const Case &state, uint64_t revision)
{
    Json json = {{"schemaVersion", 1}, {"revision", revision}, {"mode", state.mode}, {"language", "russian"},
                 {"inputOwner", ""}, {"analogMovement", false}, {"movementGeneration", 0}, {"capabilities", Json::array()}};
    if (std::string(state.mode) == "unknown" && *state.blocked)
        json["blocked"] = state.blocked;
    return json.dump();
}

} // namespace

int main(int argc, char **argv)
{
    states_directory = argc > 1 ? argv[1] : "tests/states";
    const std::string pack = argc > 2 ? argv[2] : "tests/pack";
    Runtime run(pack);
    Host &host = *run.host;
    run.Feed("presentation", "presentation-game");
    uint64_t revision = 100;
    for (const Case &state : kCases)
    {
        const std::string extra = state.extra;
        if (extra == "cinematic")
        {
            Json presentation = State("presentation-game");
            presentation["cinematic"] = true;
            Expect(host.SetState("presentation", presentation.dump()), "cinematic presentation accepted");
        }
        if (extra == "video")
            Expect(host.SetState("video", R"({"fullscreen":true})"), "video state accepted");
        if (extra == "spyglass")
            run.Feed("spyglass", "spyglass-ship");
        Expect(host.SetState("gameplay", GameplayJson(state, ++revision)) || !*state.blocked, std::string("gameplay accepted: ") + state.what);
        run.Frame();
        run.Frame();
        const bool owns = host.WantsTouch(400, 200);
        Expect(owns == state.ui, std::string(state.what) + (state.ui ? ": the UI should hold the touch" : ": the game should get the touch"));
        if (state.ui)
        {
            // Held, then dropped when gameplay comes: nothing is pressed, nothing is sent.
            const size_t sent = run.commands.size();
            host.Touch(TouchPhase::Down, 1, 400, 200);
            host.Touch(TouchPhase::Up, 1, 400, 200);
            run.Frame();
            Expect(run.commands.size() == sent, std::string(state.what) + ": a held touch sends nothing");
        }
        // Back to plain gameplay for the next case.
        if (extra == "cinematic")
            run.Feed("presentation", "presentation-game");
        if (extra == "video")
            Expect(host.SetState("video", "null"), "video end accepted");
        if (extra == "spyglass")
            run.Feed("spyglass", "spyglass-down");
        const Case walking = {"", "explore", "", "", false};
        Expect(host.SetState("gameplay", GameplayJson(walking, ++revision)), "gameplay again");
        run.Frame();
        Expect(!host.WantsTouch(400, 200), std::string(state.what) + ": gameplay gives the touches back to the game");
    }
    if (failures)
        std::fprintf(stderr, "%d routing check(s) failed\n", failures);
    else
        std::printf("routing checks passed\n");
    return failures ? 1 : 0;
}
