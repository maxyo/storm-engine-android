// Host checks for slot content of nested components with the whole runtime (a renderer
// that draws nothing, the synthetic pack tests/pack plus two components): when the parent
// template passes content into a nested component only in some states (a row's inline
// button that appears later), the in-place patch adds it, keeps it, and removes it again,
// also when the nested template has its slot only `t-if children`.
// No GL context, no game data, no text (no font needed).
#include "runtime_stub.h"
#include "storm_ui/ui.h"

#include <nlohmann/json.hpp>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>

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

// c-slot-row: a row like C-ListRow, its slot exists only `t-if children`, between other parts.
// c-slot-list: a parent that passes a button into the row only while `button` is set.
static fs::path MakePack(const fs::path &base)
{
    const fs::path pack = fs::temp_directory_path() / ("storm-ui-slot-" + std::to_string(::getpid()));
    fs::remove_all(pack);
    fs::copy(base, pack, fs::copy_options::recursive);
    Json components = Json::parse(Read(pack / "components.json"));
    components["components"].push_back(Json::parse(R"({
        "tag": "c-slot-row", "props": {"title": {"type": "text", "default": ""}},
        "template": "<div class=\"title\" data-title=\"{{title}}\"></div><t-if test=\"children\"><div class=\"lr-slot\" slot=\"1\"></div></t-if><div class=\"tail\"><span></span></div>"})"));
    components["components"].push_back(Json::parse(R"({
        "tag": "c-slot-list", "props": {"button": {"type": "text", "default": ""}},
        "template": "<c-slot-row id=\"row\" title=\"Set\"><t-if test=\"button\"><div id=\"inline\" class=\"button\" data-label=\"{{button}}\"></div></t-if></c-slot-row>"})"));
    Write(pack / "components.json", components.dump());
    Write(pack / "slots.rml", "<rml><head></head><body><c-slot-list id=\"list\"/></body></rml>");
    return pack;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: storm_ui_slot_unit <tests/pack>\n");
        return 2;
    }
    const fs::path pack = MakePack(argv[1]);
    std::unique_ptr<FileSource> files = DirectoryFiles(pack.string());
    NoBackend backend;
    double clock = 0.0;
    Config config;
    config.files = files.get();
    config.backend = &backend;
    config.width = 800;
    config.height = 400;
    config.clock = [&clock] { return clock; };
    std::string error;
    std::unique_ptr<Host> host = Host::Create(config, &error);
    Expect(host != nullptr, "the runtime starts: " + error);
    if (!host)
        return 1;
    Expect(host->LoadDocument("slots.rml"), "the slots document loads");
    const auto frame = [&] {
        clock += 1.0 / 60.0;
        host->Frame();
    };
    Rml::ElementDocument *document = Rml::GetContext("storm-ui")->GetDocument(0);
    // The row as the player sees it: the classes of its children in order, the slot's child count.
    const auto row = [&] {
        std::string out;
        Rml::Element *element = document->GetElementById("row");
        for (int i = 0; element && i < element->GetNumChildren(); ++i)
        {
            Rml::Element *child = element->GetChild(i);
            out += (out.empty() ? "" : " ") + child->GetClassNames();
            if (child->HasAttribute("slot"))
                out += "[" + std::to_string(child->GetNumChildren()) + "]";
        }
        return out;
    };
    const auto show = [&](const std::string &button) {
        Expect(host->SetProps("list", Json{{"button", button}}.dump()), "props of the list are set");
        frame();
        frame();
    };
    frame();
    Expect(row() == "title tail", "without content the row has no slot: " + row());
    show("Choose files");
    Rml::Element *inline_button = document->GetElementById("inline");
    Expect(inline_button && inline_button->GetAttribute<Rml::String>("data-label", "") == "Choose files", "a button that appears later is added by the patch: " + row());
    Expect(row().find("title lr-slot[") == 0 && row().find(" tail") != std::string::npos, "the slot sits between the title and the tail: " + row());
    Rml::Element *tail = document->GetElementById("row")->GetChild(2);
    Expect(tail && tail->GetClassNames() == "tail" && tail->GetNumChildren() == 1, "the tail keeps only its own children: " + row());
    show("Import");
    Expect(document->GetElementById("inline") == inline_button && inline_button->GetAttribute<Rml::String>("data-label", "") == "Import",
           "a changed button is patched in place: " + row());
    show("");
    Expect(row() == "title tail" && !document->GetElementById("inline"), "a button that goes away takes its slot with it: " + row());
    show("Import");
    Expect(document->GetElementById("inline") && row().find("lr-slot[") != std::string::npos, "and it comes back: " + row());
    host.reset();
    fs::remove_all(pack);
    if (failures)
        std::fprintf(stderr, "%d slot check(s) failed\n", failures);
    else
        std::printf("slot checks passed\n");
    return failures ? 1 : 0;
}
