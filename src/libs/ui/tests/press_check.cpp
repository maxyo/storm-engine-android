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

#include <RmlUi/Core.h>
#include <RmlUi/Core/Transform.h>
#include <RmlUi/Core/TransformPrimitive.h>

#include <nlohmann/json.hpp>

#include <cmath>
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
        "tag": "c-notice", "behaviors": ["toast"],
        "props": {"text": {"type": "text", "default": ""}, "variant": {"type": "text", "default": ""}, "place": {"type": "text", "default": ""},
                  "tone": {"type": "text", "default": ""}, "compact": {"type": "bool", "default": false}, "leaving": {"type": "bool", "default": false}},
        "template": "<div data-toast=\"toast\" style=\"height: 30dp;\">{{text}}</div>"})"));
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
    Write(pack / "overlay.rml", "<rml><head><style>body { font-family: LatoLatin; width: 100%; height: 100%; pointer-events: none; z-index: 1000; } #tooltip { display: none; } #tooltip.open { display: block; position: absolute; }"
                                " #toasts { position: absolute; left: 0; right: 0; top: 0; display: flex; flex-direction: column; }"
                                " @keyframes in { from { opacity: 0; } to { opacity: 1; } } @keyframes out { from { opacity: 1; max-height: 30dp; } to { opacity: 0; max-height: 0dp; } }"
                                " c-notice { display: block; width: 100%; animation: 0.18s cubic-out in; } c-notice [data-toast] { display: block; pointer-events: auto; } c-notice.leaving { opacity: 0; max-height: 0dp; animation: 0.22s cubic-out out; }</style></head>"
                                "<body><div id=\"tooltip\"></div><div id=\"toasts\"></div></body></rml>");
    Write(pack / "rows.rml", "<rml><head><style>body { font-family: LatoLatin; font-size: 16dp; width: 800dp; height: 400dp; }"
                             " c-row { display: block; position: absolute; left: 0; width: 200dp; height: 48dp; }"
                             " c-fit { display: block; position: absolute; left: 0; width: 72dp; height: 40dp; }</style></head><body>"
                             "<c-row id=\"short\" style=\"top: 0;\" hold=\"1\" title=\"Short\"/>"
                             "<c-row id=\"long\" style=\"top: 100dp;\" hold=\"1\" title=\"A reply far too long to fit in two lines of a narrow column, so it is cut and ends in an ellipsis\"/>"
                             "<c-fit id=\"fit\" style=\"top: 200dp;\" hold=\"1\" least=\"11\" title=\"Mannschaft\"/>"
                             "<c-fit id=\"cut\" style=\"top: 300dp;\" hold=\"1\" title=\"Mannschaft\"/>"
                             "<c-pz id=\"pz\" style=\"display: block; position: absolute; left: 400dp; top: 200dp; width: 400dp; height: 200dp;\"/>"
                             "</body></rml>");
    // A store-like component: a list of 20 rows, the deal's amount, its total and the buy button.
    components["components"].push_back(Json::parse(R"({
        "tag": "c-store",
        "props": {"rows": {"type": "json", "default": []}, "amount": {"type": "int", "default": 0}, "total": {"type": "text", "default": ""}},
        "template": "<div id=\"list\"><t-for each=\"rows\" as=\"r\"><div class=\"row{{ r.sel ? ' sel' : '' }}\" data-value=\"{{r.id}}\"><span class=\"name\">{{r.name}}</span><span class=\"held\">{{r.held}}</span></div></t-for></div><div id=\"amount\">{{amount}}</div><div id=\"total\">{{total}}</div><c-row id=\"buy\" title=\"{{total}}\" style=\"display: block; width: 200dp; height: 48dp;\"/>"})"));
    // A screen that passes its deal on: the amount goes to the store whole, the label is its own text.
    components["components"].push_back(Json::parse(R"({
        "tag": "c-outer",
        "props": {"deal": {"type": "json", "default": null}, "rows": {"type": "json", "default": []}, "label": {"type": "text", "default": ""}},
        "template": "<div class=\"label\">{{label}}</div><t-if test=\"deal\"><c-store id=\"inner\" rows=\"{{ json(rows) }}\" props=\"{{ json(deal) }}\"/></t-if>"})"));
    // constants.boxSize: the size prop is the box (an icon measured without formatting its content).
    components["components"].push_back(Json::parse(R"({
        "tag": "c-sized", "constants": {"boxSize": "size"},
        "props": {"size": {"type": "int", "default": 24}},
        "template": "<div style=\"display: inline-block; width: 10dp; height: 10dp;\"></div>"})"));
    // A slider (behaviour range): the template places the knob at the value.
    components["components"].push_back(Json::parse(R"({
        "tag": "c-slide", "behaviors": ["range"],
        "props": {"value": {"type": "int", "default": 0}, "min": {"type": "int", "default": 0}, "max": {"type": "int", "default": 100},
                  "active": {"type": "bool", "default": false}},
        "template": "<div data-range=\"value\" style=\"position: absolute; left: 100dp; top: 100dp; width: 400dp; height: 40dp;\"><div id=\"knob\" data-range-knob=\"\" style=\"position: absolute; left: {{ (value - min) / (max - min) * 100 }}%; top: 0; width: 0; height: 40dp;\"></div></div>"})"));
    Write(pack / "slide.rml", "<rml><head><style>body { font-family: LatoLatin; font-size: 16dp; width: 800dp; height: 400dp; }</style></head><body>"
                              "<c-slide id=\"coarse\" max=\"4\"/></body></rml>");
    // A slider whose value's texts and marks are live parts (data-live-*): a drag needs no render
    // of the control until its shape (here: above or below the middle) changes.
    components["components"].push_back(Json::parse(R"({
        "tag": "c-live-slide", "behaviors": ["range"],
        "props": {"value": {"type": "int", "default": 0}, "min": {"type": "int", "default": 0}, "max": {"type": "int", "default": 100},
                  "active": {"type": "bool", "default": false}},
        "template": "<div data-live-shape=\"value > 50 ? 'high' : 'low'\"><div data-range=\"value\" style=\"position: absolute; left: 100dp; top: 100dp; width: 400dp; height: 40dp;\"><div id=\"live-knob\" data-range-knob=\"\" data-live-style=\"'left: ' + value + '%;'\" style=\"position: absolute; left: {{value}}%; top: 0; width: 0; height: 40dp;\"></div></div><div id=\"live-num\" data-live-text=\"value\">{{value}}</div><t-if test=\"value > 50\"><div id=\"live-high\">high</div></t-if></div>"})"));
    Write(pack / "live-slide.rml", "<rml><head><style>body { font-family: LatoLatin; font-size: 16dp; width: 800dp; height: 400dp; }</style></head><body>"
                                   "<c-live-slide id=\"live\" value=\"10\"/></body></rml>");
    Write(pack / "slide-fine.rml", "<rml><head><style>body { font-family: LatoLatin; font-size: 16dp; width: 800dp; height: 400dp; }</style></head><body>"
                                   "<c-slide id=\"fine\" max=\"4000\"/></body></rml>");
    Write(pack / "components.json", components.dump());
    {
        Json rows = Json::array();
        for (int i = 0; i < 20; ++i)
            rows.push_back({{"id", i}, {"name", "Goods " + std::to_string(i)}, {"held", i * 10}, {"sel", i == 3}});
        Write(pack / "store.rml", "<rml><head><style>body { font-family: LatoLatin; font-size: 16dp; width: 800dp; height: 400dp; }</style></head><body>"
                                  "<c-store id=\"store\" amount=\"1\" total=\"Buy 1\" rows='" + rows.dump() + "'/>"
                                  "<div style=\"display: flex;\"><c-sized id=\"sized\" size=\"20\"/></div></body></rml>");
        Write(pack / "bar.rml", "<rml><head><style>body { font-family: LatoLatin; font-size: 16dp; width: 800dp; height: 400dp; }</style></head><body>"
                                "<div id=\"bar\" rmlui-layout-boundary=\"\" style=\"display: flex; flex-direction: row; width: 800dp; height: 40dp;\">"
                                "<div id=\"stat\" rmlui-layout-boundary=\"\" style=\"flex: 0 0 auto;\">189</div><div id=\"rest\" style=\"flex: 1 1 0;\">rest</div>"
                                "</div></body></rml>");
        Write(pack / "outer.rml", "<rml><head><style>body { font-family: LatoLatin; font-size: 16dp; width: 800dp; height: 400dp; }</style></head><body>"
                                  "<c-outer id=\"outer\" label=\"Store\" deal='{\"amount\": 1, \"total\": \"Buy 1\"}' rows='" + rows.dump() + "'/></body></rml>");
    }
    Write(pack / "cutout-bar.rml", "<rml><head><style>body { font-family: LatoLatin; font-size: 16dp; width: 800dp; height: 400dp; }"
                                   " #bar { display: flex; flex-direction: row; position: absolute; left: 0; top: 0; width: 800dp; height: 40dp; }"
                                   " c-row { display: block; flex: 0 0 auto; width: 40dp; height: 40dp; }"
                                   " #meter { display: flex; flex-direction: row; position: absolute; left: 0; top: 100dp; width: 200dp; height: 40dp; }"
                                   " #track { display: block; flex: 1 1 0; height: 40dp; }</style></head><body>"
                                   "<div id=\"bar\"><c-row id=\"first\" title=\"A\"/><c-row id=\"second\" title=\"B\"/></div>"
                                   "<div id=\"meter\"><c-row id=\"icon\" title=\"I\"/><div id=\"track\"></div><c-row id=\"end\" title=\"90\"/></div></body></rml>");
    Write(pack / "cutout.rml", "<rml><head><style>body { font-family: LatoLatin; font-size: 16dp; width: 800dp; height: 400dp; }"
                               " c-row { display: block; position: absolute; left: 0; height: 40dp; }</style></head><body>"
                               "<c-row id=\"small\" style=\"top: 200dp; width: 40dp;\" title=\"S\"/>"
                               "<c-row id=\"wide\" style=\"top: 300dp; width: 800dp;\" title=\"A row across the screen\"/>"
                               "<div style=\"display: block; position: absolute; left: 0; top: 150dp; width: 200dp; height: 40dp; overflow-y: auto;\">"
                               "<c-row id=\"listed\" style=\"position: relative; top: 0; width: 40dp;\" title=\"L\"/><div style=\"display: block; height: 200dp;\"></div></div>"
                               "</body></rml>");
    // The dialogue panel's layout (C-DialoguePanel): the speech box stops 44 dp above the
    // bottom for the «Дальше ›» line, the replies (data-reserve) reach the bottom and take that
    // line's place on the final page.
    Write(pack / "pages.rml", "<rml><head><style>body { font-family: LatoLatin; font-size: 16dp; width: 800dp; height: 400dp; }"
                              " .panel { position: absolute; left: 0; top: 0; width: 400dp; height: 200dp; }"
                              " ui-pages { display: block; position: absolute; left: 0; top: 0; right: 0; bottom: 44dp; line-height: 26dp; white-space: nowrap; overflow: hidden; }"
                              " .replies { position: absolute; left: 0; right: 0; bottom: 0; }"
                              " .replies c-row { display: block; height: 48dp; }"
                              " ui-pages.more ~ .replies { visibility: hidden; pointer-events: none; }"
                              " .next { position: absolute; right: 0; bottom: 0; width: 80dp; height: 44dp; } ui-pages.last ~ .next { display: none; }</style></head><body>"
                              "<div class=\"panel\"><ui-pages text=\"Welcome aboard, captain! The yard has timber, pitch and canvas in plenty, and my men have been idle since the storm. What will it be today?\" page=\"0\" mode=\"paged\"/>"
                              "<div class=\"replies\" data-reserve=\"1\"><c-row id=\"reply\" title=\"Repair my ship\"/><c-row id=\"leave\" title=\"Goodbye\"/></div>"
                              "<c-row id=\"next\" class=\"next\" title=\"Next\"/></div>"
                              "</body></rml>");
    // The dialogue sheet (s-dialogue): it grows (data-grow) up to its max-height before the
    // speech pages; here 200 dp high in a 400 dp screen, at most 88 % (352 dp).
    const std::string sheet =
        "<rml><head><style>body { font-family: LatoLatin; font-size: 16dp; width: 800dp; height: 400dp; }"
        " .sheet { position: absolute; left: 0; bottom: 0; width: 400dp; height: 200dp; max-height: 88%; }"
        " ui-pages { display: block; position: absolute; left: 0; top: 0; right: 0; bottom: 44dp; line-height: 26dp; white-space: nowrap; overflow: hidden; }"
        " .replies { position: absolute; left: 0; right: 0; bottom: 0; } .replies c-row { display: block; height: 48dp; }"
        " ui-pages.more ~ .replies { visibility: hidden; pointer-events: none; }"
        " .next { position: absolute; right: 0; bottom: 0; width: 80dp; height: 44dp; } ui-pages.last ~ .next { display: none; }</style></head><body>"
        "<div class=\"sheet\" data-grow=\"1\"><ui-pages text=\"TEXT\" page=\"0\" mode=\"paged\"/>"
        "<div class=\"replies\" data-reserve=\"1\"><c-row id=\"reply\" title=\"Repair my ship\"/><c-row id=\"leave\" title=\"Goodbye\"/></div>"
        "<c-row id=\"next\" class=\"next\" title=\"Next\"/></div></body></rml>";
    const auto with = [&](const std::string &text) {
        std::string doc = sheet;
        doc.replace(doc.find("TEXT"), 4, text);
        return doc;
    };
    const std::string greeting = "Welcome aboard, captain! The yard has timber, pitch and canvas in plenty, and my men have been idle since "
                                 "the storm. We can mend the hull, change the sails and rigging, or sell you a sloop or a brig that "
                                 "stand at the pier. What will it be today?";
    Write(pack / "grow.rml", with(greeting));
    std::string story;
    for (int i = 0; i < 8; ++i)
        story += greeting + " ";
    Write(pack / "grow-long.rml", with(story));
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
    int sounds = 0; // the interface clicks a press start asked for (Action screen "sound")
    double clock = 0.0;
    Config config;
    config.files = files.get();
    config.backend = &backend;
    config.width = 800;
    config.height = 400;
    config.clock = [&clock] { return clock; };
    config.on_action = [&clicks, &sounds](const Action &action) {
        if (action.screen == "sound")
        {
            ++sounds;
            return;
        }
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
    // A quick pan (1200 px/s, a sample every 1/120 s, a frame every second one): the picture
    // follows 1:1 with nothing laid out, then coasts on, frame by frame, to the right edge
    // of the zoomed picture (640 px wide in the 400 px viewport).
    {
        double t = clock;
        float x = 700.0f;
        int layouts = 0, coasting = 0;
        host->Touch(TouchPhase::Down, 1, x, 300, t);
        for (int i = 1; i <= 12; ++i)
        {
            t += 1.0 / 120.0;
            x -= 10.0f;
            host->Touch(TouchPhase::Move, 1, x, 300, t);
            if (i % 2 == 0)
            {
                clock = t;
                host->Frame();
                layouts += i > 2 ? host->LastFrameStats().layout_passes : 0;
            }
        }
        host->Touch(TouchPhase::Up, 1, x, 300, t);
        double next = 1.0;
        while (coasting < 120 && host->Prepare(next) && next == 0.0)
        {
            clock += 1.0 / 60.0;
            host->Frame();
            layouts += host->LastFrameStats().layout_passes;
            ++coasting;
        }
        Expect(layouts == 0, "a pan and its coast lay nothing out (" + std::to_string(layouts) + " layout passes)");
        Expect(coasting >= 3 && coasting < 120, "the released picture coasts on frame by frame and stops (" + std::to_string(coasting) + " frames)");
        tap(600, 300);
        Expect(clicks.size() == 1 && clicks[0] == "pz point 687.5,250.0", "it came to rest at the right edge of the picture (" +
                                                                               (clicks.empty() ? std::string("-") : clicks[0]) + ")");
    }
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
    // A toast animates in, stays (hud::ToastSeconds: 3 s for a short one), animates out (toastLeave) and goes: frames keep
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
    Expect(!host->Prepare(delay) && delay > 2.5 && delay < 2.7, "a shown toast asks for no frame, only a wake-up when it is due");
    // a finger on it stops its time; lifting it gives back what was left
    Expect(host->WantsTouch(10, 8), "the toast takes a finger");
    host->Touch(TouchPhase::Down, 1, 10, 8);
    frames(5.0);
    Expect(!host->Prepare(delay) && delay > 3.0, "held: no wake-up is due while the finger stays: " + std::to_string(delay));
    host->Touch(TouchPhase::Up, 1, 10, 8);
    frames(0.05);
    Expect(!host->Prepare(delay) && delay > 2.4 && delay < 2.7, "released: the time it had left runs on: " + std::to_string(delay));
    frames(2.6);
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
    // A bar of several controls under a cutout (a header: back, then the next control right
    // after it) is laid out beside the cutout: both stay apart and take their taps where they are
    // drawn. Moved one by one, the first landed on the second, which then took its taps.
    Expect(host->LoadDocument("cutout-bar.rml"), "the bar document loads");
    frames(0.05);
    host->SetCutouts({Cutout{0, 0, 60, 60}});
    frames(0.05);
    tap(84, 20); // 64..104: where the first control is now
    Expect(clicks.size() == 1 && clicks[0] == "first click", "the first control of the bar is beside the cutout and takes the tap there: " + (clicks.empty() ? std::string("nothing") : clicks[0]));
    tap(124, 20); // 104..144: the second, after it
    Expect(clicks.size() == 1 && clicks[0] == "second click", "the second follows it, not under it: " + (clicks.empty() ? std::string("nothing") : clicks[0]));
    tap(20, 20);
    Expect(clicks.empty(), "nothing of the bar is left under the cutout");
    // A row of a set width (a hero card's health bar, width 100%) beside a cutout keeps its width:
    // the padding comes out of it and its flexible part gives way, so its last part stays inside.
    host->SetCutouts({Cutout{0, 90, 60, 60}});
    frames(0.05);
    tap(180, 120); // 160..200: the row's end, where the row ends
    Expect(clicks.size() == 1 && clicks[0] == "end click", "the end of a set-width row stays inside it: " + (clicks.empty() ? std::string("nothing") : clicks[0]));
    tap(84, 120);
    Expect(clicks.size() == 1 && clicks[0] == "icon click", "its first part beside the cutout: " + (clicks.empty() ? std::string("nothing") : clicks[0]));
    host->SetCutouts({});
    frames(0.05);
    tap(20, 20);
    Expect(clicks.size() == 1 && clicks[0] == "first click", "without the cutout the bar begins at the edge again");

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
    // The same small control inside a scrolling list stays where the list puts it: scrolled content ignores the cutout.
    tap(20, 165);
    Expect(clicks.size() == 1 && clicks[0] == "listed click", "a row of a scrolling list stays under the cutout: " + (clicks.empty() ? std::string("nothing") : clicks[0]));
    tap(60, 165);
    Expect(clicks.empty(), "and was not moved out beside it");
    host->SetCutouts({});
    frames(0.05);
    tap(20, 220);
    Expect(clicks.size() == 1 && clicks[0] == "small click", "without the cutout the control is back at the edge");

    // A greeting that fits beside the replies stays on one page: the replies show at once (the
    // pager measures the part of the speech box they cover, not their whole height).
    Expect(host->LoadDocument("pages.rml"), "the paged speech loads");
    frames(0.2);
    tap(40, 130);
    Expect(clicks.size() == 1 && clicks[0] == "reply click", "a three-line greeting and two replies: one page, the replies answer at once: " +
                                                                  (clicks.empty() ? std::string("nothing") : clicks[0]));
    tap(360, 180);
    Expect(clicks.size() == 1 && clicks[0] == "leave click", "the last reply sits where «Дальше» would be, and it is not there");

    // A greeting too long for the designed sheet beside its replies: the sheet grows and it
    // stays one page; a speech longer than even the tallest sheet pages.
    Expect(host->LoadDocument("grow.rml"), "the growing sheet loads");
    frames(0.3);
    tap(40, 330);
    Expect(clicks.size() == 1 && clicks[0] == "reply click", "the sheet grew: the greeting and its replies on one page: " +
                                                                  (clicks.empty() ? std::string("nothing") : clicks[0]));
    Expect(host->LoadDocument("grow-long.rml"), "the long speech loads");
    frames(0.3);
    tap(360, 380);
    Expect(clicks.size() == 1 && clicks[0] == "next click", "a speech beyond the tallest sheet pages: «Дальше» is there");
    tap(40, 330);
    Expect(clicks.empty(), "… and the replies wait");

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

    // One tap whose finger trembles (the platform delivers every sample with its time, the
    // historical ones of a batched move too): one press, one click, one action. On a game
    // interface the press start asks for the interface's click; a plain document (the
    // launcher's screens) has none.
    const auto trembling_tap = [&](float x, float y, int samples) {
        clicks.clear();
        sounds = 0;
        double t = clock;
        host->Touch(TouchPhase::Down, 1, x, y, t);
        for (int i = 0; i < samples; ++i)
            host->Touch(TouchPhase::Move, 1, x + float(i % 3), y + float(i % 2), t += 0.004);
        frames(1.0 / 60.0);
        host->Touch(TouchPhase::Up, 1, x + 1, y, t + 0.004);
        frames(0.05);
    };
    host->ExpectInterface();
    Expect(host->SetInterface(menu), "the game menu opens again");
    frames(0.05);
    trembling_tap(100, 24, 12);
    Expect(sounds == 1 && clicks.size() == 1 && clicks[0] == " back",
           "a tap with 12 move samples on a game interface: one click, one action (" + std::to_string(sounds) + " clicks, " +
               std::to_string(clicks.size()) + " actions)");
    host->CloseInterface();
    frames(0.05);
    Expect(host->LoadDocument("rows.rml"), "the rows document loads for the trembling tap");
    frames(0.05);
    trembling_tap(100, 24, 12);
    Expect(sounds == 0 && clicks.size() == 1 && clicks[0] == "short click",
           "a tap with 12 move samples on a plain document: one action, no click (" + std::to_string(clicks.size()) + " actions)");


    // One value changes, only what shows it changes: a new amount sets the amount's text, the
    // total's text and the button's title (which rebuilds the button), not one of the 20 rows.
    Expect(host->LoadDocument("store.rml"), "the store document loads");
    frames(0.1);
    {
        Rml::Element *list = nullptr;
        std::vector<Rml::Element *> rows_before;
        const auto rows_of = [&] {
            std::vector<Rml::Element *> rows;
            if (list)
                for (int i = 0; i < list->GetNumChildren(); ++i)
                    rows.push_back(list->GetChild(i));
            return rows;
        };
        const auto find = [](const char *id) -> Rml::Element * {
            Rml::Context *context = Rml::GetContext("storm-ui");
            for (int i = 0; context && i < context->GetNumDocuments(); ++i)
                if (Rml::Element *found = context->GetDocument(i)->GetElementById(id))
                    return found;
            return nullptr;
        };
        list = find("list");
        Expect(list && list->GetNumChildren() == 20, "the store lists 20 rows");
        rows_before = rows_of();
        Expect(host->SetProps("store", R"({"amount": 2, "total": "Buy 2"})"), "the amount changes");
        clock += 1.0 / 60.0;
        host->Frame();
        const FrameStats changed = host->LastFrameStats();
        Expect(changed.touched > 0 && changed.touched <= 4 && changed.patch_fallbacks == 0,
               "a new amount touches the amount, the total and the button only (" + std::to_string(changed.touched) + " elements, " +
                   std::to_string(changed.patch_fallbacks) + " whole levels)");
        Expect(find("list") == list && rows_of() == rows_before, "the rows are the same elements");
        Json rows = Json::array();
        for (int i = 0; i < 20; ++i)
            rows.push_back({{"id", i}, {"name", "Goods " + std::to_string(i)}, {"held", i == 3 ? 32 : i * 10}, {"sel", i == 3}});
        Expect(host->SetProps("store", Json{{"rows", rows}}.dump()), "one row's hold changes");
        clock += 1.0 / 60.0;
        host->Frame();
        const FrameStats row = host->LastFrameStats();
        Expect(row.touched == 1 && rows_of() == rows_before, "one row's figure touches that figure only (" + std::to_string(row.touched) + " elements)");
        // The same values again: the render is the shown one, nothing to draw.
        Expect(host->SetProps("store", R"({"amount": 2, "total": "Buy 2"})"), "the same amount again");
        double delay = 0.0;
        clock += 1.0 / 60.0;
        host->Prepare(delay);
        host->Frame();
        Expect(host->LastFrameStats().touched == 0 && host->LastFrameStats().laid_out == 0, "the same values touch and lay out nothing");
        Rml::Element *sized = find("sized");
        Expect(sized && std::abs(sized->GetBox().GetSize(Rml::BoxArea::Border).x - 20.0f) < 0.01f &&
                   std::abs(sized->GetBox().GetSize(Rml::BoxArea::Border).y - 20.0f) < 0.01f,
               "constants.boxSize makes the size prop the box: 20 x 20");
        Expect(host->SetProps("sized", R"({"size": 32})"), "the size changes");
        frames(0.05);
        Expect(std::abs(sized->GetBox().GetSize(Rml::BoxArea::Border).x - 32.0f) < 0.01f, "… and the box follows: 32");
    }

    // A boundary sized by its contents in a flex row (a header's figure): a text of the same width
    // lays out only it; a wider one changes the row around it, which is laid out again.
    Expect(host->LoadDocument("bar.rml"), "the bar document loads");
    frames(0.1);
    {
        Rml::Context *context = Rml::GetContext("storm-ui");
        const auto find = [context](const char *id) -> Rml::Element * {
            for (int i = 0; context && i < context->GetNumDocuments(); ++i)
                if (Rml::Element *found = context->GetDocument(i)->GetElementById(id))
                    return found;
            return nullptr;
        };
        Rml::Element *stat = find("stat"), *rest = find("rest");
        const float narrow = stat ? stat->GetBox().GetSize().x : 0.f, rest_at = rest ? rest->GetAbsoluteLeft() : 0.f;
        Expect(stat && rest && narrow > 0.f, "the bar is laid out");
        if (stat && rest)
        {
            stat->SetInnerRML("WWWWWWW 1 514");
            frames(0.05);
            Expect(stat->GetBox().GetSize().x > narrow + 10.f && rest->GetAbsoluteLeft() > rest_at + 10.f,
                   "a wider figure widens its box and moves what follows (" + std::to_string(stat->GetBox().GetSize().x) + ")");
            stat->SetInnerRML("189");
            frames(0.05);
            Expect(std::abs(stat->GetBox().GetSize().x - narrow) < 0.01f && std::abs(rest->GetAbsoluteLeft() - rest_at) < 0.01f,
                   "back to the first figure: back to the first layout");
        }
    }

    // A value a screen only passes on (a drag of the deal's amount): the screen does not render its
    // template, the store renders once and touches the amount, the total and the button only.
    Expect(host->LoadDocument("outer.rml"), "the outer document loads");
    frames(0.1);
    {
        Expect(host->SetProps("outer", R"({"deal": {"amount": 2, "total": "Buy 2"}})"), "the deal's amount changes");
        clock += 1.0 / 60.0;
        host->Frame();
        const FrameStats moved = host->LastFrameStats();
        Expect(moved.forwarded >= 1 && moved.rebuilds == 1 && moved.touched <= 5 && moved.laid_out <= 30 && moved.patch_fallbacks == 0,
               "a passed-on value: the screen forwards it (" + std::to_string(moved.forwarded) + "), only the store renders (" +
                   std::to_string(moved.rebuilds) + " renders), " + std::to_string(moved.touched) + " elements touched, " +
                   std::to_string(moved.laid_out) + " boxes");
        Expect(host->SetProps("outer", R"({"label": "Shop"})"), "the screen's own text changes");
        clock += 1.0 / 60.0;
        host->Frame();
        const FrameStats own = host->LastFrameStats();
        Expect(own.forwarded == 0 && own.rebuilds == 1, "its own text: the screen renders, the store does not");
        Expect(host->SetProps("outer", R"({"deal": {"amount": 3, "total": "Buy 3"}})"), "the amount after the screen rendered");
        clock += 1.0 / 60.0;
        host->Frame();
        Rml::Context *context = Rml::GetContext("storm-ui");
        Rml::Element *amount = nullptr;
        for (int i = 0; context && i < context->GetNumDocuments() && !amount; ++i)
            amount = context->GetDocument(i)->GetElementById("amount");
        Expect(amount && amount->GetInnerRML() == "3", "the store shows the amount passed on last");
        Expect(host->SetProps("outer", R"({"deal": null})"), "the deal closes");
        frames(0.05);
        amount = nullptr;
        for (int i = 0; context && i < context->GetNumDocuments() && !amount; ++i)
            amount = context->GetDocument(i)->GetElementById("amount");
        Expect(!amount, "a deal whose truth changed renders the screen: the store is gone");
    }

    // A slider's knob stays under the finger: the value has steps (0…4 over 400 px: 100 px each),
    // the finger has none. The template places the knob at the value; the behaviour moves it on
    // to the finger with a transform and takes that away on release.
    // The live slider: the drag moves change its number and knob without a render; the middle
    // (its shape) renders once; the update stays far under half a millisecond.
    Expect(host->LoadDocument("live-slide.rml"), "the live slider loads");
    frames(0.1);
    {
        Rml::Context *context = Rml::GetContext("storm-ui");
        const auto find = [context](const char *id) -> Rml::Element * {
            for (int i = 0; context && i < context->GetNumDocuments(); ++i)
                if (Rml::Element *found = context->GetDocument(i)->GetElementById(id))
                    return found;
            return nullptr;
        };
        // 400 px track at 100..500 (density 1): value 10 at 140, 30 at 220.
        host->Touch(TouchPhase::Down, 1, 140, 120, clock);
        frames(1.0 / 60.0);
        int renders = 0, refreshed = 0;
        double worst = 0.0;
        for (float x = 150; x <= 300; x += 10) // values 12 … 50: below the middle
        {
            host->Touch(TouchPhase::Move, 1, x, 120, clock);
            clock += 1.0 / 60.0;
            host->Frame();
            const FrameStats moved = host->LastFrameStats();
            renders += moved.rebuilds;
            refreshed += moved.refreshed;
            worst = std::max(worst, moved.update_ms);
        }
        Rml::Element *num = find("live-num");
        Expect(renders == 0 && refreshed >= 10, "a drag below the middle renders the control " + std::to_string(renders) + " times (" +
                                                    std::to_string(refreshed) + " live refreshes)");
        Expect(num && num->GetInnerRML() == "50", "the number follows the finger: " + (num ? std::string(num->GetInnerRML()) : std::string("none")));
        Expect(worst < 0.5, "a drag move's update is under 0.5 ms (worst " + std::to_string(worst) + " ms)");
        host->Touch(TouchPhase::Move, 1, 340, 120, clock); // 60: above the middle
        clock += 1.0 / 60.0;
        host->Frame();
        Expect(host->LastFrameStats().rebuilds == 1 && find("live-high") != nullptr, "crossing the middle renders once: its branch shows");
        host->Touch(TouchPhase::Up, 1, 340, 120, clock);
        frames(0.05);
        num = find("live-num");
        Expect(num && num->GetInnerRML() == "60", "after the release the render shows the value");
    }

    Expect(host->LoadDocument("slide.rml"), "the slider document loads");
    frames(0.1);
    {
        const auto find = [](const char *id) -> Rml::Element * {
            Rml::Context *context = Rml::GetContext("storm-ui");
            for (int i = 0; context && i < context->GetNumDocuments(); ++i)
                if (Rml::Element *found = context->GetDocument(i)->GetElementById(id))
                    return found;
            return nullptr;
        };
        // Where the knob is drawn: its laid-out place plus its transform.
        const auto knob_x = [&]() -> float {
            Rml::Element *knob = find("knob");
            if (!knob)
                return -1.0f;
            float x = knob->GetAbsoluteOffset(Rml::BoxArea::Border).x;
            if (const Rml::Property *property = knob->GetLocalProperty(Rml::PropertyId::Transform))
                if (const Rml::TransformPtr transform = property->Get<Rml::TransformPtr>())
                    for (const Rml::TransformPrimitive &primitive : transform->GetPrimitives())
                        if (primitive.type == Rml::TransformPrimitive::TRANSLATEX)
                            x += primitive.translate_x.values[0].number;
            return x;
        };
        clicks.clear();
        host->Touch(TouchPhase::Down, 1, 100, 120);
        frames(0.05);
        float worst = 0.0f;
        int layouts = 0;
        for (float x = 101.0f; x <= 500.0f; x += 7.0f)
        {
            host->Touch(TouchPhase::Move, 1, x, 120);
            clock += 1.0 / 60.0;
            host->Frame();
            worst = std::max(worst, std::abs(knob_x() - x));
            // A frame in which the value did not change moves the knob without a rebuild or a layout.
            if (host->LastFrameStats().rebuilds == 0)
                layouts += host->LastFrameStats().layout_passes;
        }
        Expect(worst < 0.01f, "the knob is drawn under the finger in every frame of the drag (worst " + std::to_string(worst) + " px off)");
        Expect(clicks.size() == 4, "while the value still changes in steps: 4 changes over the drag (" + std::to_string(clicks.size()) + ")");
        Expect(layouts == 0, "a move between two values lays nothing out");
        host->Touch(TouchPhase::Up, 1, 493, 120);
        frames(0.05);
        Expect(std::abs(knob_x() - 500.0f) < 0.01f, "released, the knob is at its value (4 of 4: 500 px), not where the finger left it");
    }

    // A platform that gives the touches once per frame sends the moves as samples and has each
    // pointer placed for the frame's time (Host::PlaceTouches). A finger moving evenly (1000 px/s,
    // a sample every 7 ms) under frames every 1/120 s: placed at its newest sample the slider
    // moved 7 or 14 px per frame; placed for the frame's time it moves the same 8.33 px in each.
    Expect(host->LoadDocument("slide-fine.rml"), "the fine slider loads");
    frames(0.1);
    {
        const auto drag = [&](bool placed) {
            clicks.clear();
            const double start = clock;
            double sample = start;
            host->Touch(TouchPhase::Down, 1, 120, 120, start);
            std::vector<float> values;
            for (int frame = 1; frame <= 30; ++frame)
            {
                clock = start + frame / 120.0;
                while (sample + 0.007 <= clock)
                {
                    sample += 0.007;
                    host->Touch(placed ? TouchPhase::Sample : TouchPhase::Move, 1, 120.0f + float((sample - start) * 1000.0), 120, sample);
                }
                if (placed)
                    host->PlaceTouches(clock);
                host->Frame();
                if (!clicks.empty())
                    values.push_back(std::stof(clicks.back().substr(clicks.back().rfind(' ') + 1)) / 10.0f); // 10 units per px
                else
                    values.push_back(values.empty() ? 20.0f : values.back());
            }
            host->Touch(TouchPhase::Up, 1, 400, 120, clock);
            frames(0.05);
            float least = 1e9f, most = 0.0f;
            for (size_t i = 3; i < values.size(); ++i) // from the third frame: two samples are there
            {
                least = std::min(least, values[i] - values[i - 1]);
                most = std::max(most, values[i] - values[i - 1]);
            }
            return std::pair<float, float>(least, most);
        };
        const auto [raw_least, raw_most] = drag(false);
        Expect(raw_most - raw_least > 6.0f, "at its newest sample the slider moves in uneven steps (" + std::to_string(raw_least) + " to " +
                                                std::to_string(raw_most) + " px per frame): the check measures something");
        const auto [least, most] = drag(true);
        Expect(least > 8.1f && most < 8.6f, "placed for the frame's time it moves evenly: 8.33 px in every frame (" + std::to_string(least) + " to " +
                                                std::to_string(most) + ")");
    }

    host.reset();
    fs::remove_all(pack);
    if (failures)
        std::fprintf(stderr, "%d press check(s) failed\n", failures);
    else
        std::printf("press checks passed\n");
    return failures ? 1 : 0;
}
