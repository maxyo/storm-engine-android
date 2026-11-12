// Host checks for the press behaviour with the whole runtime (a renderer that draws
// nothing, the synthetic pack tests/pack plus one row component and a real font, so text
// is measured): a long press on a row whose title was cut to fit opens the tooltip with
// the full text and sends nothing; on a row whose title fits it is a plain press, also
// when it fits only stepped down to its min-size. The
// panzoom behaviour (c-map-view) reports taps in the picture's units through pan and zoom. A full-screen
// game video (state channel "video") hides the screen and passes every touch to the game.
// A tap on what is not laid out yet waits for its first frame: a document tapped before it was
// drawn, an interface the game opens (ExpectInterface) before its model arrived, and a tap
// made while the game is in no gameplay mode (title load, scene change): the next interface
// gets it, gameplay drops it.
// No GL context, no game data.
#include "runtime_stub.h"
#include "storm_ui/ui.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <unistd.h>

using namespace storm::ui;
using Json = nlohmann::json;
namespace fs = std::filesystem;

static int failures = 0;

static void Expect(bool ok, const std::string &what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

// The test pack with one more component: a row like C-ListRow (title cut to two lines,
// long press = tooltip) and an overlay document with the tooltip layer.
static fs::path MakePack(const fs::path &base, const fs::path &font)
{
    const fs::path pack = fs::temp_directory_path() / ("storm-ui-press-" + std::to_string(::getpid()));
    fs::remove_all(pack);
    fs::copy(base, pack, fs::copy_options::recursive);
    fs::copy_file(font, pack / "font.ttf");
    Json manifest = Json::parse(Read(pack / "pack.json"));
    manifest["fonts"] = Json::array({"font.ttf"});
    manifest["overlay"] = "overlay.rml";
    manifest["settings"] = {{"toastMs", 500}, {"toastLeave", 0.22}};
    Write(pack / "pack.json", manifest.dump());
    Json components = Json::parse(Read(pack / "components.json"));
    components["components"].push_back(Json::parse(R"({
        "tag": "c-row", "behaviors": ["press"],
        "props": {"title": {"type": "text", "default": ""}, "hold": {"type": "bool", "default": false}, "tip": {"type": "text", "default": ""}},
        "constants": {"holdMode": "tooltip"},
        "template": "<ui-clamp lines=\"2\" text=\"{{title}}\" style=\"display: block; width: 100%;\"/>"})"));
    // A one-line label that fits only when stepped down (ui-clamp min-size), and the same label
    // without a step-down: cut, so its long press is the tooltip.
    components["components"].push_back(Json::parse(R"({
        "tag": "c-fit", "behaviors": ["press"],
        "props": {"title": {"type": "text", "default": ""}, "least": {"type": "int", "default": 0}, "hold": {"type": "bool", "default": false}, "tip": {"type": "text", "default": ""}},
        "constants": {"holdMode": "tooltip"},
        "template": "<ui-clamp lines=\"1\" min-size=\"{{least}}\" text=\"{{title}}\" style=\"display: block; width: 100%; white-space: nowrap;\"/>"})"));
    // The toast the host shows: it comes in and goes out with an animation (as the pack's C-Notice).
    components["components"].push_back(Json::parse(R"({
        "tag": "c-notice",
        "props": {"text": {"type": "text", "default": ""}, "variant": {"type": "text", "default": ""}, "place": {"type": "text", "default": ""},
                  "tone": {"type": "text", "default": ""}, "compact": {"type": "bool", "default": false}, "leaving": {"type": "bool", "default": false}},
        "template": "<div style=\"height: 30dp;\">{{text}}</div>"})"));
    components["components"].push_back(Json::parse(R"({
        "tag": "c-pz", "behaviors": ["panzoom"],
        "props": {"base": {"type": "json", "default": [1000, 500]}, "view": {"type": "json", "default": []}},
        "template": "<div data-panzoom=\"1\" style=\"position: absolute; left: 0; top: 0; right: 0; bottom: 0;\"><div data-plane=\"1\" style=\"position: absolute;\"></div><div data-zoom=\"in\" style=\"position: absolute; right: 0; top: 0; width: 40dp; height: 40dp;\"></div></div>"})"));
    // The pause menu's screen component, reduced to its «Продолжить» (id "resume") as a row.
    components["components"].push_back(Json::parse(R"({
        "tag": "s-game-menu",
        "props": {"line": {"type": "json", "default": {}}, "resume": {"type": "json", "default": {}}, "buttons": {"type": "json", "default": []}, "confirm": {"type": "json", "default": {}}, "extras": {"type": "json", "default": []}},
        "template": "<div style=\"font-family: LatoLatin;\"><c-row id=\"resume\" style=\"display: block; position: absolute; left: 0; top: 0; width: 200dp; height: 48dp;\" title=\"Resume\"/></div>"})"));
    Write(pack / "components.json", components.dump());
    Write(pack / "overlay.rml", "<rml><head><style>body { font-family: LatoLatin; } #tooltip { display: none; } #tooltip.open { display: block; position: absolute; }"
                                " #toasts-bottom { position: absolute; left: 0; right: 0; bottom: 0; display: flex; flex-direction: column; }"
                                " @keyframes in { from { opacity: 0; } to { opacity: 1; } } @keyframes out { from { opacity: 1; max-height: 30dp; } to { opacity: 0; max-height: 0dp; } }"
                                " c-notice { display: block; animation: 0.18s cubic-out in; } c-notice.leaving { opacity: 0; max-height: 0dp; animation: 0.22s cubic-out out; }</style></head>"
                                "<body><div id=\"tooltip\"></div><div id=\"toasts-bottom\"></div></body></rml>");
    Write(pack / "rows.rml", "<rml><head><style>body { font-family: LatoLatin; font-size: 16dp; width: 800dp; height: 400dp; }"
                             " c-row { display: block; position: absolute; left: 0; width: 200dp; height: 48dp; }"
                             " c-fit { display: block; position: absolute; left: 0; width: 72dp; height: 40dp; }</style></head><body>"
                             "<c-row id=\"short\" style=\"top: 0;\" hold=\"1\" title=\"Short\"/>"
                             "<c-row id=\"long\" style=\"top: 100dp;\" hold=\"1\" title=\"A reply far too long to fit in two lines of a narrow column, so it is cut and ends in an ellipsis\"/>"
                             "<c-fit id=\"fit\" style=\"top: 200dp;\" hold=\"1\" least=\"11\" title=\"Mannschaft\"/>"
                             "<c-fit id=\"cut\" style=\"top: 300dp;\" hold=\"1\" title=\"Mannschaft\"/>"
                             "<c-pz id=\"pz\" style=\"display: block; position: absolute; left: 400dp; top: 200dp; width: 400dp; height: 200dp;\"/>"
                             "</body></rml>");
    Write(pack / "cutout.rml", "<rml><head><style>body { font-family: LatoLatin; font-size: 16dp; width: 800dp; height: 400dp; }"
                               " c-row { display: block; position: absolute; left: 0; height: 40dp; }</style></head><body>"
                               "<c-row id=\"small\" style=\"top: 200dp; width: 40dp;\" title=\"S\"/>"
                               "<c-row id=\"wide\" style=\"top: 300dp; width: 800dp;\" title=\"A row across the screen\"/>"
                               "</body></rml>");
    return pack;
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: storm_ui_press_unit <tests/pack> <font.ttf>\n");
        return 2;
    }
    const fs::path pack = MakePack(argv[1], argv[2]);
    std::unique_ptr<FileSource> files = DirectoryFiles(pack.string());
    NoBackend backend;
    std::vector<std::string> clicks;
    double clock = 0.0;
    Config config;
    config.files = files.get();
    config.backend = &backend;
    config.width = 800;
    config.height = 400;
    config.clock = [&clock] { return clock; };
    config.on_action = [&clicks](const Action &action) {
        clicks.push_back(action.node + " " + action.verb + (action.value.empty() ? "" : " " + action.value));
    };
    std::string error;
    std::unique_ptr<Host> host = Host::Create(config, &error);
    Expect(host != nullptr, "the runtime starts: " + error);
    if (!host)
        return 1;
    Expect(host->LoadDocument("rows.rml"), "the rows document loads");
    const auto frames = [&](double seconds) {
        for (double t = 0.0; t < seconds; t += 1.0 / 60.0)
        {
            clock += 1.0 / 60.0;
            host->Frame();
        }
    };
    frames(0.2);
    const auto hold = [&](float y, float x = 100) {
        clicks.clear();
        host->Touch(TouchPhase::Down, 1, x, y);
        frames(1.0);
        host->Touch(TouchPhase::Up, 1, x, y);
        frames(0.1);
    };
    hold(124);
    Expect(clicks.empty(), "a long press on a cut title opens its tooltip and sends nothing");
    hold(24);
    Expect(clicks.size() == 1 && clicks[0] == "short click", "a long press on a title that fits is a plain press");
    hold(220, 30);
    Expect(clicks.size() == 1 && clicks[0] == "fit click", "the same label with min-size steps down and fits: a plain press");
    hold(320, 30);
    Expect(clicks.empty(), "a label too wide for its box without a step-down is cut (tooltip)");

    // Panzoom: the 1000 x 500 picture fills the 400 x 200 viewport (0.4 px per unit).
    const auto tap = [&](float x, float y) {
        clicks.clear();
        host->Touch(TouchPhase::Down, 1, x, y);
        host->Touch(TouchPhase::Up, 1, x, y);
        frames(0.05);
    };
    tap(500, 300);
    Expect(clicks.size() == 1 && clicks[0] == "pz point 250.0,250.0", "a tap reports the picture point under it");
    tap(780, 220); // the zoom-in part: ×1.6 around the centre, nothing sent
    Expect(clicks.empty(), "the zoom part zooms locally and sends nothing");
    tap(600, 300);
    Expect(clicks.size() == 1 && clicks[0] == "pz point 500.0,250.0", "the centre stays under the centre after zooming");
    clicks.clear();
    host->Touch(TouchPhase::Down, 1, 600, 300);
    host->Touch(TouchPhase::Move, 1, 650, 300);
    host->Touch(TouchPhase::Move, 1, 664, 300);
    host->Touch(TouchPhase::Up, 1, 664, 300);
    frames(0.05);
    Expect(clicks.empty(), "a drag pans and sends nothing");
    tap(600, 300);
    Expect(clicks.size() == 1 && clicks[0] == "pz point 400.0,250.0", "after a 64 px pan the centre shows a point 100 units left");
    // A full-screen game video (channel "video") hides what the UI shows and gives every touch
    // to the game; the screen comes back as it was when the video ends.
    Expect(host->WantsTouch(100, 24) && host->InterfaceActive(), "a document takes the touches");
    Expect(host->SetState("video", R"({"fullscreen":true})"), "the video state is accepted");
    frames(0.05);
    Expect(!host->WantsTouch(100, 24) && !host->InterfaceActive() && !host->WantsBack(),
           "during a full-screen video the touches and the back key go to the game");
    hold(24);
    Expect(clicks.empty(), "nothing of the hidden screen is pressed during the video");
    Expect(host->SetState("video", "null"), "the video end is accepted");
    frames(0.05);
    Expect(host->WantsTouch(100, 24) && host->InterfaceActive(), "after the video the screen takes the touches again");
    Expect(!host->SetState("video", R"({"fullscreen":"yes"})"), "a malformed video state is refused aloud");
    // A toast animates in, stays (toastMs), animates out (toastLeave) and goes: frames keep
    // coming while an animation runs, and stop once everything has settled.
    frames(0.5);
    double delay = 0.0;
    Expect(!host->Prepare(delay) && delay > 0.2, "an idle screen asks for no frame");
    const auto drawing = [&](double from, double to) { // every Prepare in [from, to) s asks for a frame at once
        bool all = true;
        for (double t = from; t < to; t += 1.0 / 60.0)
        {
            double next = 1.0;
            all = host->Prepare(next) && next == 0.0 && all;
            host->Frame();
            clock += 1.0 / 60.0;
        }
        return all;
    };
    host->Toast("Saved", "ok");
    Expect(drawing(0.0, 0.15), "the toast's entrance keeps frames coming");
    frames(0.2);
    Expect(!host->Prepare(delay) && delay > 0.05 && delay < 0.5, "a shown toast asks for no frame, only a wake-up when it is due");
    frames(0.2);
    Expect(drawing(0.0, 0.18), "its exit keeps frames coming");
    frames(0.2);
    Expect(!host->Prepare(delay) && delay > 0.2, "after the exit has run the toast is gone and frames stop");

    // A document opened by the platform and tapped before any frame laid it out: the tap
    // waits for that layout and then presses what is under it.
    Expect(host->LoadDocument("rows.rml"), "the rows document loads again");
    tap(100, 24);
    Expect(clicks.size() == 1 && clicks[0] == "short click", "a tap before the document's first frame presses it after that frame");

    // The game opens an interface whose model comes seconds later (the interface loads first):
    // from the announcement the UI owns every touch, and the player's first tap waits for the
    // screen instead of reaching what was there before or the game.
    // A display cutout on the left edge (x 0..30, y 150..350): the layout runs under it; the
    // small control that falls under it moves right by the minimum (to 34), the row across the
    // screen stays where it is.
    Expect(host->LoadDocument("cutout.rml"), "the cutout document loads");
    frames(0.05);
    host->SetCutouts({Cutout{0, 150, 30, 200}});
    frames(0.05);
    tap(60, 220);
    Expect(clicks.size() == 1 && clicks[0] == "small click", "the control under the cutout moved out of it: " + (clicks.empty() ? std::string("nothing") : clicks[0]));
    tap(20, 220);
    Expect(clicks.empty(), "nothing of it is left under the cutout");
    tap(20, 320);
    Expect(clicks.size() == 1 && clicks[0] == "wide click", "the row across the screen runs under the cutout");
    tap(60, 100);
    Expect(clicks.empty(), "away from the cutout nothing moves into empty space");
    host->SetCutouts({});
    frames(0.05);
    tap(20, 220);
    Expect(clicks.size() == 1 && clicks[0] == "small click", "without the cutout the control is back at the edge");

    host.reset();
    host = Host::Create(config, &error);
    Expect(host != nullptr, "a second runtime starts: " + error);
    if (!host)
        return 1;
    frames(0.05);
    clicks.clear();
    const fs::path states = fs::path(argv[1]) / "../states";
    const std::string explore = Read(states / "gameplay-explore.json");
    Json blocked = Json::parse(explore);
    blocked["mode"] = "unknown"; // what the engine publishes while it loads or changes scene
    blocked["blocked"] = "loading";
    blocked["revision"] = 8;
    blocked["capabilities"] = Json::array();

    // Before the game is in any gameplay mode (the title load, a scene change) nothing on the
    // screen can be used: the UI owns the touches and holds them. Gameplay coming next drops them.
    Expect(!host->WantsTouch(700, 350), "before any gameplay state nothing is decided: the game keeps the touches");
    Expect(host->SetState("gameplay", blocked.dump()), "the engine loads (mode unknown, blocked loading)");
    Expect(host->WantsTouch(700, 350), "while the game loads the touches are the UI's");
    tap(700, 350);
    frames(0.5);
    Expect(host->SetState("gameplay", explore), "gameplay starts");
    frames(0.05);
    Expect(clicks.empty() && !host->WantsTouch(700, 350), "gameplay drops the held tap and the game gets the touches again");

    // A scene change (no gameplay mode again), then the interface the game opens: a tap made
    // in between waits for it and presses it.
    blocked["revision"] = 9;
    Expect(host->SetState("gameplay", blocked.dump()), "the game leaves gameplay");
    Expect(host->WantsTouch(700, 350), "between scenes the touches are the UI's again");
    tap(100, 24);
    frames(0.5);
    Expect(clicks.empty(), "nothing receives the tap between scenes");
    host->ExpectInterface();
    const std::string first_menu = Read(fs::path(argv[1]) / "../models/game-menu.json");
    Expect(host->SetInterface(first_menu), "the game menu model arrives after the scene change");
    frames(0.05);
    Expect(clicks.size() == 1 && clicks[0] == " back",
           "the tap made between scenes presses the menu that came next: " + (clicks.empty() ? std::string("nothing") : clicks[0]));
    clicks.clear();
    host->CloseInterface();
    Expect(host->SetState("gameplay", explore), "gameplay again");
    frames(0.05);

    Expect(!host->WantsTouch(700, 350), "nothing of the UI over the game: its touches go to the game");
    host->ExpectInterface();
    Expect(host->WantsTouch(700, 350) && host->WantsTouch(100, 24), "an interface being opened owns every touch at once");
    tap(100, 24);
    frames(0.5);
    Expect(clicks.empty(), "the tap waits while the model is still to come");
    const std::string menu = Read(fs::path(argv[1]) / "../models/game-menu.json");
    Expect(host->SetInterface(menu), "the game menu model arrives");
    frames(0.05);
    // «Продолжить» resumes through the engine's back (menu_game.cpp).
    Expect(clicks.size() == 1 && clicks[0] == " back",
           "then the held tap presses the screen's «Продолжить»: " + (clicks.empty() ? std::string("nothing") : clicks[0]));
    clicks.clear();
    host->CloseInterface();
    frames(0.05);
    host->ExpectInterface();
    tap(100, 24);
    host->CloseInterface();
    frames(0.05);
    Expect(clicks.empty() && !host->WantsTouch(700, 350), "an opening withdrawn before its model drops its held tap and gives the touches back");
    host.reset();
    fs::remove_all(pack);
    if (failures)
        std::fprintf(stderr, "%d press check(s) failed\n", failures);
    else
        std::printf("press checks passed\n");
    return failures ? 1 : 0;
}
