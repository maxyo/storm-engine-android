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

static void Saves(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "saves-load.json"));
    ExpectEqual(router.Using(), "save_load", "saves is a ported screen");
    ExpectEqual(surface.tag, "s-saves", "saves mounted");
    Expect(surface.errors.empty() && surface.props["extras"]["rows"].empty() && surface.props["extras"]["note"] == "",
           "every node of the saves screen is known");
    ExpectEqual(surface.props["title"], "<save.title_load>", "load title");
    const Json rows = surface.props["rows"];
    Expect(rows.size() == 3 && rows[1]["selected"] == true, "records listed, the game's selection chosen");
    ExpectEqual(rows[0]["cells"][1]["text"], "1 <common.month_10> 1654", "in-game date of the record");
    ExpectEqual(rows[0]["cells"][2]["text"], "10 <common.month_3> 2001, 08:50", "creation date of the record");
    const Json detail = surface.props["detail"];
    Expect(detail["title"] == "Бухта Бета" && detail["delete"] == true && detail["primary"] == "<save.load>", "detail of the chosen record");
    Expect(detail["facts"].size() == 3 && detail["facts"][1]["v"] == "6 <save.hours> 42 <save.minutes>", "in-game date, play time, created");

    router.OnEvent({"saves", "row", "2"});
    ExpectEqual(surface.props["detail"]["title"], "Остров Гамма", "a row tap chooses the record locally");
    Expect(surface.commands.empty() && surface.props["detail"]["facts"].size() == 2, "nothing sent; play time belongs to the game's selection");
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:save_load\tANDROID_SAVE_LIST\t2\tactivate@9:3", "«Загрузить» activates the record");
    router.OnEvent({"delete", "click", ""});
    ExpectEqual(surface.Last(), "widget:save_load\tANDROID_SAVE_LIST\t2\tdelete@9:3", "the trash glyph deletes the record");

    InterfaceModel question = Load(models, "saves-confirm.json");
    router.SetInterface(question);
    Json confirm = surface.props["confirm"];
    Expect(confirm["preset"] == "danger" && confirm["title"] == "<save.delete>" && confirm["subject"] == "Остров Гамма" &&
               confirm["text"] == "Удалить эту запись?",
           "delete question: danger dialog naming the record, game text kept");
    router.OnEvent({"confirm-confirm", "click", ""});
    ExpectEqual(surface.Last(), "widget:save_load\tCONFIRM_YES\t-1\tpress", "confirm answers yes");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "widget:save_load\tCONFIRM_NO\t-1\tpress", "← during a question answers no");

    // From the title screen a load needs no question.
    router.SetInterface(Load(models, "saves-load.json"));
    router.OnEvent({"primary", "click", ""});
    SetFromTitle(true);
    surface.commands.clear();
    question.revision = 3;
    router.SetInterface(question);
    Expect(surface.commands.size() == 1 && surface.Last() == "widget:save_load\tCONFIRM_YES\t-1\tpress" && surface.props["confirm"].is_null(),
           "load from the title screen: the question is answered at once");
    router.SetInterface(question);
    Expect(surface.commands.size() == 1, "and only once per revision");
    SetFromTitle(false);

    router.SetInterface(Load(models, "saves-save.json"));
    ExpectEqual(surface.props["title"], "<save.title_save>", "save title");
    Expect(surface.props["rows"][0]["cells"][0]["text"] == "<save.new>" && surface.props["rows"][0]["selected"] == true &&
               surface.props["rows"][0]["cells"][1] == "",
           "«Новая запись» leads, chosen, without dates");
    ExpectEqual(surface.props["detail"]["primary"], "<save.save>", "new record: «Сохранить»");
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:save_load\tANDROID_SAVE_LIST\t3\tactivate@10:4", "saving activates the new slot");
    router.OnEvent({"saves", "row", "1"});
    ExpectEqual(surface.props["detail"]["primary"], "<save.overwrite>", "an existing record when saving: «Перезаписать»");

    // The game's name editor is accepted as proposed.
    InterfaceModel naming = Load(models, "saves-save.json");
    Widget ok;
    ok.node = "CUSTOM_SAVE_BTN_OK";
    ok.type = "button";
    ok.label = "Ok";
    Widget name;
    name.node = "CUSTOM_SAVE_NAME";
    name.type = "edit";
    name.value = "Порт Альфа";
    naming.widgets.push_back(name);
    naming.widgets.push_back(ok);
    naming.revision = 5;
    surface.commands.clear();
    router.SetInterface(naming);
    Expect(surface.commands.size() == 1 && surface.Last() == "widget:save_load\tCUSTOM_SAVE_BTN_OK\t-1\tpress", "name editor: accepted at once");
    router.SetInterface(naming);
    Expect(surface.commands.size() == 1, "once per revision");

    InterfaceModel empty = Load(models, "saves-load.json");
    Without(empty, "ANDROID_SAVE_LIST");
    empty.opening = 50;
    surface.errors.clear();
    router.SetInterface(empty);
    Expect(router.Using() == "generic" && router.Rejection() == "ANDROID_SAVE_LIST(save)" && surface.errors.size() == 1,
           "without the engine's record list the saves screen is rejected loudly");
}

static void Settings(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    std::ifstream state(models + "/app-settings.json", std::ios::binary);
    router.SetState("appSettings", Json::parse(state));
    router.SetInterface(Load(models, "settings.json"));
    ExpectEqual(router.Using(), "option_screen", "settings is a ported screen");
    ExpectEqual(surface.tag, "s-settings", "settings mounted");
    Expect(surface.errors.empty(), "no errors");
    const Json extras = surface.props["extras"];
    Expect(extras["rows"].size() == 1 && extras["rows"][0]["title"] == "По умолчанию" && extras["note"] == "",
           "keyboard nodes and colour correction are not offered; the defaults button goes under «⋯»");
    Expect(surface.props["tabs"].size() == 4 && surface.props["tabs"][0]["selected"] == true, "four pages, the first open");
    Json columns = surface.props["sections"][0]["columns"];
    Expect(columns[0]["title"] == "<settings.volume>" && columns[0]["rows"].size() == 3 && columns[0]["rows"][0]["label"] == "<settings.music>" &&
               columns[0]["rows"][0]["value"] == 70,
           "volume sliders with our labels");
    Expect(columns[1]["rows"].size() == 3 && columns[1]["rows"][0]["label"] == "Автозапись", "game toggles keep the game's labels");

    const std::string prefix = "widget:option_screen\t";
    router.OnEvent({columns[0]["rows"][0]["id"], "change", "40"});
    Expect(surface.commands.empty(), "slider drag progress sends nothing");
    router.OnEvent({columns[0]["rows"][0]["id"], "commit", "40"});
    ExpectEqual(surface.Last(), prefix + "MUSIC_SLIDE\t-1\tset@12:40", "slider release sets the position");
    router.OnEvent({columns[1]["rows"][2]["id"], "change", "1"});
    ExpectEqual(surface.Last(), prefix + "ENHANCEDSAILING_CHECKBOX\t0\t1", "toggle on");

    router.OnEvent({"tabs", "select", "2"});
    Json image = surface.props["sections"];
    Expect(image.size() == 2 && image[0]["title"] == "<settings.graphics>", "image page: the renderer options first");
    const Json filtering = image[0]["columns"][0]["rows"][0];
    Expect(filtering["kind"] == "segment" && filtering["selected"] == 0 && filtering["restart"] == true && filtering["disabled"] == false,
           "texture filtering from the host, marked as needing a restart");
    const Json grass = image[1]["columns"][0]["rows"][3];
    Expect(grass["kind"] == "segment" && grass["selected"] == 2 && grass["label"] == "<settings.grass>", "grass radio group as one segment");
    router.OnEvent({grass["id"], "change", "3"});
    ExpectEqual(surface.Last(), prefix + "HERB_CHECKBOX\t3\t1", "grass option chosen");
    surface.commands.clear();
    router.OnEvent({filtering["id"], "change", "3"});
    Expect(surface.commands.empty() && surface.props["sections"][0]["columns"][0]["rows"][0]["selected"] == 3, "renderer options are a draft");
    router.OnEvent({"app-sea_foam", "change", "0"});
    router.OnEvent({"done", "click", ""});
    Expect(surface.commands.size() == 2 &&
               surface.commands[0] == "host app-settings {\"character_shadows\":0,\"sea_foam\":0,\"texture_anisotropy\":8,\"texture_degradation\":0,\"vsync\":1}" &&
               surface.commands[1] == prefix + "BTN_OK\t-1\tpress",
           "«Готово» hands the renderer options to the host and confirms the game's settings");
    surface.commands.clear();
    router.OnEvent({"done", "click", ""});
    Expect(surface.commands.size() == 1, "unchanged renderer options are not sent again");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), prefix + "BTN_CANCEL\t-1\tpress", "← drops the game's changes");

    router.OnEvent({"tabs", "select", "3"});
    Expect(surface.props["sections"][0]["columns"][0]["rows"][0]["display"] == "×1.2", "HUD scale shows the game's factor");

    InterfaceModel broken = Load(models, "settings.json");
    Without(broken, "BTN_OK");
    broken.opening = 77;
    router.SetInterface(broken);
    Expect(router.Using() == "generic" && router.Rejection() == "BTN_OK(button)" && surface.errors.size() == 1,
           "without «Ок» the settings screen is rejected loudly");
}

static void Title(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "title.json"));
    ExpectEqual(router.Using(), "mainmenu", "title is a ported screen");
    ExpectEqual(surface.tag, "s-title", "title mounted");
    Expect(surface.errors.empty() && FromTitle(), "no errors; the menus now run from the title screen");
    Expect(surface.props["primary"]["label"] == "<menu.continue>" && surface.props["buttons"].size() == 4, "«Продолжить» and four buttons");
    const char *const commands[][2] = {{"new", "New"}, {"load", "Load"}, {"options", "Options"}, {"credits", "Credits"}, {"exit", "Exit"}};
    for (const auto &[id, command] : commands)
    {
        router.OnEvent({id, "click", ""});
        ExpectEqual(surface.Last(), std::string("main:") + command, std::string(id) + " sends the engine's title command");
    }
    router.OnEvent({"about", "click", ""});
    ExpectEqual(surface.Last(), "host app-about ", "«О программе» asks the host for its about page");

    router.OnEvent({"continue", "click", ""});
    Expect(surface.Last() == "main:Load" && ContinueRequested(), "«Продолжить» opens the saves");
    router.SetInterface(Load(models, "saves-load.json"));
    Expect(!ContinueRequested() && surface.Last() == "widget:save_load\tANDROID_SAVE_LIST\t0\tactivate@9:1",
           "the saves screen loads the newest record at once");
    InterfaceModel question = Load(models, "saves-confirm.json");
    surface.commands.clear();
    router.SetInterface(question);
    Expect(surface.commands.size() == 1 && surface.Last() == "widget:save_load\tCONFIRM_YES\t-1\tpress", "and answers the load question");
    SetFromTitle(false);
}

static void NewGame(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "new-game.json"));
    ExpectEqual(router.Using(), "selectcharacter", "new game is a ported screen");
    ExpectEqual(surface.tag, "s-new-game", "new game mounted");
    Expect(surface.errors.empty() && surface.props["extras"]["rows"].empty() && surface.props["extras"]["note"] == "",
           "every node is known; the profile is not offered");
    const Json rows = surface.props["rows"];
    Expect(rows.size() == 2 && rows[0]["kind"] == "stepper" && rows[0]["text"] == "Средняя" && rows[1]["kind"] == "segment" &&
               rows[1]["label"] == "Морской бой",
           "difficulty selector and the sea battle segment");
    Expect(surface.props["toggles"].size() == 6 && surface.props["toggles"][0]["id"] == "tutorial", "six option toggles, the port's own first");
    Expect(surface.props["hero"]["name"] == "Герой Альфа" && surface.props["hero"]["nation"] == "<menu.nation_france>", "hero and nation");
    Expect(surface.props["setup"]["text"] == "фехтование" && surface.props["setup"]["note"] == "Описание занятия героя.", "occupation and its text");
    Expect(surface.props["stats"].size() == 3 && surface.props["stats"][2]["v"] == "7", "starting attributes");
    const std::string prefix = "widget:selectcharacter\t";
    router.OnEvent({"difficulty-right", "click", ""});
    ExpectEqual(surface.Last(), prefix + "RIGHTCHANGE_COMPLEX\t-1\tpress", "difficulty ›");
    router.OnEvent({"setup-left", "click", ""});
    ExpectEqual(surface.Last(), prefix + "LEFTCHANGE_SETUP\t-1\tpress", "occupation ‹");
    router.OnEvent({"battle", "change", "1"});
    ExpectEqual(surface.Last(), prefix + "CHECK_ARCADESAIL\t1\t1", "sea battle type chosen");
    router.OnEvent({"tutorial", "change", "1"});
    ExpectEqual(surface.Last(), prefix + "ANDROID_TUTORIAL_SKIP\t0\t1@4", "tutorial skip carries its row identity");
    router.OnEvent({surface.props["toggles"][1]["id"], "change", "1"});
    ExpectEqual(surface.Last(), prefix + "CHECK_HARDCORE\t0\t1", "option toggle");
    router.OnEvent({"bio", "click", ""});
    Expect(surface.props["extras"]["reading"] == true && surface.props["extras"]["readingText"] == "Короткая биография героя.", "ⓘ reads the biography");
    router.OnEvent({"start", "click", ""});
    ExpectEqual(surface.Last(), prefix + "OK_BUTTON\t-1\tpress", "«Начать игру»");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), prefix + "CANCEL_BUTTON\t-1\tpress", "← cancels");

    InterfaceModel window = Load(models, "new-game.json");
    Widget text;
    text.node = "CONFIRM_WINDOW_STRINGS";
    text.type = "text";
    text.label = "Начать заново?";
    Widget yes;
    yes.node = "CONFIRM_WINDOW_MB_YES";
    yes.type = "button";
    yes.label = "Да";
    window.widgets.push_back(text);
    window.widgets.push_back(yes);
    window.revision = 2;
    router.SetInterface(window);
    Expect(surface.props["modal"]["title"] == "Начать заново?" && surface.props["modal"]["actions"].size() == 1, "the game's window as a dialog");
    router.OnEvent({"modal-0", "click", ""});
    ExpectEqual(surface.Last(), prefix + "CONFIRM_WINDOW_MB_YES\t-1\tpress", "its button presses the game's button");

    InterfaceModel broken = Load(models, "new-game.json");
    Without(broken, "OK_BUTTON");
    broken.opening = 66;
    router.SetInterface(broken);
    Expect(router.Using() == "generic" && router.Rejection() == "OK_BUTTON(button)", "without «Ок» the new game screen is rejected loudly");
}

static void Autosaves(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "saves-autosave.json"));
    const Json rows = surface.props["rows"];
    Expect(rows.size() == 4 && rows[0]["cells"][0]["text"] == "<save.autosave> 2" && rows[0]["cells"][0]["icon"] == "reload",
           "autosaves form their own group at the top, named by the string table");
    router.OnEvent({"saves", "row", "0"});
    Expect(surface.props["detail"]["delete"] == false && surface.props["detail"]["title"] == "<save.autosave> 2", "an autosave has no quick delete");
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:save_load\tANDROID_SAVE_LIST\t1\tactivate@9:9", "an autosave loads");
    InterfaceModel saving = Load(models, "saves-autosave-save.json");
    router.SetInterface(saving);
    bool listed = false;
    for (const Json &row : surface.props["rows"])
        listed = listed || row["cells"][0].dump().find("autosave") != std::string::npos;
    Expect(!listed && surface.props["rows"].size() == 4, "autosaves are never an overwrite target");
}

int main(int argc, char **argv)
{
    const std::string models = argc > 1 ? argv[1] : "tests/models";
    GameMenu(models);
    Saves(models);
    Autosaves(models);
    Settings(models);
    Title(models);
    NewGame(models);
    for (const char *id : {"mainmenu", "selectcharacter", "game_menu", "save_load", "option_screen", "about", "quick_save"})
        Expect(!HostOwned(id), std::string(id) + " is not drawn by the platform host");
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("menu screen checks passed\n");
    return failures ? 1 : 0;
}
