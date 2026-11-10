// Host checks of the rare screens (reading cards, maps, colony, rest, trade book): admission,
// the props that matter and every action mapping. Synthetic models only (tests/models).
#include "../src/screens/screen.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>

using namespace storm::ui;
using namespace storm::ui::screens;

static int failures = 0;

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

struct FakeSurface final : Surface
{
    std::string tag;
    Json props;
    std::vector<std::string> commands;
    std::vector<std::string> errors;
    double now = 0.0;

    void Show(const std::string &shown, const Json &view) override
    {
        tag = shown;
        props = view;
    }
    void Hide() override
    {
        tag.clear();
    }
    void Send(const Action &action) override
    {
        commands.push_back(action.screen.empty() ? "host " + action.node + " " + action.value : EngineCommand(action));
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
    std::string Last() const
    {
        return commands.empty() ? std::string() : commands.back();
    }
};

static InterfaceModel Load(const std::string &directory, const char *name)
{
    std::ifstream file(directory + "/" + name, std::ios::binary);
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    InterfaceModel model;
    std::string error;
    Expect(ParseInterfaceModel(json, model, &error), std::string("parse ") + name + ": " + error);
    return model;
}

static void Without(InterfaceModel &model, const char *node)
{
    model.widgets.erase(std::remove_if(model.widgets.begin(), model.widgets.end(), [node](const Widget &w) { return w.node == node; }),
                        model.widgets.end());
}

static void Card(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "reading-map.json"));
    ExpectEqual(router.Using(), "map", "world map signal is a ported screen");
    ExpectEqual(surface.tag, "s-reading", "reading card mounted");
    Expect(surface.errors.empty() && surface.props["extras"]["rows"].empty(), "every node of the signal is known");
    const Json &card = surface.props;
    Expect(card["mode"] == "card" && card["title"] == "Вахтенный" && card["icon"] == "spyglass", "caption and icon");
    ExpectEqual(card["text"], "По курсу два торговца. Что делаем?", "news and question read as one paragraph");
    const Json &actions = card["actions"];
    Expect(actions.size() == 2 && actions[1]["id"] == "ok" && actions[1]["variant"] == "primary", "engage is the primary, right");
    Expect(actions[0]["disabled"] == true && actions[0]["variant"] == "secondary" && actions[0]["reason"] == "<reading.cannot_leave>",
           "a forced encounter shows «sail on» disabled with its reason");
    router.OnEvent({"ok", "click", ""});
    ExpectEqual(surface.Last(), "widget:map\tB_OK\t-1\tpress", "engage presses B_OK");
    const size_t sent = surface.commands.size();
    router.OnEvent({"card", "dismiss", ""});
    Expect(surface.commands.size() == sent && surface.errors.empty(), "a tap beside the card answers nothing");

    router.SetInterface(Load(models, "reading-leavebattle.json"));
    ExpectEqual(surface.props["icon"], "swords", "leaving a battle: swords");
    Expect(surface.props["actions"][0]["variant"] == "danger" && surface.props["actions"][0]["label"] == "Остаться", "the other answer is the wax seal");
    router.OnEvent({"cancel", "click", ""});
    ExpectEqual(surface.Last(), "widget:leavebattle\tB_CANCEL\t-1\tpress", "the second answer presses B_CANCEL");

    router.SetInterface(Load(models, "reading-salary.json"));
    const Json &pay = surface.props["actions"][1];
    Expect(surface.props["icon"] == "money" && surface.props["title"] == "Жалованье", "salary: caption and money");
    ExpectEqual(surface.props["text"], "Команда ждёт жалованье.", "the amount is not part of the text");
    Expect(pay["label"] == "<reading.salary_pay>" && pay["sub"] == "−1 860 песо" && pay["disabled"] == true &&
               pay["reason"] == "<reading.no_money>",
           "pay: the amount on the primary, disabled without money");
    Expect(surface.props["actions"][0]["label"] == "<reading.salary_refuse>", "refuse is named for what it does");

    InterfaceModel broken = Load(models, "reading-map.json");
    Without(broken, "B_OK");
    broken.opening = 77;
    router.SetInterface(broken);
    surface.now += 1.0;
    router.Tick();
    Expect(router.Using() == "generic" && router.Rejection() == "B_OK(button)", "a card without its answer is rejected loudly");
}

static void Frame(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "reading-frame.json"));
    Expect(surface.tag == "s-reading" && surface.props["mode"] == "frame" && surface.props["text"] == "Прошёл один час…", "interlude: its line");
    Expect(surface.errors.empty(), "every node of the interlude is known");
    router.OnEvent({"frame", "click", ""});
    ExpectEqual(surface.Last(), "back", "a tap ends the interlude the game's way (the global back)");
}

int main(int argc, char **argv)
{
    const std::string models = argc > 1 ? argv[1] : "tests/models";
    Card(models);
    Frame(models);
    for (const char *id : {"map", "dirsailenc", "leavebattle", "salary", "boal_frame"})
        Expect(!HostOwned(id), std::string(id) + " is not drawn by the platform host");
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("rare screen checks passed\n");
    return failures ? 1 : 0;
}
