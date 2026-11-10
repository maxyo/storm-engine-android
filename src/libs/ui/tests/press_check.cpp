// Host checks for the press behaviour with the whole runtime (a renderer that draws
// nothing, the synthetic pack tests/pack plus one row component and a real font, so text
// is measured): a long press on a row whose title was cut to fit opens the tooltip with
// the full text and sends nothing; on a row whose title fits it is a plain press. The
// panzoom behaviour (c-map-view) reports taps in the picture's units through pan and zoom.
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
    Write(pack / "pack.json", manifest.dump());
    Json components = Json::parse(Read(pack / "components.json"));
    components["components"].push_back(Json::parse(R"({
        "tag": "c-row", "behaviors": ["press"],
        "props": {"title": {"type": "text", "default": ""}, "hold": {"type": "bool", "default": false}, "tip": {"type": "text", "default": ""}},
        "constants": {"holdMode": "tooltip"},
        "template": "<ui-clamp lines=\"2\" text=\"{{title}}\" style=\"display: block; width: 100%;\"/>"})"));
    components["components"].push_back(Json::parse(R"({
        "tag": "c-pz", "behaviors": ["panzoom"],
        "props": {"base": {"type": "json", "default": [1000, 500]}, "view": {"type": "json", "default": []}},
        "template": "<div data-panzoom=\"1\" style=\"position: absolute; left: 0; top: 0; right: 0; bottom: 0;\"><div data-plane=\"1\" style=\"position: absolute;\"></div><div data-zoom=\"in\" style=\"position: absolute; right: 0; top: 0; width: 40dp; height: 40dp;\"></div></div>"})"));
    Write(pack / "components.json", components.dump());
    Write(pack / "overlay.rml", "<rml><head><style>body { font-family: LatoLatin; } #tooltip { display: none; } #tooltip.open { display: block; position: absolute; }</style></head>"
                                "<body><div id=\"tooltip\"></div></body></rml>");
    Write(pack / "rows.rml", "<rml><head><style>body { font-family: LatoLatin; font-size: 16dp; width: 800dp; height: 400dp; }"
                             " c-row { display: block; position: absolute; left: 0; width: 200dp; height: 48dp; }</style></head><body>"
                             "<c-row id=\"short\" style=\"top: 0;\" hold=\"1\" title=\"Short\"/>"
                             "<c-row id=\"long\" style=\"top: 100dp;\" hold=\"1\" title=\"A reply far too long to fit in two lines of a narrow column, so it is cut and ends in an ellipsis\"/>"
                             "<c-pz id=\"pz\" style=\"display: block; position: absolute; left: 400dp; top: 200dp; width: 400dp; height: 200dp;\"/>"
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
    const auto hold = [&](float y) {
        clicks.clear();
        host->Touch(TouchPhase::Down, 1, 100, y);
        frames(1.0);
        host->Touch(TouchPhase::Up, 1, 100, y);
        frames(0.1);
    };
    hold(124);
    Expect(clicks.empty(), "a long press on a cut title opens its tooltip and sends nothing");
    hold(24);
    Expect(clicks.size() == 1 && clicks[0] == "short click", "a long press on a title that fits is a plain press");

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
    host.reset();
    fs::remove_all(pack);
    if (failures)
        std::fprintf(stderr, "%d press check(s) failed\n", failures);
    else
        std::printf("press checks passed\n");
    return failures ? 1 : 0;
}
