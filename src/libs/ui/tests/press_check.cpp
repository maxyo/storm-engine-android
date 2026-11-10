// Host checks for the press behaviour with the whole runtime (a renderer that draws
// nothing, the synthetic pack tests/pack plus one row component and a real font, so text
// is measured): a long press on a row whose title was cut to fit opens the tooltip with
// the full text and sends nothing; on a row whose title fits it is a plain press.
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
    Write(pack / "components.json", components.dump());
    Write(pack / "overlay.rml", "<rml><head><style>body { font-family: LatoLatin; } #tooltip { display: none; } #tooltip.open { display: block; position: absolute; }</style></head>"
                                "<body><div id=\"tooltip\"></div></body></rml>");
    Write(pack / "rows.rml", "<rml><head><style>body { font-family: LatoLatin; font-size: 16dp; width: 800dp; height: 400dp; }"
                             " c-row { display: block; position: absolute; left: 0; width: 200dp; height: 48dp; }</style></head><body>"
                             "<c-row id=\"short\" style=\"top: 0;\" hold=\"1\" title=\"Short\"/>"
                             "<c-row id=\"long\" style=\"top: 100dp;\" hold=\"1\" title=\"A reply far too long to fit in two lines of a narrow column, so it is cut and ends in an ellipsis\"/>"
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
    config.on_action = [&clicks](const Action &action) { clicks.push_back(action.node + " " + action.verb); };
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
    host.reset();
    fs::remove_all(pack);
    if (failures)
        std::fprintf(stderr, "%d press check(s) failed\n", failures);
    else
        std::printf("press checks passed\n");
    return failures ? 1 : 0;
}
