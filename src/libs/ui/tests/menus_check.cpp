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
    std::vector<std::string> toasts, alerts;
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
    void Toast(const std::string &text) override
    {
        toasts.push_back(text);
    }
    void Alert(const std::string &text) override
    {
        alerts.push_back(text);
    }
    void SetLanguage(const std::string &) override
    {
    }
    std::string Text(const std::string &key) override
    {
        return "<" + key + ">";
    }
    void Shown(const std::string &, const std::string &, const std::string &) const override
    {
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
    ExpectEqual(surface.props["line"], "Герой Альфа · Порт Альфа · 1 <common.month_10> 1654", "the game in progress above the actions");
    ExpectEqual(router.Using(), "game_menu", "pause menu is a ported screen");
    ExpectEqual(surface.tag, "s-game-menu", "pause menu mounted");
    Expect(surface.errors.empty() && surface.props["extras"]["rows"].empty(), "every node of the pause menu is known");
    ExpectEqual(surface.props["resume"]["label"], "Продолжить", "resume is the game's button");
    Expect(surface.props["buttons"].size() == 4 && surface.props["buttons"][3]["id"] == "exit", "grid: save, load, settings, exit (no disabled updates)");
    ExpectEqual(surface.props["buttons"][3]["label"], "<menu.exit>", "the exit tile reads «Выйти», not the game's program exit");
    Expect(surface.props["confirm"].is_null(), "no question yet");
    router.OnEvent({"resume", "click", ""});
    ExpectEqual(surface.Last(), "back", "«Продолжить» resumes through the menu's cancel (exitCancel)");
    const char *const ids[][2] = {{"save", "MB_SAVE"}, {"load", "MB_LOAD"}, {"options", "MB_OPTIONS"}, {"exit", "MB_EXITGAME"}};
    for (const auto &[id, node] : ids)
    {
        router.OnEvent({id, "click", ""});
        ExpectEqual(surface.Last(), std::string("widget:game_menu\t") + node + "\t-1\tpress", std::string(id) + " presses its button");
    }
    Expect(!FromTitle(), "the pause menu means a game is in progress");

    router.SetInterface(Load(models, "game-menu-exit.json"));
    const Json confirm = surface.props["confirm"];
    Expect(confirm["preset"] == "danger" && confirm["title"] == "<menu.exit_to_main_menu_question>" && confirm["text"] == "" &&
               confirm["yes"] == "<menu.exit>",
           "exit question: «Выйти в главное меню?», not the game's question of the program exit");
    const size_t before = surface.commands.size();
    router.OnEvent({"confirm-confirm", "click", ""});
    Expect(surface.commands.size() == before + 2 && surface.commands[before] == "host app-exit-to-main-menu " &&
               surface.commands[before + 1] == "widget:game_menu\tCONFIRM_EXIT_YES\t-1\tpress",
           "«Выйти» tells the host it leads to the main menu, then answers yes (the game's ExitProgram)");
    router.OnEvent({"confirm", "dismiss", ""});
    ExpectEqual(surface.Last(), "widget:game_menu\tCONFIRM_EXIT_NO\t-1\tpress", "the scrim answers no");
    router.OnEvent({"confirm-cancel", "click", ""});
    ExpectEqual(surface.Last(), "widget:game_menu\tCONFIRM_EXIT_NO\t-1\tpress", "«Отмена» answers no");

    // CL's game_menu has its own exit to the main menu (MB_EXITMENU → GameOverMainMenu): it is the
    // one exit, its program exit (MB_EXITGAME) is left to the main menu, no host restart.
    InterfaceModel cl = Load(models, "game-menu.json");
    cl.opening = 96;
    for (const Widget &widget : cl.widgets)
        if (widget.node == "MB_EXITGAME")
        {
            Widget toMenu = widget;
            toMenu.node = "MB_EXITMENU";
            toMenu.label = "Главное меню";
            cl.widgets.push_back(toMenu);
            break;
        }
    router.SetInterface(cl);
    Expect(surface.errors.empty() && surface.props["buttons"].size() == 4 && surface.props["buttons"][3]["label"] == "Главное меню",
           "CL: the exit tile is the game's exit to the main menu");
    router.OnEvent({"exit", "click", ""});
    ExpectEqual(surface.Last(), "widget:game_menu\tMB_EXITMENU\t-1\tpress", "CL: exit presses MB_EXITMENU");

    // TEHO's game_menu.c greys out MB_RESUME during a boarding; its cancel still resumes there.
    InterfaceModel boarding = Load(models, "game-menu.json");
    boarding.opening = 97;
    for (Widget &widget : boarding.widgets)
        if (widget.node == "MB_RESUME")
            widget.enabled = false;
    router.SetInterface(boarding);
    Expect(surface.props["resume"]["disabled"] == false, "boarding: «Продолжить» stays available while the cancel resumes");
    router.OnEvent({"resume", "click", ""});
    ExpectEqual(surface.Last(), "back", "boarding: «Продолжить» takes the cancel route");
    for (Widget &widget : boarding.widgets)
        if (widget.node == "MB_RESUME")
            widget.context["resumeOnCancel"] = "0";
    boarding.opening = 98;
    const size_t sent = surface.commands.size();
    router.SetInterface(boarding);
    Expect(surface.props["resume"]["disabled"] == true, "no resume (Buttons.Resume.enable 0): «Продолжить» unavailable");
    router.OnEvent({"resume", "click", ""});
    Expect(surface.commands.size() == sent && surface.errors.empty(), "an unavailable «Продолжить» sends nothing");

    // A place where the game allows no saving (boarding, a battle, a no-save zone): the same
    // menu, «Сохранить» shown but disabled; a tap says why only when the game gives a reason.
    InterfaceModel no_save = Load(models, "game-menu.json");
    no_save.opening = 100;
    for (Widget &widget : no_save.widgets)
        if (widget.node == "MB_SAVE")
            widget.enabled = false, widget.actions.clear(), widget.unavailable_reason = "Сейчас нельзя сохраниться";
    router.SetInterface(no_save);
    {
        Json save;
        for (const Json &button : surface.props["buttons"])
            if (button["id"] == "save")
                save = button;
        Expect(surface.props["buttons"].size() == 4 && save["disabled"] == true && save["reason"] == "Сейчас нельзя сохраниться",
               "no-save zone: the same four actions, «Сохранить» disabled with the game's reason");
    }

    InterfaceModel broken = Load(models, "game-menu.json");
    Without(broken, "MB_RESUME");
    broken.opening = 99;
    router.SetInterface(broken);
    surface.now += 1.0; // a rejected opening is reported once it has settled
    router.Tick();
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
    ExpectEqual(rows[0]["cells"][0], "Герой Альфа · Порт Альфа", "a record is named «<hero> · <location>»");
    ExpectEqual(rows[2]["cells"][0], "Остров Гамма", "a record written without the hero: its location");
    ExpectEqual(rows[0]["cells"][1]["text"], "1 <common.month_10> 1654", "in-game date of the record");
    ExpectEqual(rows[0]["cells"][1]["sub"], "10 <common.month_3> 2001", "creation date under it (another year: the year instead of the time)");
    Expect(surface.props["columns"].size() == 3 && surface.props["columns"][1]["w"] == "120px",
           "name, the dates in one column, the autosave mark: the name keeps its room beside the detail");
    const Json detail = surface.props["detail"];
    Expect(detail["title"] == "Бухта Бета" && detail["sub"] == "Герой Альфа" && detail["delete"] == true && detail["primary"] == "<save.load>", "detail of the chosen record");
    Expect(detail["facts"].size() == 3 && detail["facts"][0]["v"] == "2 <common.month_10> 1654, 11:20" &&
               detail["facts"][1]["v"] == "6 <save.hours> 42 <save.minutes>" && detail["facts"][2]["v"] == "11 <common.month_3> 2001",
           "in-game date, play time, created");

    router.OnEvent({"saves", "row", "2"});
    ExpectEqual(surface.props["detail"]["title"], "Остров Гамма", "a row tap chooses the record locally");
    Expect(surface.commands.empty() && surface.props["detail"]["facts"].size() == 3 && surface.props["detail"]["sub"] == "" &&
               surface.props["detail"]["facts"][1]["v"] == "25 <save.hours> 0 <save.minutes>",
           "nothing sent; every record has its own play time");
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:save_load\tANDROID_SAVE_LIST\t2\tactivate@9:3", "«Загрузить» activates the record");
    router.OnEvent({"delete", "click", ""});
    ExpectEqual(surface.Last(), "widget:save_load\tANDROID_SAVE_LIST\t2\tdelete@9:3", "the trash glyph deletes the record");

    InterfaceModel question = Load(models, "saves-confirm.json");
    router.SetInterface(question);
    Json confirm = surface.props["confirm"];
    Expect(confirm["preset"] == "danger" && confirm["title"] == "<save.delete>" && confirm["subject"] == "Остров Гамма" &&
               confirm["text"] == "" && confirm["yes"] == "<save.delete>",
           "delete question: «Удалить «record»?» (V2-Save-ConfirmDelete)");
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
               surface.props["rows"][0]["cells"][0]["sub"] == "Герой Альфа · Порт Дельта" && surface.props["rows"][0]["cells"][1] == "",
           "«Новая запись» leads, chosen, named by the game in progress, without dates");
    Expect(surface.props["detail"]["title"] == "Порт Дельта" && surface.props["detail"]["facts"].size() == 1,
           "new record: the location, the hero, the play time only");
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
    empty.opening = 49;
    surface.errors.clear();
    router.SetInterface(empty);
    Expect(router.Using() == "save_load" && surface.props["rows"].empty() && surface.props["detail"].is_null() && surface.errors.empty(),
           "no records: the empty list (V2-Save-Empty)");
    Without(empty, "ANDROID_SAVES");
    empty.opening = 50;
    router.SetInterface(empty);
    surface.now += 1.0; // a rejected opening is reported once it has settled
    router.Tick();
    Expect(router.Using() == "generic" && router.Rejection() == "ANDROID_SAVES(list)" && surface.errors.size() == 1,
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
    Expect(image.size() == 1 && image[0]["columns"].size() == 2 && image[0]["columns"][0]["title"] == "<settings.graphics>" &&
               image[0]["columns"][1]["title"] == "<settings.detail>",
           "image page: one graphics column and the game's scene detail");
    Json preset = image[0]["columns"][0]["rows"][0];
    Expect(image[0]["columns"][0]["rows"].size() == 3 && preset["id"] == "app-fx_preset" && preset["selected"] == 0 && preset["restart"] == false && !preset.contains("label") &&
               preset["options"] == Json::array({"<settings.fx_low>", "<settings.fx_medium>", "<settings.fx_high>"}),
           "the quality preset «Низкая / Средняя / Высокая», applied at once; no effect overrides; a measured preset "
           "(fx_preset_auto) is not marked: " + preset.dump());
    Expect(preset["hint"] == "<settings.fx_scene_height>",
           "«Низкая» measured at a lower scene height (fx_auto_render_height) shows the height under the row: " + preset.dump());
    const Json textures = image[0]["columns"][0]["rows"][1];
    Expect(textures["id"] == "app-texture_degradation" && textures["restart"] == true && textures["disabled"] == false,
           "texture quality from the host, marked as needing a restart");
    const Json rate = image[0]["columns"][0]["rows"][2];
    Expect(rate["id"] == "app-frame_rate" && rate["label"] == "<settings.frame_rate>" && rate["selected"] == 1 && rate["restart"] == false &&
               rate["disabled"] == false && rate["options"] == Json::array({"30", "60", "90", "120"}),
           "«Частота кадров» offers the rates the host reports for the display, 60 chosen, applied at once: " + rate.dump());
    const Json grass = image[0]["columns"][1]["rows"][0];
    Expect(image[0]["columns"][1]["rows"].size() == 2 && grass["kind"] == "segment" && grass["selected"] == 2 && grass["label"] == "<settings.grass>",
           "the game's grass (radio group as one segment) and sea detail; its other effects are the preset's");
    router.OnEvent({grass["id"], "change", "3"});
    ExpectEqual(surface.Last(), prefix + "HERB_CHECKBOX\t3\t1", "grass option chosen");
    surface.commands.clear();
    router.OnEvent({"app-fx_preset", "change", "2"});
    router.OnEvent({"app-frame_rate", "change", "3"});
    Expect(surface.commands.empty() && surface.props["sections"][0]["columns"][0]["rows"][0]["selected"] == 2 &&
               surface.props["sections"][0]["columns"][0]["rows"][2]["selected"] == 3,
           "renderer options are a draft");
    Expect(!surface.props["sections"][0]["columns"][0]["rows"][0].contains("hint"),
           "the measured scene height belongs to «Низкая» only");
    router.OnEvent({"done", "click", ""});
    Expect(surface.commands.size() == 2 &&
               surface.commands[0] ==
                   "host app-settings {\"frame_rate\":120,\"fx_preset\":2,\"texture_degradation\":0}" &&
               surface.commands[1] == prefix + "BTN_OK\t-1\tpress",
           "«Готово» hands the renderer options to the host and confirms the game's settings; the measured-preset "
           "flag fx_preset_auto and the measured scene height fx_auto_render_height stay with the host");
    surface.commands.clear();
    router.OnEvent({"done", "click", ""});
    Expect(surface.commands.size() == 1, "unchanged renderer options are not sent again");
    {
        // A debuggable host reports single effect overrides: «Отладка эффектов» has the FSR / SMAA
        // rows and «Замер GPU» (fx_gpu_profile), handed to the host with «Готово» like the others.
        FakeSurface debug_surface;
        Router debug(debug_surface);
        Json reported{{"fx_preset", 1}, {"texture_degradation", 0}};
        for (const char *key : {"fx_ssao", "fx_msaa", "fx_bloom", "fx_grade", "fx_dither", "fx_contact_shadows", "fx_shadows", "fx_sea_band_limit",
                                "fx_sea_soft_edges", "fx_sea_specular", "fx_light_per_pixel", "fx_light_point", "fx_light_specular",
                                "fx_light_normal_map", "fx_alpha_to_coverage", "fx_anisotropy", "fx_ao_volume", "fx_smaa", "fx_sharpen",
                                "fx_render_height", "fx_gpu_profile", "fx_lamp_shadows", "fx_lamp_soft", "fx_lamp_model_lift",
                                "fx_lamp_debug", "fx_sail_light", "fx_town_light", "fx_town_light_debug", "fx_lamp_tint",
                                "render_backend", "render_thread", "render_overlap", "fx_bounce", "fx_bounce_debug",
                                "fx_town_walls", "fx_gloss", "fx_gloss_strength",
                                "fx_gloss_debug", "fx_sea_waves", "fx_sea_waves_lift",
                                "fx_sea_wakes", "fx_sea_waves_debug", "fx_pom_probe",
                                "fx_pom_toggle", "fx_upscale", "fx_light_reach", "fx_light_core",
                                "fx_light_level"}) // GraphicsSettings.DEBUG_KEYS, -1 = the preset's
            reported[key] = -1;
        debug.SetState("appSettings", reported);
        debug.SetInterface(Load(models, "settings.json"));
        debug.OnEvent({"tabs", "select", "2"});
        Json rows = Json::array();
        for (const Json &section : debug_surface.props["sections"])
            for (const Json &column : section["columns"])
                if (column["title"] == "Отладка эффектов")
                    rows = column["rows"];
        const auto row = [&](const char *id) {
            for (const Json &r : rows)
                if (r["id"] == id)
                    return r;
            return Json();
        };
        Expect(row("app-fx_render_height")["options"] == Json::array({"пресет", "720", "900", "1000", "1100", "экран"}) && row("app-fx_smaa")["label"] == "SMAA" &&
                   row("app-fx_sharpen").is_object() && row("app-fx_gpu_profile")["options"] == Json::array({"выкл", "вкл"}),
               "effects debug: scene height, SMAA, RCAS, GPU timing: " + rows.dump());
        Expect(row("app-fx_lamp_shadows")["options"] == Json::array({"пресет", "выкл", "8", "16"}) && row("app-fx_lamp_soft").is_object() &&
                   row("app-fx_lamp_model_lift").is_object() && row("app-fx_lamp_debug").is_object(),
               "effects debug: lamp shadows 0/8/16, their filter, the characters' lift, the lamp views: " + rows.dump());
        Expect(row("app-fx_sail_light")["options"] == Json::array({"пресет", "выкл", "100%", "200%", "300%"}),
               "effects debug: sunlight through the sails: " + rows.dump());
        Expect(row("app-fx_lamp_tint")["options"] == Json::array({"пресет", "0%", "25%", "50%", "100%"}) &&
                   row("app-render_backend")["options"] == Json::array({"авто", "GLES", "Vulkan"}) &&
                   row("app-render_thread").is_object() && row("app-render_overlap").is_object(),
               "effects debug: lamp tint, graphics API, render thread, frame overlap: " + rows.dump());
        Expect(row("app-fx_light_reach")["options"] == Json::array({"пресет", "100%", "160%", "220%", "300%"}) &&
                   row("app-fx_light_core").is_object() && row("app-fx_light_level").is_object(),
               "effects debug: the lights' reach, core and level: " + rows.dump());
        Expect(row("app-fx_bounce").is_object() && row("app-fx_bounce_debug").is_object() &&
                   row("app-fx_town_walls")["options"] == Json::array({"пресет", "игра", "наши"}),
               "effects debug: bounce and town walls: " + rows.dump());
        Expect(row("app-fx_gloss").is_object() && row("app-fx_gloss_strength").is_object() &&
                   row("app-fx_gloss_debug")["options"].size() == 6,
               "effects debug: gloss maps: " + rows.dump());
        Expect(row("app-fx_sea_waves").is_object() && row("app-fx_sea_wakes").is_object() &&
                   row("app-fx_sea_waves_debug")["options"].size() == 4,
               "effects debug: waves from objects: " + rows.dump());
        Expect(row("app-fx_upscale")["options"] == Json::array({"пресет", "выкл", "вкл"}) &&
                   row("app-fx_upscale")["label"] == "Нейро-×2 текстур",
               "effects debug: neural ×2 textures forced on or off: " + rows.dump());
        Expect(row("app-fx_town_light")["options"] == Json::array({"пресет", "игра", "наш"}) && row("app-fx_town_light_debug").is_object(),
               "effects debug: the town light, game's or ours, and its view: " + rows.dump());
        Expect(row("app-fx_pom_probe")["label"] == "Прицел: текстура и параллакс" &&
                   row("app-fx_pom_probe")["options"] == Json::array({"выкл", "вкл"}) &&
                   row("app-fx_pom_toggle")["options"] == Json::array({"А", "Б"}),
               "effects debug: the parallax probe and its toggle: " + rows.dump());
        debug.OnEvent({"app-fx_render_height", "change", "2"});
        debug.OnEvent({"app-fx_gpu_profile", "change", "1"});
        debug.OnEvent({"app-fx_pom_toggle", "change", "1"});
        debug.OnEvent({"done", "click", ""});
        Expect(!debug_surface.commands.empty() && debug_surface.commands[0].find("\"fx_render_height\":900") != std::string::npos &&
                   debug_surface.commands[0].find("\"fx_gpu_profile\":1") != std::string::npos &&
                   debug_surface.commands[0].find("\"fx_pom_toggle\":1") != std::string::npos,
               "«Готово» hands the scene height and the GPU timing to the host: " + Json(debug_surface.commands).dump());
    }
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), prefix + "BTN_CANCEL\t-1\tpress", "← drops the game's changes");

    router.OnEvent({"tabs", "select", "3"});
    Expect(surface.props["sections"][0]["columns"][0]["rows"][0]["display"] == "×1.2", "HUD scale shows the game's factor");

    InterfaceModel broken = Load(models, "settings.json");
    Without(broken, "BTN_OK");
    broken.opening = 77;
    router.SetInterface(broken);
    surface.now += 1.0; // a rejected opening is reported once it has settled
    router.Tick();
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
    ExpectEqual(surface.props["primary"]["sub"], "Герой Альфа · Порт Альфа", "«Продолжить» names the newest record");
    Expect(surface.props["primary"]["disabled"] == false, "«Продолжить» with a record");
    {
        InterfaceModel none = Load(models, "title.json");
        Without(none, "ANDROID_CONTINUE");
        none.revision = 7;
        router.SetInterface(none);
        Expect(surface.props["primary"]["disabled"] == true && surface.props["primary"]["reason"] == "<menu.no_saves>",
               "no record: «Продолжить» disabled, the reason on a tap");
        router.SetInterface(Load(models, "title.json"));
    }
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
    {
        // The host's «Продолжить» (titleIntent, the app's Test Lab Game Loop): once, at the next title build or at once.
        surface.commands.clear();
        router.SetState("titleIntent", Json{{"intent", "Continue"}});
        router.SetInterface(Load(models, "title.json"));
        Expect(surface.commands.size() == 1 && surface.Last() == "main:Load" && ContinueRequested(),
               "titleIntent Continue opens the saves to load the newest record");
        SetContinueRequested(false);
        router.SetInterface(Load(models, "title.json"));
        Expect(surface.commands.size() == 1, "only once");
        InterfaceModel none = Load(models, "title.json");
        Without(none, "ANDROID_CONTINUE");
        none.revision = 11;
        router.SetInterface(none);
        const size_t errors = surface.errors.size();
        router.SetState("titleIntent", Json{{"intent", "Continue"}});
        Expect(surface.commands.size() == 1 && surface.errors.size() == errors + 1, "without a record it is an error, nothing is sent");
        surface.errors.clear();
        router.SetInterface(Load(models, "title.json"));
        surface.commands.clear();
        router.SetState("titleIntent", Json{{"intent", "Continue"}});
        Expect(surface.commands.size() == 1 && surface.Last() == "main:Load" && ContinueRequested(),
               "titleIntent while the title is up acts at once");
        SetContinueRequested(false);
        const size_t errors2 = surface.errors.size();
        router.SetState("titleIntent", Json{{"intent", "New"}});
        Expect(surface.errors.size() == errors2 + 1, "an unknown intent is an error (title up)");
        surface.errors.clear();
        SetFromTitle(false);
    }
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
    ExpectEqual(surface.props["hero"]["image"]["texture"], "INTERFACES/PORTRAITS/128/face_1", "the hero's game portrait by face");
    ExpectEqual(surface.props["toggles"][1]["label"], "<menu.hardcore>", "the untranslated «HardcoreGame» key gets our name");
    Expect(surface.props["setup"]["text"] == "<menu.hero_type_1>" && surface.props["setup"]["note"] == "Описание занятия героя.",
           "occupation titled by its key, and its text");
    Expect(!surface.props.contains("stats"), "no attribute table (V2-Menu-NewGame)");
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
    Expect(surface.props["modal"].is_null() && surface.Last() == prefix + "CONFIRM_WINDOW_MB_YES\t-1\tpress",
           "the game's question about its existing profile folder is answered for the player (no profiles)");
    const size_t sent = surface.commands.size();
    router.SetInterface(window);
    Expect(surface.commands.size() == sent, "once per revision");

    InterfaceModel broken = Load(models, "new-game.json");
    Without(broken, "OK_BUTTON");
    broken.opening = 66;
    router.SetInterface(broken);
    surface.now += 1.0; // a rejected opening is reported once it has settled
    router.Tick();
    Expect(router.Using() == "generic" && router.Rejection() == "OK_BUTTON(button)", "without «Ок» the new game screen is rejected loudly");
}

static void Autosaves(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "saves-autosave.json"));
    const Json rows = surface.props["rows"];
    Expect(rows.size() == 4 && rows[0]["cells"][0]["text"] == "<save.autosave>" && rows[0]["cells"][0]["sub"] == "Герой Альфа · Остров Гамма" &&
               rows[0]["cells"][2]["icon"] == "energy",
           "autosaves form their own group at the top, named by the string table, marked by the bolt");
    router.OnEvent({"saves", "row", "0"});
    Expect(surface.props["detail"]["delete"] == false && surface.props["detail"]["title"] == "Остров Гамма" &&
               surface.props["detail"]["sub"] == "<save.autosave> · Герой Альфа",
           "an autosave has no quick delete");
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:save_load\tANDROID_SAVE_LIST\t1\tactivate@9:9", "an autosave loads");
    InterfaceModel saving = Load(models, "saves-autosave-save.json");
    router.SetInterface(saving);
    bool listed = false;
    for (const Json &row : surface.props["rows"])
        listed = listed || row["cells"][0].dump().find("autosave") != std::string::npos;
    Expect(!listed && surface.props["rows"].size() == 4, "autosaves are never an overwrite target");
}

static void GameOver(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetState("gameOver", Json{{"kind", "sea"}, {"language", "russian"}});
    Expect(router.Using() == "game_over" && surface.tag == "s-game-over" && surface.props["title"] == "<menu.game_over_sea>",
           "game over: the defeat's title over the death picture");
    router.Back();
    Expect(surface.commands.empty() && router.Using() == "game_over", "back does nothing: the game ends it");
    const Json actions = surface.props["actions"];
    Expect(actions.size() == 3 && actions[0]["id"] == "go-load" && actions[1]["id"] == "go-options" && actions[2]["id"] == "go-title",
           "the death screen offers load, settings and the title screen");
    router.OnEvent({"go-load", "click", ""});
    ExpectEqual(surface.Last(), "widget:game_over\tEND\t-1\tpress", "a choice ends the death picture at once");
    router.SetState("gameOver", nullptr);
    surface.commands.clear();
    router.SetInterface(Load(models, "title.json"));
    Expect(surface.commands.size() == 1 && surface.commands[0] == "main:Load", "then the title screen opens the chosen load once");
    router.SetInterface(Load(models, "title.json"));
    Expect(surface.commands.size() == 1, "only once");
    router.SetState("gameOver", Json{{"kind", "land"}, {"language", "russian"}});
    router.OnEvent({"go-title", "click", ""});
    surface.commands.clear();
    InterfaceModel title = Load(models, "title.json");
    title.opening += 1;
    router.SetInterface(title);
    Expect(surface.commands.empty() && surface.errors.empty(), "«Главное меню» stays on the title screen");
    router.CloseInterface();
    router.SetState("gameOver", Json{{"kind", "sea"}});
    router.SetState("gameOver", nullptr);
    Expect(!router.Active(), "null closes it");
    router.SetState("gameOver", Json{{"kind", "victory"}});
    Expect(!router.Active() && surface.errors.size() == 1, "an unknown kind is an error");
}

// The quick save's plate (quick_save.c): the game's caption and line, nothing to press.
static void QuickSave(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "quick-save.json"));
    Expect(router.Using() == "quick_save" && surface.tag == "s-quick-save" && surface.errors.empty() && surface.props["extras"]["rows"].empty(),
           "the quick save is a ported screen; every node is known");
    Expect(surface.props["title"] == "Быстрое сохранение" && surface.props["text"] == "Идёт сохранение...", "the game's caption and line");
    router.Back();
    Expect(surface.commands.empty() && router.Using() == "quick_save", "back does nothing: the game closes it when the record is written");
    InterfaceModel untitled = Load(models, "quick-save.json");
    Without(untitled, "INFO_WINDOW_CAPTION");
    untitled.opening += 1;
    router.SetInterface(untitled);
    Expect(surface.props["title"] == "<menu.quick_save>", "without the game's caption: our title");
}

// The engine's load watchdog (core load_watch.hpp): the player is told which record did not load.
static void LoadFailure()
{
    FakeSurface surface;
    Router router(surface);
    router.SetState("loadFailure", Json{{"record", "SAVE\\player\\Автосохранение 1"}, {"reason", "the record has no readable description"}});
    Expect(surface.alerts.size() == 1 && surface.alerts.front() == "<save.load_failed>: Автосохранение 1" && surface.errors.empty(),
           "a failed load names its record to the player in an alert (never the HUD's log)");
    router.SetState("loadFailure", nullptr);
    Expect(surface.alerts.size() == 1 && surface.errors.empty(), "null says nothing");
    router.SetState("loadFailure", Json{{"record", 7}});
    Expect(surface.alerts.size() == 1 && surface.errors.size() == 1, "a malformed failure is an error, never a quiet drop");
}

int main(int argc, char **argv)
{
    const std::string models = argc > 1 ? argv[1] : "tests/models";
    GameMenu(models);
    GameOver(models);
    QuickSave(models);
    LoadFailure();
    Saves(models);
    Autosaves(models);
    Settings(models);
    Title(models);
    NewGame(models);
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("menu screen checks passed\n");
    return failures ? 1 : 0;
}
