// Host checks of the menu screens (menu_*.cpp): admission with loud rejection, the props
// that matter and every action mapping. Synthetic models only (tests/models).
#include "../src/screens/menu.h"
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
        commands.push_back(EngineCommand(action));
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

static void GameMenu(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "game-menu.json"));
    ExpectEqual(router.Using(), "game_menu", "pause menu is a ported screen");
    ExpectEqual(surface.tag, "s-game-menu", "pause menu mounted");
    Expect(surface.errors.empty() && surface.props["extras"]["rows"].empty(), "every node of the pause menu is known");
    ExpectEqual(surface.props["resume"]["label"], "Продолжить", "resume is the game's button");
    Expect(surface.props["buttons"].size() == 4 && surface.props["buttons"][3]["id"] == "exit", "grid: save, load, settings, exit (no disabled updates)");
    Expect(surface.props["confirm"].is_null(), "no question yet");
    const char *const ids[][2] = {{"resume", "MB_RESUME"}, {"save", "MB_SAVE"}, {"load", "MB_LOAD"}, {"options", "MB_OPTIONS"}, {"exit", "MB_EXITGAME"}};
    for (const auto &[id, node] : ids)
    {
        router.OnEvent({id, "click", ""});
        ExpectEqual(surface.Last(), std::string("widget:game_menu\t") + node + "\t-1\tpress", std::string(id) + " presses its button");
    }
    Expect(!FromTitle(), "the pause menu means a game is in progress");

    router.SetInterface(Load(models, "game-menu-exit.json"));
    const Json confirm = surface.props["confirm"];
    Expect(confirm["preset"] == "danger" && confirm["title"] == "<menu.exit_question>" && confirm["text"] == "Выйти?",
           "exit question: danger dialog keeping the game's text");
    router.OnEvent({"confirm-confirm", "click", ""});
    ExpectEqual(surface.Last(), "widget:game_menu\tCONFIRM_EXIT_YES\t-1\tpress", "«Выйти» answers yes");
    router.OnEvent({"confirm", "dismiss", ""});
    ExpectEqual(surface.Last(), "widget:game_menu\tCONFIRM_EXIT_NO\t-1\tpress", "the scrim answers no");
    router.OnEvent({"confirm-cancel", "click", ""});
    ExpectEqual(surface.Last(), "widget:game_menu\tCONFIRM_EXIT_NO\t-1\tpress", "«Отмена» answers no");

    InterfaceModel broken = Load(models, "game-menu.json");
    Without(broken, "MB_RESUME");
    broken.opening = 99;
    router.SetInterface(broken);
    Expect(router.Using() == "generic" && router.Rejection() == "MB_RESUME(button)" && surface.errors.size() == 1,
           "without «Продолжить» the pause menu is rejected loudly");
}

int main(int argc, char **argv)
{
    const std::string models = argc > 1 ? argv[1] : "tests/models";
    GameMenu(models);
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("menu screen checks passed\n");
    return failures ? 1 : 0;
}
