// Host checks for screen routing: admission (KNOWN / REQUIRED / extras), the generic
// classification and the mapping of component events to engine actions. Synthetic
// models only (tests/models); no GL context, no game data.
#include "../src/picture_atlas.h"
#include "../src/screens/controls.h"

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
    std::vector<Action> actions;
    std::vector<std::string> logs;
    std::vector<std::string> errors;
    std::string language;
    TextRequest text;
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
        actions.push_back(action);
    }
    void RequestText(TextRequest request, const std::string &) override
    {
        text = std::move(request);
    }
    void Toast(const std::string &) override
    {
    }
    void SetLanguage(const std::string &game_language) override
    {
        language = game_language;
    }
    std::string Text(const std::string &key) override
    {
        return "<" + key + ">"; // the tables live in the pack; screens only name keys
    }
    void Log(const std::string &line) override
    {
        logs.push_back(line);
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

static Widget Make(const char *node, const char *type, const char *label = "", std::vector<std::string> actions = {})
{
    Widget widget;
    widget.node = node;
    widget.type = type;
    widget.label = label;
    widget.actions = std::move(actions);
    return widget;
}

static void Admission_()
{
    static constexpr NodeRule rules[] = {{"TABLE_LIST", "choice", true}, {"INFO_TEXT", "text"}, {"QTY_*", "text|button"}};
    InterfaceModel model;
    model.widgets = {Make("table_list", "choice", "row", {"select"}), Make("INFO_TEXT", "button", "Info", {"press"}),
                     Make("QTY_Result", "text", "42"),               Make("B_MOD", "button", "Mod", {"press"}),
                     Make("MAIN_BACK", "image"),                     Make("SCROLL_LIST", "image"),
                     Make("DEBUG_INFO", "text", "fps")};
    const Admission admitted = Admit(model, rules);
    Expect(admitted.accepted, "required node present (case-insensitive) → accepted");
    Expect(admitted.known.size() == 2, "known: the table row and the QTY_ prefix text");
    Expect(admitted.extras.size() == 2, "a known node with an unexpected type and an unknown button are extras");
    Expect(admitted.One("QTY_RESULT") != nullptr && admitted.Label("qty_result") == "42", "lookup by node");

    model.widgets.erase(model.widgets.begin());
    Expect(!Admit(model, rules).accepted, "missing required node → not accepted");
    model.widgets.push_back(Make("TABLE_LIST", "text", "not a table"));
    Expect(!Admit(model, rules).accepted, "required node with another type → not accepted");

    Expect(Hidden(Make("ANY_SCROLL", "stepper")) && !Hidden(Make("SHIPS_SCROLL", "choice", "Ship", {"select"})), "scroll bars are hidden, scroll lists are not");
    Expect(Hidden(Make("EMPTY", "text")) && !Hidden(Make("NAME", "edit")), "empty text hidden, empty edit shown");
}

static void Roles()
{
    Expect(RoleOf(Make("EXIT_BTN", "button", "x")) == ButtonRole::Exit, "EXIT* node is the exit");
    Expect(RoleOf(Make("B1", "button", "Отмена")) == ButtonRole::Exit, "«Отмена» is the exit");
    Expect(RoleOf(Make("B_OK", "button", "Дальше")) == ButtonRole::Confirm, "*_OK node confirms");
    Expect(RoleOf(Make("B2", "button", "Готово")) == ButtonRole::Confirm, "«Готово» confirms");
    Expect(RoleOf(Make("B3", "button", "Удалить запись")) == ButtonRole::Danger, "«Удалить» is dangerous");
    Expect(RoleOf(Make("I_SHIP_2", "button", "Корабль")) == ButtonRole::Section && SectionOf(Make("I_SHIP_2", "button")) == "ship", "section navigation");
    Expect(RoleOf(Make("B4", "button", "Подробнее")) == ButtonRole::Other, "anything else is secondary");
    // One case per shipped language (en is covered by the checks of the shared words above).
    Expect(RoleOf(Make("B5", "button", "Zurück")) == ButtonRole::Exit, "de: «Zurück» is the exit");
    Expect(RoleOf(Make("B6", "button", "Bestätigen")) == ButtonRole::Confirm, "de: «Bestätigen» confirms");
    Expect(RoleOf(Make("B7", "button", "Eintrag löschen")) == ButtonRole::Danger, "de: «löschen» is dangerous");
    Expect(RoleOf(Make("B8", "button", "Cancelar")) == ButtonRole::Exit, "es: «Cancelar» is the exit");
    Expect(RoleOf(Make("B9", "button", "Sí")) == ButtonRole::Confirm, "es: «Sí» confirms");
    Expect(RoleOf(Make("B10", "button", "Eliminar partida")) == ButtonRole::Danger, "es: «Eliminar» is dangerous");
    Expect(RoleOf(Make("B11", "button", "Annuler")) == ButtonRole::Exit, "fr: «Annuler» is the exit");
    Expect(RoleOf(Make("B12", "button", "Terminé")) == ButtonRole::Confirm, "fr: «Terminé» confirms");
    Expect(RoleOf(Make("B13", "button", "Supprimer la sauvegarde")) == ButtonRole::Danger, "fr: «Supprimer» is dangerous");
    Expect(RoleOf(Make("B14", "button", "Indietro")) == ButtonRole::Exit, "it: «Indietro» is the exit");
    Expect(RoleOf(Make("B15", "button", "Conferma")) == ButtonRole::Confirm, "it: «Conferma» confirms");
    Expect(RoleOf(Make("B16", "button", "Elimina partita")) == ButtonRole::Danger, "it: «Elimina» is dangerous");
}

static void Commands()
{
    Action action;
    action.screen = "store";
    action.opening = 7;
    action.node = "TABLE_LIST";
    action.index = 3;
    action.verb = "select";
    action.value = "row-3";
    ExpectEqual(EngineCommand(action), "widget:store\tTABLE_LIST\t3\tselect@7:row-3", "versioned select");
    action.value.clear();
    ExpectEqual(EngineCommand(action), "widget:store\tTABLE_LIST\t3\tselect", "plain select");
    action.verb = "toggle";
    action.value = "1";
    ExpectEqual(EngineCommand(action), "widget:store\tTABLE_LIST\t3\t1", "check value");
    action.verb = "set";
    action.value = "25";
    ExpectEqual(EngineCommand(action), "widget:store\tTABLE_LIST\t3\tset@7:25", "slider position");
    action.verb = "press";
    action.value.clear();
    action.guard = "row-3";
    ExpectEqual(EngineCommand(action), "widget:store\tTABLE_LIST\t3\tguard@7:row-3|press", "guarded press");
    Action back;
    back.screen = "store";
    back.verb = "back";
    ExpectEqual(EngineCommand(back), "back", "global back");
    Action plain;
    plain.node = "button-1";
    plain.verb = "click";
    ExpectEqual(EngineCommand(plain), "", "an action of a plain document is no engine command");
}

static void Generic(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "generic-split.json"));
    ExpectEqual(router.Using(), "generic", "unknown interface → generic layout");
    Expect(router.Rejection().empty() && surface.errors.empty(), "an unknown interface in the generic layout is no error");
    Expect(surface.logs.size() == 1 && surface.logs[0].rfind("screen mod_bounty: no ported screen, generic layout; nodes=TITLE,TABLE_TARGETS", 0) == 0,
           "generic use is logged once with the node set");
    router.SetInterface(Load(models, "generic-split.json"));
    Expect(surface.logs.size() == 1, "the same node set is not logged again");
    Expect(!router.OnEvent({"nobody", "click", ""}) && surface.errors.size() == 1 &&
               surface.errors[0] == "screen generic: no mapping for event nobody click ",
           "an event without a mapping is an error");
    surface.errors.clear();
    ExpectEqual(surface.tag, "s-generic", "generic screen mounted");
    ExpectEqual(surface.props.value("layout", ""), "split", "table + long text → Split");
    ExpectEqual(surface.props.value("title", ""), "Доска розыска", "heading → title");
    Expect(surface.props["left"].size() == 1 && surface.props["left"][0]["kind"] == "table", "main block is the table");
    Expect(surface.props["left"][0]["rows"].size() == 5 && surface.props["left"][0]["rows"][4]["disabled"] == true, "disabled row keeps its reason");
    Expect(surface.props["right"].size() == 1 && surface.props["right"][0]["paragraphs"].size() == 2, "detail = the text, two paragraphs");
    Expect(surface.props["actions"].size() == 2 && surface.props["actions"][1]["variant"] == "primary" &&
               surface.props["actions"][1]["label"] == "Взять",
           "«Взять» is the primary, bottom-right");
    Expect(surface.props["extras"]["actions"].empty(), "no «⋯» without overflow");

    Expect(router.OnEvent({"g0", "row", "2"}), "table row event is bound");
    ExpectEqual(surface.Last(), "widget:mod_bounty\tTABLE_TARGETS\t2\tselect@7:t2", "row tap selects by row identity");
    router.OnEvent({"g0", "row", "4"});
    Expect(surface.commands.size() == 1, "a row without verbs sends nothing");
    router.OnEvent({"g-primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:mod_bounty\tB_TAKE\t-1\tpress", "primary presses its button");
    Expect(surface.props["actions"][1]["busy"] == true, "pressed button is busy until the next revision");
    InterfaceModel next = Load(models, "generic-split.json");
    next.revision = 2;
    router.SetInterface(next);
    Expect(surface.props["actions"][1]["busy"] == false, "next revision clears busy");
    router.OnEvent({"g-primary", "click", ""});
    surface.now = 5.0;
    router.Tick();
    Expect(surface.props["actions"][1]["busy"] == false, "busy expires when the engine never answers");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "widget:mod_bounty\tEXIT_BTN\t-1\tpress", "← presses the exit button");

    router.SetInterface(Load(models, "generic-settings.json"));
    ExpectEqual(surface.props.value("layout", ""), "settings", "controls only → Settings");
    const Json rows = surface.props["left"][0]["rows"];
    Expect(rows.size() == 6, "two toggles, one segment, slider, field, stepper");
    Expect(rows[2]["kind"] == "segment" && rows[2]["label"] == "Сложность" && rows[2]["selected"] == 1, "radio group → segment named by the preceding text");
    Expect(rows[3]["kind"] == "slider" && rows[3]["value"] == 60, "slider shows percent of its range");
    Expect(surface.props["danger"]["label"] == "Удалить настройки", "danger button on the left");
    const std::string prefix = "widget:mod_options\t";
    router.OnEvent({rows[0]["id"], "change", "0"});
    ExpectEqual(surface.Last(), prefix + "CHECK_OPTIONS\t0\t0", "toggle off");
    router.OnEvent({rows[2]["id"], "change", "2"});
    ExpectEqual(surface.Last(), prefix + "CHECK_DIFF\t2\t1", "segment chooses the option");
    router.OnEvent({rows[3]["id"], "commit", "47"});
    ExpectEqual(surface.Last(), prefix + "SLIDE_VOL\t-1\tset@7:25", "slider percent → position snapped to the step");
    router.OnEvent({rows[4]["id"], "focus", "Аделина"});
    ExpectEqual(surface.text.text, "Аделина", "field asks for the editor with its text");
    router.SubmitText(surface.text.token, "Мария", true);
    ExpectEqual(surface.Last(), prefix + "EDIT_NAME\t-1\tМария", "accepted text goes to the edit box");
    router.OnEvent({rows[5]["id"].get<std::string>() + "-right", "click", ""});
    ExpectEqual(surface.Last(), prefix + "FLAG_CHANGER\t-1\tright", "stepper arrow");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), prefix + "B_CANCEL\t-1\tpress", "«Отмена» is not drawn: ← sends it");

    // More than three buttons: two stay, the rest go under «⋯».
    InterfaceModel many;
    many.screen = "mod_many";
    many.opening = 3;
    many.widgets = {Make("T", "text", "Выберите действие"), Make("B_A", "button", "Раз", {"press"}), Make("B_B", "button", "Два", {"press"}),
                    Make("B_C", "button", "Три", {"press"}),  Make("B_D", "button", "Четыре", {"press"})};
    router.SetInterface(many);
    ExpectEqual(surface.props.value("layout", ""), "reading", "text and buttons only → Reading");
    Expect(surface.props["actions"].size() == 2 && surface.props["extras"]["rows"].size() == 2 && surface.props["extras"]["actions"].size() == 1,
           "overflow buttons sit in the «Прочее» sheet");
    router.OnEvent({"extras-more", "click", ""});
    Expect(surface.props["extras"]["open"] == true, "«⋯» opens the sheet");
    router.OnEvent({surface.props["extras"]["rows"][1]["id"], "click", ""});
    ExpectEqual(surface.Last(), "widget:mod_many\tB_D\t-1\tpress", "sheet row presses its button");
    Expect(surface.props["extras"]["open"] == false, "the sheet closes after an action");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "back", "no exit button → global back");

    // Section navigation of the hero screens goes to the header.
    InterfaceModel book;
    book.screen = "questbook"; // a hero section without a ported screen yet
    book.opening = 5;
    book.widgets = {Make("TITLE", "heading", "Журнал торговли"), Make("I_SHIP", "button", "Корабли", {"press"}),
                    Make("I_SHIP_2", "button", "Корабли", {"press"}), Make("STR_BUTTONS", "text", "Корабли")};
    router.SetInterface(book);
    Expect(surface.props["sections"] == "journal" && surface.props["left"].empty(), "section buttons and their captions leave the content");
    router.OnEvent({"header", "section", "ship"});
    ExpectEqual(surface.Last(), "widget:questbook\tI_SHIP\t-1\tpress", "a header section presses its button");

    router.CloseInterface();
    Expect(surface.tag.empty() && !router.Active(), "closing unmounts the screen");
}

static void Store(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "store.json"));
    ExpectEqual(router.Using(), "store", "store is a ported screen");
    ExpectEqual(surface.tag, "s-trade", "store screen mounted");
    Expect(surface.props["list"]["rows"].size() == 8 && surface.props["primary"].is_null(), "goods listed, no deal yet");
    ExpectEqual(surface.props["extras"]["note"], "Членам купеческой гильдии — скидка 5 % до 1 декабря", "unknown text → parchment note");
    Expect(surface.props["extras"]["rows"].size() == 1 && surface.props["extras"]["rows"][0]["title"] == "Гильдейский счёт", "unknown button → «Прочее» row");
    Expect(surface.logs.size() == 1 && surface.logs[0] == "screen store extras=B_GUILD,GUILD_NOTE", "extras logged once");
    InterfaceModel one_line = Load(models, "store.json");
    for (Widget &widget : one_line.widgets)
        if (widget.node == "CAPACITY")
            widget.label = "Дедвейт: 189 / 700"; // as the game writes it
    router.SetInterface(one_line);
    Expect(surface.props["stats"][0]["label"] == "Дедвейт" && surface.props["stats"][0]["value"] == "189 / 700", "a one-line figure is split at its colon");
    Expect(surface.logs.size() == 1 && surface.errors.empty(), "the same node set is not logged again");

    router.OnEvent({"goods", "row", "1"});
    ExpectEqual(surface.Last(), "widget:store\tTABLE_LIST\t1\tactivate@7:goods:1:ship:0", "row tap opens the deal for that row");

    router.SetInterface(Load(models, "store-deal.json"));
    const Json row = surface.props["list"]["rows"][0];
    Expect(row["q"]["value"] == 300 && row["q"]["max"] == 2040 && row["q"]["min"] == 0, "open row shows the target amount in the hold");
    ExpectEqual(surface.props["primary"]["label"], "Купить 100", "primary names the deal");
    surface.commands.clear();
    router.OnEvent({"goods", "commit", "150"});
    ExpectEqual(surface.Last(), "widget:store\tQTY_EDIT\t-1\tguard@7:goods:0:ship:0|-50", "below the hold = sell, guarded by the row");
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:store\tQTY_OK_BUTTON\t-1\tguard@7:goods:0:ship:0|press", "primary confirms");
    surface.now += 0.3;
    router.SetInterface(Load(models, "store-deal.json")); // the game answers with its own amount (a limit)
    Expect(surface.props["list"]["rows"][0]["q"]["value"] == 150, "the amount just set is shown for a moment");
    surface.now += 1.0;
    router.Tick();
    Expect(surface.props["list"]["rows"][0]["q"]["value"] == 300, "then the game's amount, without another model");
    surface.commands.clear();
    router.OnEvent({"goods", "row", "2"});
    Expect(surface.commands.size() == 2 && surface.commands[0] == "widget:store\tQTY_CANCEL_BUTTON\t-1\tguard@7:goods:0:ship:0|press" &&
               surface.commands[1] == "widget:store\tTABLE_LIST\t2\tactivate@7:goods:2:ship:0",
           "another row: cancel the open deal, then open the new one");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "widget:store\tQTY_CANCEL_BUTTON\t-1\tguard@7:goods:0:ship:0|press", "← closes the deal first");
    surface.commands.clear();
    router.Back();
    ExpectEqual(surface.Last(), "widget:store\tQTY_CANCEL_BUTTON\t-1\tguard@7:goods:0:ship:0|press", "the platform's back does what ← does");
    router.OnEvent({"extras-more", "click", ""});
    surface.commands.clear();
    router.Back();
    Expect(surface.commands.empty() && surface.props["extras"]["open"] != true, "back closes the «Прочее» sheet before anything else");
    router.OnEvent({"header", "subject-next", ""});
    Expect(surface.Last().find("SHIPS_SCROLL") == std::string::npos, "the ship cannot change during a deal");

    // Without its required table the store falls back to the generic layout.
    InterfaceModel broken = Load(models, "store.json");
    broken.widgets.erase(std::remove_if(broken.widgets.begin(), broken.widgets.end(), [](const Widget &w) { return w.node == "TABLE_LIST"; }),
                         broken.widgets.end());
    broken.opening = 8;
    surface.now = 10.0;
    router.SetInterface(broken);
    Expect(surface.tag.empty() && surface.errors.empty(), "a fresh opening that is not complete yet waits, unreported");
    InterfaceModel filled = Load(models, "store.json");
    filled.opening = 8;
    filled.revision = 2;
    router.SetInterface(filled);
    Expect(router.Using() == "store" && surface.errors.empty(), "the ported screen when the game has filled the interface");
    broken.opening = 9;
    router.SetInterface(broken);
    surface.now += 1.0;
    router.Tick();
    ExpectEqual(router.Using(), "generic", "store without TABLE_LIST → generic");
    ExpectEqual(router.Rejection(), "TABLE_LIST(choice)", "the rejection names the missing required node");
    Expect(surface.errors.size() == 1 &&
               surface.errors[0] == "screen store: required nodes missing or mistyped: TABLE_LIST(choice); drawn by the generic layout",
           "a ported screen that cannot be used is an error");
    broken.revision = 99;
    router.SetInterface(broken);
    Expect(surface.errors.size() == 1, "reported once per node set");
    broken.widgets.pop_back();
    router.SetInterface(broken);
    Expect(surface.errors.size() == 2, "and again when the node set changes");
    router.SetInterface(Load(models, "store.json"));
    Expect(router.Using() == "store" && router.Rejection().empty(), "the ported screen returns when its nodes are back");

    Expect(HostOwned("ship") && !HostOwned("store") && !HostOwned("hirecrew") && !HostOwned("mod_bounty"), "host keeps only the listed interfaces");
}

// The other commerce interfaces share the store's screen; these check what differs.
static void Commerce(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    for (const char *name : {"storage.json", "storage-deal.json", "itemsbox.json", "itemsbox-deal.json", "itemstrade-deal.json",
                             "cannons-deal.json", "contraband-deal.json"})
    {
        const InterfaceModel model = Load(models, name);
        router.SetInterface(model);
        Expect(router.Using() == model.screen && surface.tag == "s-trade" && router.Rejection().empty(), std::string(name) + " is drawn by the trade screen");
        Expect(surface.props["extras"]["rows"].empty() && surface.props["extras"]["note"] == "", std::string(name) + " has no extras");
    }
    Expect(surface.errors.empty(), "no errors on the commerce models");

    // Storage: a free transfer applies when the amount settles; no primary button.
    router.SetInterface(Load(models, "storage-deal.json"));
    Expect(surface.props["primary"].is_null() && surface.props["list"]["cols"].size() == 2, "storage: two amount columns, no deal button");
    ExpectEqual(surface.props["list"]["rows"][0]["image"]["texture"], "atlas:GOODS/Sugar", "rows carry the game's picture reference");
    Expect(surface.props["stats"].size() == 2 && surface.props["stats"][1]["value"] == "2328 / 10000", "hold and storage figures in the header");
    surface.commands.clear();
    const std::string zero = "widget:storage\tQTY_EDIT\t-1\tguard@7:row:0|0";
    router.OnEvent({"goods", "change", "80"});
    router.OnEvent({"goods", "change", "90"});
    Expect(surface.commands.size() == 2 && surface.commands[0] == zero && surface.commands[1] == zero,
           "each step only leaves giving at once; the amount waits for it to settle");
    surface.commands.clear();
    surface.now += 0.5;
    router.Tick();
    Expect(surface.commands.size() == 2 && surface.commands[0] == "widget:storage\tQTY_EDIT\t-1\tguard@7:row:0|30" &&
               surface.commands[1] == "widget:storage\tQTY_OK_BUTTON\t-1\tguard@7:row:0|press",
           "then the last amount is entered and confirmed, in that order");
    surface.commands.clear();
    router.OnEvent({"goods", "commit", "10"});
    router.Tick();
    Expect(surface.commands.size() == 3 && surface.commands[0] == zero && surface.commands[1] == "widget:storage\tQTY_EDIT\t-1\tguard@7:row:0|-50",
           "the end of a drag applies at once; less than we hold = give, after zero");
    surface.commands.clear();
    router.OnEvent({"goods", "commit", "60"});
    router.Tick();
    Expect(surface.commands.size() == 2 && surface.Last() == zero, "an unchanged amount confirms nothing");

    // Chest / body: the game's category tabs, take all, remove body.
    router.SetInterface(Load(models, "itemsbox.json"));
    Expect(surface.props["filters"].size() == 4 && surface.props["filters"][0]["selected"] == true, "itemsbox: the game's tabs");
    Expect(surface.props["list"]["cols"][0]["label"] == "<trade.yours>" && surface.props["list"]["cols"][1]["label"] == "<trade.theirs>",
           "itemsbox: the game names both amount columns alike, so the string table names them");
    ExpectEqual(surface.language, "russian", "the router passes the model's language on");
    ExpectEqual(surface.props["primary"]["label"], "Взять всё", "itemsbox: take all is the primary");
    Expect(surface.props["actions"].size() == 1 && surface.props["actions"][0]["label"] == "Убрать тело", "itemsbox: remove body beside it");
    router.OnEvent({"filters", "select", "2"});
    ExpectEqual(surface.Last(), "widget:itemsbox\tTABBTN_3\t-1\tpress", "a tab presses the game's tab button");
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:itemsbox\tGETALL_BUTTON\t-1\tpress", "take all");
    router.OnEvent({"action-0", "click", ""});
    ExpectEqual(surface.Last(), "widget:itemsbox\tDELBODY_BUTTON\t-1\tpress", "remove body");
    router.SetInterface(Load(models, "itemsbox-deal.json"));
    Expect(surface.props["list"]["rows"][0]["q"]["steps"].size() == 2 && surface.props["list"]["rows"][0]["q"]["max"] == 26, "single items: steps 1 and 10, up to both sides");

    // While a deal is set, the header shows what it changes.
    router.SetInterface(Load(models, "store.json"));
    InterfaceModel buying = Load(models, "store-deal.json");
    for (Widget &widget : buying.widgets)
        if (widget.node == "CAPACITY")
            widget.label = "Трюм:\n194 / 199"; // the game counts the deal into the hold figure
    router.SetInterface(buying);
    Expect(surface.props["stats"][0]["value"] == "189" && surface.props["stats"][0]["preview"] == "194", "hold: before → with the deal");
    Expect(surface.props["stats"][1]["value"] == "1 500" && surface.props["stats"][1]["preview"] == "1 475", "balance: now → after buying for 25");
    ExpectEqual(surface.props["list"]["rows"][5]["meta"], "<trade.export> · Пачка 10", "the trade type the colour tells is named from the string table");
    InterfaceModel doubloons = Load(models, "itemstrade-deal.json");
    for (Widget &widget : doubloons.widgets)
        if (widget.node == "OUR_GOLD")
            widget.context["currency"] = "doubloons";
    router.SetInterface(doubloons);
    Expect(surface.props["stats"][1]["label"] == "<trade.doubloons>" && !surface.props["stats"][1].contains("icon") &&
               surface.props["stats"][1]["preview"] == "750",
           "a doubloon merchant: the balance is named, not drawn as coins");

    // Money trades: explicit primary with the price.
    router.SetInterface(Load(models, "cannons-deal.json"));
    ExpectEqual(surface.props["primary"]["label"], "Купить 6", "cannons: the primary names the deal");
    ExpectEqual(surface.props["primary"]["sub"], "Деньги 1740, вес 36 ц", "and carries the price");
    ExpectEqual(surface.props["list"]["rows"][1]["meta"], "Дист. 450 · Урон x1.5 · Время 11", "cannons: range, damage, reload under the name");
    router.SetInterface(Load(models, "contraband-deal.json"));
    Expect(surface.props["list"]["rows"][0]["q"]["max"] == 50 && surface.props["list"]["rows"][0]["q"]["min"] == 0, "contraband: goods without a buy price can only be sold");
    ExpectEqual(surface.props["primary"]["label"], "Продать 50", "contraband: selling");
    router.SetInterface(Load(models, "itemstrade-deal.json"));
    ExpectEqual(surface.props["subject"]["label"], "Шарль де Мор", "items trade: the character selector is the subject");
    router.OnEvent({"goods", "commit", "5"});
    surface.now += 0.5;
    router.Tick();
    ExpectEqual(surface.Last(), "widget:itemstrade\tQTY_EDIT\t-1\tguard@7:row:0|2", "money trades only enter the amount");
}

static void Crew(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "hirecrew.json"));
    Expect(router.Using() == "hirecrew" && surface.tag == "s-crew" && surface.errors.empty(), "hirecrew is drawn by the crew screen");
    Json row = surface.props["list"]["rows"][0];
    Expect(row["q"]["value"] == 40 && row["q"]["max"] == 65 && row["cells"][2]["text"] == "23", "crew on the ship, up to ship + tavern, price per man");
    Expect(surface.props["primary"]["disabled"] == true, "nothing to confirm yet");
    Expect(surface.props["ship"].size() == 4 && surface.props["ship"][0]["v"] == "Хорошая" && surface.props["tavern"][1]["v"] == "неопытные",
           "morale and experience of both crews");
    router.OnEvent({"crew", "commit", "50"});
    ExpectEqual(surface.Last(), "widget:hirecrew\tQTY_EDIT\t-1\tguard@7:ship:0|10", "more men = hire, bound to the ship");

    router.SetInterface(Load(models, "hirecrew-hire.json"));
    row = surface.props["list"]["rows"][0];
    Expect(row["q"]["value"] == 50 && surface.props["stats"][0]["preview"] == "50", "hiring 10 shows 50 on the ship");
    ExpectEqual(surface.props["primary"]["label"], "Нанять 10", "primary names the deal");
    ExpectEqual(surface.props["primary"]["sub"], "Стоимость найма 230", "with its cost");
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:hirecrew\tQTY_OK_BUTTON\t-1\tguard@7:ship:0|press", "confirm is bound to the ship");

    surface.now += 2.0; // past the moment the player's own amount is preferred
    router.Tick();
    router.SetInterface(Load(models, "hirecrew-fire.json"));
    Expect(surface.props["list"]["rows"][0]["q"]["value"] == 35, "firing 5 shows 35 on the ship");
    surface.commands.clear();
    router.OnEvent({"crew", "commit", "45"});
    Expect(surface.commands.size() == 2 && surface.commands[0] == "widget:hirecrew\tQTY_EDIT\t-1\tguard@7:ship:0|0" &&
               surface.commands[1] == "widget:hirecrew\tQTY_EDIT\t-1\tguard@7:ship:0|5",
           "from firing to hiring: zero first, then the amount");
    router.OnEvent({"header", "subject-next", ""});
    ExpectEqual(surface.Last(), "widget:hirecrew\tSHIPS_SCROLL\t1\tselect@7:ship:1", "the next ship");

    InterfaceModel broken = Load(models, "hirecrew.json");
    broken.widgets.erase(std::remove_if(broken.widgets.begin(), broken.widgets.end(), [](const Widget &w) { return w.node == "QTY_EDIT"; }),
                         broken.widgets.end());
    broken.opening = 9;
    router.SetInterface(broken);
    surface.now += 1.0;
    router.Tick();
    Expect(router.Using() == "generic" && router.Rejection() == "QTY_EDIT(edit)" && surface.errors.size() == 1, "without its edit box the crew screen is rejected loudly");
}

// The capability token a widget of the model offers for `verb` ("yard@7:2:press").
static std::string Yard(const InterfaceModel &model, const char *node, int index, const char *type, const std::string &verb)
{
    for (const Widget &widget : model.widgets)
        if (widget.node == node && widget.index == index && widget.type == type)
            for (const std::string &action : widget.actions)
                if (action.rfind("yard@", 0) == 0 && action.size() > verb.size() && action.substr(action.size() - verb.size() - 1) == ":" + verb)
                    return "widget:shipyard\t" + std::string(node) + "\t" + std::to_string(index) + "\t" + action;
    return "(no token " + std::string(node) + " " + verb + ")";
}

static Widget *Find(InterfaceModel &model, const char *node, const char *type)
{
    for (Widget &widget : model.widgets)
        if (widget.node == node && widget.type == type)
            return &widget;
    return nullptr;
}

static Widget Control(const char *node, int index, const std::string &token, const char *type = "shipyard_control")
{
    Widget widget = Make(node, type, "", {token});
    widget.index = index;
    widget.enabled = true;
    return widget;
}

// Shipyard: models with the structure recorded on the device (texts replaced), plus repair and
// captain windows built from them.
static void Shipyard(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel main = Load(models, "shipyard-main.json");
    router.SetInterface(main);
    Expect(router.Using() == "shipyard" && surface.tag == "s-shipyard" && router.Rejection().empty(), "shipyard is drawn by its screen");
    Expect(surface.errors.empty() && surface.props["extras"]["rows"].empty() && surface.props["extras"]["note"] == "", "no errors, no extras");
    Expect(surface.props["catalog"]["rows"].size() == 3 && surface.props["catalog"]["rows"][2]["cells"][2]["text"] == "23 824" &&
               surface.props["catalog"]["rows"][0]["cells"][0]["sub"] == "«Чайка»",
           "catalogue: type, «name», grouped price");
    Expect(surface.props["own"]["sub"] == "Класс 5" && surface.props["own"]["actions"].size() == 2 && surface.props["own"]["actions"][1]["sub"] == "11\u00a0575",
           "our ship: repair, and sell with its trade-in price");
    Expect(surface.props["detail"]["title"] == "Кеч «Марта»" && surface.props["detail"]["primary"].is_null() &&
               surface.props["detail"]["cannons"]["value"] == "6 фунт / 12" && surface.props["sheet"].is_null(),
           "our ship on the right; no buy button for it");
    Expect(surface.props["subject"]["label"] == "Марта" && surface.props["subject"]["sub"] == "1 <common.of> 5", "fleet slots in the header");

    router.OnEvent({"catalog", "row", "1"});
    ExpectEqual(surface.Last(), Yard(main, "TABLE_SHIPYARD", 1, "choice", "select"), "a catalogue row selects the offer with its token");
    surface.commands.clear();
    router.OnEvent({"sell", "click", ""});
    Expect(surface.commands.empty(), "one capability per model revision: the second tap waits for the answer");
    main.revision += 1;
    router.SetInterface(main);
    router.OnEvent({"sell", "click", ""});
    ExpectEqual(surface.Last(), Yard(main, "BUTTON_SELL", -1, "button", "press"), "sell presses the game's button");
    main.revision += 1;
    router.SetInterface(main);
    router.OnEvent({"header", "subject-next", ""});
    ExpectEqual(surface.Last(), Yard(main, "SHIPS_SCROLL", 1, "choice", "select"), "the next fleet slot");
    main.revision += 1;
    router.SetInterface(main);
    router.OnEvent({"cannons", "click", ""});
    ExpectEqual(surface.Last(), Yard(main, "TABLE_OTHER", 8, "choice", "activate"), "the cannons row opens the game's cannons window");
    main.revision += 1;
    router.SetInterface(main);
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), Yard(main, "EXIT_BTN", -1, "shipyard_control", "press"), "← leaves through the game's exit control");

    // Not ready: drawn, but nothing is sent.
    InterfaceModel settling = main;
    settling.revision += 1;
    Find(settling, "ANDROID_SHIPYARD_STATE", "shipyard_state")->value = "0";
    router.SetInterface(settling);
    surface.commands.clear();
    router.OnEvent({"sell", "click", ""});
    Expect(surface.commands.empty() && surface.props["own"]["actions"][1]["disabled"] == true, "a state that is not ready offers nothing");
    Expect(surface.errors.empty(), "a state settling for a moment is not an error");
    surface.now += 2.5;
    router.Tick();
    Expect(surface.errors.size() == 1, "a state that stays unusable is reported");

    const InterfaceModel preview = Load(models, "shipyard-preview.json");
    router.SetInterface(preview);
    Expect(surface.props["detail"]["title"] == "Шлюпка «Чайка»" && surface.props["detail"]["primary"]["label"] == "Купить" &&
               surface.props["detail"]["primary"]["sub"] == "2 170" && surface.props["catalog"]["rows"][0]["selected"] == true,
           "an offer on the right: buy with its price");
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), Yard(preview, "BUTTON_BUY", -1, "button", "press"), "buy presses the game's button");
    InterfaceModel looking = preview;
    looking.revision += 1;
    router.SetInterface(looking);
    router.OnEvent({"own", "click", ""});
    ExpectEqual(surface.Last(), Yard(looking, "SHIPS_SCROLL", 0, "choice", "select"), "our ship's row brings it back to the card");

    const InterfaceModel sell = Load(models, "shipyard-confirm-sell.json");
    router.SetInterface(sell);
    const Json sheet = surface.props["sheet"];
    Expect(sheet["kind"] == "confirm" && sheet["text"] == "Продать корабль за 11 575 песо?" && sheet["actions"].size() == 2 &&
               sheet["actions"][1]["variant"] == "danger" && sheet["actions"][1]["label"] == "Продать",
           "selling asks with the game's text and a danger button named by the deal");
    router.OnEvent({"sheet-ok", "click", ""});
    ExpectEqual(surface.Last(), Yard(sell, "MSG_OK", -1, "button", "press"), "confirm presses the game's OK");
    InterfaceModel again = sell;
    again.revision += 1;
    router.SetInterface(again);
    router.Back();
    ExpectEqual(surface.Last(), Yard(again, "MSG_CANCEL", -1, "button", "press"), "the platform's back closes the window first, as ← does");

    const InterfaceModel cannons = Load(models, "shipyard-cannons.json");
    router.SetInterface(cannons);
    Expect(surface.props["sheet"]["kind"] == "cannons" && surface.props["sheet"]["port"][0]["v"] == "4" && surface.props["sheet"]["bow"][0]["k"] == "<yard.bow>",
           "cannons window: the four sides");
    router.OnEvent({"sheet-close", "click", ""});
    ExpectEqual(surface.Last(), Yard(cannons, "CANNONS_CANCEL", -1, "button", "press"), "closing it presses the game's cancel");

    const InterfaceModel help = Load(models, "shipyard-help.json");
    router.SetInterface(help);
    Expect(surface.props["sheet"]["kind"] == "help" && surface.props["sheet"]["lines"].size() == 1, "help window with the game's text");
    router.OnEvent({"sheet", "dismiss", ""});
    ExpectEqual(surface.Last(), Yard(help, "ANDROID_SHIPYARD_CLOSE", -1, "shipyard_control", "close"), "the scrim closes help through the engine's close control");

    // Repair: the quantity asks for a percentage; one step per revision toward it.
    InterfaceModel repair = Load(models, "shipyard-main.json");
    repair.revision = 100;
    Find(repair, "ANDROID_SHIPYARD_STATE", "shipyard_state")->context["mode"] = "repair";
    const auto label = [](const char *node, const char *text) {
        Widget widget = Make(node, "text", text);
        widget.index = 0;
        return widget;
    };
    repair.widgets.push_back(label("REPAIR_WINDOW_CAPTION", "Ремонт"));
    repair.widgets.push_back(label("REPAIR_WINDOW_TEXT", "0"));
    repair.widgets.push_back(label("REPAIR_QTY_H", "88%"));
    repair.widgets.push_back(label("REPAIR_QTY_S", "95%"));
    repair.widgets.push_back(Control("REPAIR_RIGHT_H", -1, "yard@7:9:press"));
    repair.widgets.push_back(Control("REPAIR_RIGHT_H", -5, "yard@7:9:step5"));
    repair.widgets.push_back(Control("REPAIR_LEFT_H", -1, "yard@7:9:press"));
    repair.widgets.push_back(Control("REPAIR_LEFT_H", -5, "yard@7:9:step5"));
    repair.widgets.push_back(Control("REPAIR_OK", -1, "yard@7:9:press", "button"));
    repair.widgets.push_back(Control("REPAIR_ALL", -1, "yard@7:9:press", "button"));
    repair.widgets.push_back(Control("REPAIR_CANCEL", -1, "yard@7:9:press", "button"));
    for (Widget &widget : repair.widgets)
        if (widget.node == "REPAIR_OK" || widget.node == "REPAIR_ALL" || widget.node == "REPAIR_CANCEL")
            widget.label = widget.node == "REPAIR_OK" ? "Починить" : widget.node == "REPAIR_ALL" ? "Чинить всё" : "Отмена";
    router.SetInterface(repair);
    const Json parts = surface.props["sheet"]["parts"];
    Expect(surface.props["sheet"]["kind"] == "repair" && parts.size() == 2 && parts[0]["q"]["value"] == 88 && parts[0]["q"]["min"] == 88 &&
               parts[0]["q"]["max"] == 100 && parts[0]["q"]["label"] == "Корпус",
           "repair: hull and sails from what they are now to 100 %");
    surface.commands.clear();
    router.OnEvent({"hull", "commit", "100"});
    ExpectEqual(surface.Last(), "widget:shipyard\tREPAIR_RIGHT_H\t-5\tyard@7:9:step5", "far from the target: a step of 5");
    Expect(surface.commands.size() == 1, "one step per revision");
    repair.revision += 1;
    for (Widget &widget : repair.widgets)
        if (widget.node == "REPAIR_QTY_H")
            widget.label = "93%";
    router.SetInterface(repair);
    Expect(surface.commands.size() == 2 && surface.Last() == "widget:shipyard\tREPAIR_RIGHT_H\t-5\tyard@7:9:step5", "the next revision takes the next step");
    repair.revision += 1;
    for (Widget &widget : repair.widgets)
        if (widget.node == "REPAIR_QTY_H")
            widget.label = "98%";
    router.SetInterface(repair);
    Expect(surface.commands.size() == 3 && surface.Last() == "widget:shipyard\tREPAIR_RIGHT_H\t-1\tyard@7:9:press", "close to it: steps of 1");
    repair.revision += 1;
    router.SetInterface(repair); // the game did not move (money)
    Expect(surface.commands.size() == 3, "when the game stops moving, so does the screen");
    repair.revision += 1;
    router.SetInterface(repair);
    router.OnEvent({"sheet-ok", "click", ""});
    ExpectEqual(surface.Last(), "widget:shipyard\tREPAIR_OK\t-1\tyard@7:9:press", "repair confirms with the game's button");

    // Captain: choose who takes the bought ship.
    InterfaceModel captain = Load(models, "shipyard-main.json");
    captain.revision = 200;
    Find(captain, "ANDROID_SHIPYARD_STATE", "shipyard_state")->context["mode"] = "captain";
    Widget first = Control("PASSENGERSLIST", 0, "yard@7:11:select", "choice");
    first.label = "Пьер Тест";
    first.value = "1";
    Widget second = Control("PASSENGERSLIST", 1, "yard@7:11:select", "choice");
    second.label = "Анна Тест";
    captain.widgets.push_back(first);
    captain.widgets.push_back(second);
    captain.widgets.push_back(Control("ACCEPT_ADD_OFFICER", -1, "yard@7:11:press", "button"));
    captain.widgets.push_back(Control("CANCEL_ADD_OFFICER", -1, "yard@7:11:press", "button"));
    router.SetInterface(captain);
    Expect(surface.props["sheet"]["kind"] == "captain" && surface.props["sheet"]["candidates"].size() == 2 &&
               surface.props["sheet"]["candidates"][0]["selected"] == true,
           "captain: the candidates");
    router.OnEvent({"candidate-1", "click", ""});
    ExpectEqual(surface.Last(), "widget:shipyard\tPASSENGERSLIST\t1\tyard@7:11:select", "a candidate is selected with its token");
    captain.revision += 1;
    router.SetInterface(captain);
    router.OnEvent({"sheet-close", "click", ""});
    ExpectEqual(surface.Last(), "widget:shipyard\tCANCEL_ADD_OFFICER\t-1\tyard@7:11:press", "closing presses the game's cancel");

    // Without the engine's shipyard state the screen cannot work: generic layout, loudly.
    InterfaceModel broken = Load(models, "shipyard-main.json");
    broken.opening = 99;
    broken.widgets.erase(std::remove_if(broken.widgets.begin(), broken.widgets.end(), [](const Widget &w) { return w.type == "shipyard_state"; }),
                         broken.widgets.end());
    router.SetInterface(broken);
    surface.now += 1.0;
    router.Tick();
    Expect(router.Using() == "generic" && router.Rejection() == "ANDROID_SHIPYARD_STATE(shipyard_state)" && !surface.errors.empty(),
           "without its state widget the shipyard is rejected loudly");
    Expect(!HostOwned("shipyard"), "the platform host no longer draws the shipyard");
}

static void Atlas()
{
    PictureAtlas atlas;
    const size_t count = atlas.Load("; comment\n[GOODS]\nsTextureName\t= goods.tga\nwTextureWidth = 1024\nwTextureHeight = 512\n"
                                    "picture = Balls,128,256,256,384\n\n[icons]\nsTextureName = ..\\BATTLE_INTERFACE\\land_icon.tga\n"
                                    "wTextureWidth = 256\nwTextureHeight = 256\npicture = talk, 0,0,64,64 ; first\npicture = broken,1\n");
    Expect(count == 2, "two well-formed pictures");
    const AtlasPicture *balls = atlas.Find("goods", "BALLS");
    Expect(balls && balls->texture == "INTERFACES/goods" && balls->uv[0] == 0.125f && balls->uv[1] == 0.5f && balls->uv[2] == 0.25f && balls->uv[3] == 0.75f,
           "group and picture are case-insensitive; the rectangle becomes texture coordinates");
    const AtlasPicture *talk = atlas.Find("ICONS", "talk");
    Expect(talk && talk->texture == "BATTLE_INTERFACE/land_icon" && talk->uv[2] == 0.25f, "a texture outside INTERFACES is resolved");
    Expect(!atlas.Find("GOODS", "Rum") && !atlas.Find("ICONS", "broken"), "unknown and malformed pictures are not found");
}

static Json LoadJson(const std::string &directory, const char *name)
{
    std::ifstream file(directory + "/" + name, std::ios::binary);
    return Json::parse(file, nullptr, false);
}

namespace storm::ui::screens
{
std::string DialogueAnswerError(const std::string &text, int max_code_points);
}

static void Dialogue(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetState("dialogue", LoadJson(models, "dialogue.json"));
    ExpectEqual(surface.tag, "s-dialogue", "dialogue channel mounts the dialogue");
    ExpectEqual(surface.language, "russian", "the dialogue speaks the language the engine sends with it");
    Expect(surface.props["replies"].size() == 3 && surface.props["replies"][0]["hold"] == true, "replies listed, each with its long-press text");
    Expect(surface.props["page"] == 0 && surface.props["edit"].is_null(), "the speech starts on its first page, no editor");
    router.OnEvent({"reply-1", "click", ""});
    ExpectEqual(surface.Last(), "dialogue:4:1", "reply goes back with its generation");
    Expect(surface.props["replies"][1]["pressed"] == true, "the reply sent looks busy until the engine answers");
    router.OnEvent({"dialogue-next", "click", ""});
    Expect(surface.props["page"] == 1, "«next» turns the page (the pager clamps at the last one)");
    router.OnEvent({"dialogue-menu", "click", ""});
    ExpectEqual(surface.Last(), "pause:", "the menu button asks the platform host for its pause menu");
    Expect(!router.WantsBack(), "without an editor the back key stays the host's");
    router.SetState("dialogue", Json{{"active", false}});
    Expect(surface.tag.empty(), "inactive dialogue unmounts");
    router.SetState("weather", Json::object());
    Expect(surface.tag.empty() && surface.errors.size() == 1 && surface.errors[0] == "state channel weather has no consumer",
           "a channel without a consumer is an error");

    // Texts with the game's escaped line breaks, and an empty reply.
    router.SetState("dialogue", Json{{"active", true}, {"generation", 5}, {"speaker", "A"}, {"text", "one\\ntwo\\r"},
                                     {"choices", Json::array({Json{{"index", 0}, {"text", " "}}, Json{{"index", 2}, {"text", "x"}}, Json{{"text", "no index"}}})}});
    ExpectEqual(surface.props["text"].get<std::string>(), "one\ntwo", "escaped line breaks become lines");
    ExpectEqual(surface.props["replies"][0]["title"].get<std::string>(), "<dialogue.continue>", "an empty reply says what it does");
    Expect(surface.props["replies"].size() == 2 && surface.errors.size() == 2, "a choice without an index is reported, not shown");
}

static void DialogueText(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetState("dialogue", LoadJson(models, "dialogue-edit.json"));
    router.OnEvent({"reply-0", "click", ""});
    Expect(surface.commands.empty(), "a reply the player types opens the editor instead of answering");
    Expect(surface.props["edit"]["kind"] == "text" && surface.props["edit"]["value"] == "Аделина" && surface.props["edit"]["error"] == "",
           "the text editor proposes the reply's text");
    Expect(router.WantsBack(), "the back key closes the editor");
    router.OnEvent({"edit-field", "focus", ""});
    Expect(surface.text.text == "Аделина" && !surface.text.numeric && !surface.text.token.empty(), "the field opens the keyboard with the draft");
    router.SubmitText(surface.text.token, "Мария", true);
    ExpectEqual(surface.Last(), "dialogue:9:0:Мария", "the keyboard's «Done» sends the typed answer");
    Expect(surface.props["edit"]["actions"][1]["disabled"] == true, "«Done» waits for the engine's answer");
    router.OnEvent({"edit-done", "click", ""});
    Expect(surface.commands.size() == 1, "nothing goes twice while the engine decides");

    Json refused = LoadJson(models, "dialogue-edit.json");
    refused["editIndex"] = 0;
    refused["editError"] = "too_long";
    router.SetState("dialogue", refused);
    Expect(surface.props["edit"]["error"] == "<dialogue.too_long>" && surface.props["edit"]["value"] == "Мария" &&
               surface.props["edit"]["actions"][1]["disabled"] == false,
           "an answer the engine refuses keeps the draft and says why");
    router.OnEvent({"edit-done", "click", ""});
    ExpectEqual(surface.Last(), "dialogue:9:0:Мария", "the same answer can be sent again");
    router.SetState("dialogue", refused);
    Expect(surface.props["edit"]["error"] == "<dialogue.too_long>", "a repeated refusal is shown again");

    router.OnEvent({"edit-field", "click", ""});
    const size_t sent = surface.commands.size();
    router.SubmitText(surface.text.token, std::string("a\x01b"), true);
    Expect(surface.commands.size() == sent && surface.props["edit"]["error"] == "<dialogue.invalid>", "a control character is refused before sending");
    router.OnEvent({"edit-field", "click", ""});
    router.SubmitText(surface.text.token, "ignored", false);
    Expect(surface.props["edit"]["value"] == "a\x01b", "a cancelled keyboard keeps the draft");
    router.OnEvent({"edit-cancel", "click", ""});
    Expect(surface.props["edit"].is_null() && !router.WantsBack(), "«Cancel» closes the editor");
    router.OnEvent({"reply-0", "click", ""});
    router.Back();
    Expect(surface.props["edit"].is_null(), "the back key closes the editor");

    Json unknown = LoadJson(models, "dialogue-edit.json");
    unknown["editIndex"] = 0;
    unknown["editError"] = "strange";
    const size_t errors = surface.errors.size();
    router.SetState("dialogue", unknown);
    Expect(surface.errors.size() == errors + 1, "an unknown refusal code is reported");

    Json next = LoadJson(models, "dialogue-edit.json");
    router.OnEvent({"reply-0", "click", ""});
    next["generation"] = 10;
    router.SetState("dialogue", next);
    Expect(surface.props["edit"].is_null(), "a new utterance closes the editor");

    ExpectEqual(DialogueAnswerError("  Привет: \"да\" \xF0\x9F\x98\x80  ", 256), "", "multilingual input with an emoji passes");
    ExpectEqual(DialogueAnswerError("", 256), "", "an empty answer passes");
    std::string emoji;
    for (int i = 0; i < 256; ++i)
        emoji += "\xF0\x9F\x98\x80";
    ExpectEqual(DialogueAnswerError(emoji, 256), "", "256 code points pass (code points, not bytes or UTF-16 units)");
    ExpectEqual(DialogueAnswerError(emoji + "a", 256), "too_long", "the code point limit");
    ExpectEqual(DialogueAnswerError("abcd", 3), "too_long", "the reply's own limit");
    ExpectEqual(DialogueAnswerError("\xE2\x80\xA8", 256), "invalid_text", "a line separator is refused");
    ExpectEqual(DialogueAnswerError("\xC2\x85", 256), "invalid_text", "a C1 control is refused");
    ExpectEqual(DialogueAnswerError("a\xED\xA0\x80b", 256), "invalid_text", "a lone surrogate is refused");
    ExpectEqual(DialogueAnswerError(" \xC2\xA0", 256), "", "spaces pass");
}

static void DialogueNumber(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetState("dialogue", LoadJson(models, "dialogue-bet.json"));
    router.OnEvent({"reply-0", "click", ""});
    const Json edit = surface.props["edit"];
    Expect(edit["kind"] == "number" && edit["value"] == 100 && edit["max"] == 12500 && edit["money"] == 12500,
           "a whole-number proposal opens the number panel up to the hero's money");
    router.OnEvent({"edit-amount", "change", "3000"});
    Expect(surface.props["edit"]["value"] == 3000 && surface.props["edit"]["actions"][1]["sub"] == "3000", "the amount follows the control");
    router.OnEvent({"edit-amount", "commit", "99999"});
    Expect(surface.props["edit"]["value"] == 12500, "no more than the hero has");
    router.OnEvent({"edit-done", "click", ""});
    ExpectEqual(surface.Last(), "dialogue:11:0:12500", "the amount goes back as the typed answer");

    Json poor = LoadJson(models, "dialogue-bet.json");
    poor.erase("money");
    poor["generation"] = 12;
    router.SetState("dialogue", poor);
    router.OnEvent({"reply-0", "click", ""});
    Expect(surface.props["edit"]["max"] == 100 && surface.props["edit"]["money"] == -1, "without the hero's money the proposal is the limit");
}

static void Nations(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel model = Load(models, "nationrelation.json");
    router.SetInterface(model);
    ExpectEqual(router.Using(), "nationrelation", "the relations draw on the nations screen");
    ExpectEqual(surface.tag, "s-nations", "s-nations mounted");
    const Json &view = surface.props;
    Expect(view["columns"].size() == 5 && view["rows"].size() == 5 && view["reputation"].size() == 4, "five nations, four reputations");
    Expect(view["rows"][0]["cells"][0]["self"] == true && view["rows"][0]["cells"][1]["icon"] == "rel-neutral" &&
               view["rows"][0]["cells"][2]["icon"] == "rel-war" && view["rows"][1]["cells"][3]["icon"] == "rel-ally",
           "relation pictures become the legend glyphs");
    Expect(view["columns"][4]["image"]["icon"] == "nation-pirates" && view["rows"][1]["image"]["icon"] == "nation-france", "our flags for the nations");
    Expect(view["flag"]["label"] == "France" && view["apply"]["disabled"] == true && view["flagsDisabled"] == false &&
               view["flags"].size() == 5 && view["flags"][1]["selected"] == true && view["flags"][1]["id"] == "France" &&
               view["flags"][4]["icon"] == "nation-pirates" && view["flags"][4]["disabled"] == false && view["flags"][2]["disabled"] == true,
           "five flags to pick, the candidate selected, «change flag» not offered");
    Expect(view["reputation"][1]["bar"] == 67 && view["reputation"][1]["tone"] == "ok" && view["reputation"][2]["bar"] == 30 &&
               view["reputation"][2]["tone"] == "danger" && view["reputation"][0]["tone"] == "",
           "reputation numbers become bars (-100 … 100) with a tone");
    Expect(view["help"].is_null() && view["helpFlag"] == true && view["helpReputation"] == true, "help offered, not open");
    Expect(view["extras"]["rows"].empty() && view["extras"]["note"] == "", "every widget is known: no extras");
    ExpectEqual(view["title"].get<std::string>(), "Relations", "the game's title, trimmed");

    router.OnEvent({"flags", "pick", "France"});
    Expect(surface.commands.empty(), "picking the candidate again sends nothing");
    router.OnEvent({"flags", "pick", "Pirate"});
    ExpectEqual(surface.Last(), "widget:nationrelation\tRIGHTCHANGE_NATION\t-1\tnation@6:2:pick=Pirate", "a flag is picked directly");
    router.OnEvent({"flags", "pick", "France"});
    Expect(surface.commands.size() == 1, "one capability per revision: the engine consumed this generation");
    InterfaceModel next = model;
    next.revision += 1;
    for (Widget &widget : next.widgets)
        for (std::string &action : widget.actions)
            if (action.rfind("nation@6:2:", 0) == 0)
                action.replace(0, 11, "nation@6:3:");
    router.SetInterface(next);
    router.OnEvent({"flags", "pick", "Spain"});
    Expect(surface.commands.size() == 1, "a flag the hero may not raise is not picked");
    router.OnEvent({"flags", "pick", "Pirate"});
    ExpectEqual(surface.Last(), "widget:nationrelation\tRIGHTCHANGE_NATION\t-1\tnation@6:3:pick=Pirate", "the next pick in the next revision");
    next.revision += 1;
    router.SetInterface(next);
    router.OnEvent({"help-flag", "click", ""});
    ExpectEqual(surface.Last(), "widget:nationrelation\tFlagPic\t-2\tnation@6:3:help", "the flag's (i) asks for the game's help");
    next.revision += 1;
    router.SetInterface(next);
    router.OnEvent({"help-reputation", "click", ""});
    ExpectEqual(surface.Last(), "widget:nationrelation\tINFO_CLICK\t-2\tnation@6:3:help", "the reputation's (i) asks for the game's help");
    next.revision += 1;
    router.SetInterface(next);
    router.OnEvent({"header", "section", "ship"});
    ExpectEqual(surface.Last(), "widget:nationrelation\tI_SHIP\t-1\tnation@6:3:press", "a header section opens that section");
    next.revision += 1;
    router.SetInterface(next);
    router.OnEvent({"flag-apply", "click", ""});
    ExpectEqual(surface.Last(), "widget:nationrelation\tI_SHIP\t-1\tnation@6:3:press", "«change flag» not offered: nothing sent");
    for (Widget &widget : next.widgets)
        if (widget.node == "FLAGS")
        {
            widget.enabled = true;
            widget.actions = {"nation@6:3:press"};
        }
    next.revision += 1;
    router.SetInterface(next);
    router.OnEvent({"flag-apply", "click", ""});
    ExpectEqual(surface.Last(), "widget:nationrelation\tFLAGS\t-1\tnation@6:3:press", "«change flag» when offered");
    next.revision += 1;
    router.SetInterface(next);
    Expect(!router.WantsBack(), "no help open: back is the engine's");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "back", "← without an exit button: the engine's back");

    // The game's help sheet.
    InterfaceModel help = Load(models, "nationrelation-help.json");
    help.revision = next.revision + 1;
    router.SetInterface(help);
    Expect(surface.props["help"]["title"] == "Ship flag" && surface.props["help"]["text"] == "The flag your ship flies.\n\nForts fire at enemies.",
           "the help sheet: the game's texts, a tilde line becomes a blank one");
    Expect(router.WantsBack(), "back closes the help first");
    router.Back();
    ExpectEqual(surface.Last(), "widget:nationrelation\ttooltip_fader\t-3\tnation@6:3:close", "Android back closes the game's help");
    help.revision += 1;
    router.SetInterface(help);
    router.OnEvent({"help-close", "click", ""});
    ExpectEqual(surface.Last(), "widget:nationrelation\ttooltip_fader\t-3\tnation@6:3:close", "✕ closes the game's help");

    // Unknown relation picture: drawn as the game's picture and reported.
    InterfaceModel odd = Load(models, "nationrelation.json");
    odd.opening = 99;
    for (Widget &widget : odd.widgets)
        if (widget.type == "nation_row" && widget.index == 0)
            widget.cells[1].images[0].image = "Vassal";
    const size_t errors = surface.errors.size();
    router.SetInterface(odd);
    Expect(surface.errors.size() == errors + 1 && surface.props["rows"][0]["cells"][1]["image"]["texture"] == "atlas:relations/Vassal",
           "an unknown relation picture is the game's picture, reported");

    // Without the matrix the ported screen is not used, loudly.
    InterfaceModel broken = Load(models, "nationrelation.json");
    broken.opening = 100;
    broken.widgets.erase(std::remove_if(broken.widgets.begin(), broken.widgets.end(), [](const Widget &w) { return w.type == "nation_row"; }),
                         broken.widgets.end());
    router.SetInterface(broken);
    surface.now += 2.0;
    router.Tick();
    Expect(router.Using() == "generic" && router.Rejection() == "ANDROID_NATION_ROW(nation_row)", "without its rows the nations screen is rejected loudly");
}

// The application's own screens on the "launcher" channel: views, texts by key, verbs back.
static void Launcher()
{
    FakeSurface surface;
    Router router(surface);
    const auto verb = [&surface] {
        return surface.actions.empty() ? std::string() : surface.actions.back().node + " " + surface.actions.back().verb + " " + surface.actions.back().value;
    };
    const auto launcher = [](Json data) { return Json{{"screen", "launcher"}, {"language", "english"}, {"launcher", std::move(data)}}; };

    router.SetState("launcher", launcher({{"phase", "setup"}, {"library", false}, {"hasGame", false}, {"problem", "no_game"}}));
    ExpectEqual(surface.tag, "s-launcher", "launcher channel mounts the launcher");
    ExpectEqual(surface.language, "english", "launcher language follows the host");
    ExpectEqual(surface.props["view"].get<std::string>(), "setup", "no game: setup");
    Expect(surface.props["notices"].empty(), "no game is not an error notice");
    ExpectEqual(surface.props["setup"]["library"].get<std::string>(), "", "play flavor: no downloads link");
    router.OnEvent({"folder", "click", ""});
    ExpectEqual(verb(), "APP folder ", "folder row asks the host for the picker");
    router.OnEvent({"archive", "click", ""});
    ExpectEqual(verb(), "APP archive ", "archive row asks for the documents picker");
    router.OnEvent({"help", "click", ""});
    Expect(surface.props["extras"]["reading"].get<bool>(), "help opens the reading sheet");
    router.Back();
    Expect(!surface.props["extras"]["reading"].get<bool>(), "back closes the reading sheet first");

    router.SetState("launcher", launcher({{"phase", "setup"}, {"library", true}, {"hasGame", true}, {"files", true},
                                          {"result", {{"key", "import_archive_volumes"}, {"args", {"setup-2.bin"}}}}}));
    const Json notice = surface.props["notices"][0];
    ExpectEqual(notice["tone"].get<std::string>(), "danger", "failed import is a danger notice");
    ExpectEqual(notice["text"].get<std::string>(), "<launcher.result.import_archive_volumes> setup-2.bin <launcher.kept>",
                "failure names the files and keeps the old game");
    Expect(surface.props["back"].get<bool>(), "changing files of a ready game has ←");
    router.Back();
    ExpectEqual(verb(), "APP files-close ", "← returns to the ready launcher");

    router.SetState("launcher", launcher({{"phase", "ready"}, {"library", true}, {"hasGame", true},
                                          {"exit", {{"reason", "anr"}, {"when", "today"}}}}));
    ExpectEqual(surface.props["view"].get<std::string>(), "ready", "ready view");
    ExpectEqual(surface.props["notices"][0]["text"].get<std::string>(), "<launcher.exit_previous>", "previous exit noticed");
    router.OnEvent({"play", "click", ""});
    ExpectEqual(verb(), "APP play ", "play");
    router.OnEvent({"about", "click", ""});
    ExpectEqual(verb(), "APP about ", "about from the header");

    surface.now = 10;
    router.SetState("launcher", launcher({{"phase", "busy"}, {"hasGame", false},
                                          {"import", {{"stage", "copying"}, {"bytes", 0}, {"total", 2147483648.0}, {"canCancel", true}}}}));
    surface.now = 70;
    router.SetState("launcher", launcher({{"phase", "busy"}, {"hasGame", false},
                                          {"import", {{"stage", "copying"}, {"bytes", 1073741824.0}, {"total", 2147483648.0}, {"canCancel", true}}}}));
    ExpectEqual(surface.props["busy"]["title"].get<std::string>(), "<launcher.stage.copying>", "stage title");
    Expect(surface.props["busy"]["percent"].get<double>() == 50, "percent of the stage");
    Expect(surface.props["busy"]["status"].get<std::string>().find("<launcher.left>") != std::string::npos, "time left once the rate is known");
    router.OnEvent({"cancel", "click", ""});
    ExpectEqual(verb(), "APP cancel ", "cancel import");

    router.SetState("launcher", launcher({{"phase", "busy"}, {"hasGame", false},
                                          {"import", {{"stage", "choosing"}, {"canCancel", true},
                                                      {"choices", {{{"id", "a"}, {"name", "A"}, {"missing", {"a-2.bin"}}},
                                                                   {{"id", "b"}, {"name", "B"}, {"missing", Json::array()}}}}}}}));
    ExpectEqual(surface.props["view"].get<std::string>(), "choose", "several copies: the picker");
    Expect(surface.props["choose"]["rows"][0]["disabled"].get<bool>() && surface.props["choose"]["rows"][1]["selected"].get<bool>(),
           "a copy without its volumes cannot be chosen; the ready one is preselected");
    router.OnEvent({"roots", "click", ""});
    ExpectEqual(verb(), "APP choose b", "use the chosen copy");

    router.SetState("launcher", launcher({{"phase", "busy"}, {"hasGame", false},
                                          {"import", {{"stage", "reading"}, {"canCancel", true}, {"password", {{"for", "x.zip"}, {"retry", true}}}}}}));
    ExpectEqual(surface.props["password"]["title"].get<std::string>(), "<launcher.password_wrong>", "wrong password asks again");
    router.OnEvent({"password-field", "press", ""});
    Expect(surface.text.secret && !surface.text.token.empty(), "password field opens a masked editor");
    router.SubmitText(surface.text.token, "secret", true);
    ExpectEqual(verb(), "PASSWORD edit secret", "the typed password goes to the host");

    // Download library.
    const Json sets = Json::array({Json{{"id", "1"}, {"kind", "torrent"}, {"name", "Full"}, {"state", "downloading"}, {"downloaded", 50.0}, {"wanted", 100.0},
                                        {"rate", 10.0}, {"main", "pause"}, {"menu", {"details", "delete"}}},
                                   Json{{"id", "2"}, {"kind", "archive"}, {"name", "a.zip"}, {"state", "archive-ready"}, {"main", "install"}, {"menu", {"delete"}}}});
    router.SetState("launcher", Json{{"screen", "downloads"}, {"language", "english"}, {"downloads", {{"sets", sets}}}});
    ExpectEqual(surface.tag, "s-downloads", "downloads screen");
    ExpectEqual(surface.props["sets"][0]["action"].get<std::string>(), "pause", "an active set pauses inline");
    Expect(surface.props["sets"][0]["progress"].get<double>() == 50, "download progress");
    ExpectEqual(surface.props["sets"][1]["button"]["id"].get<std::string>(), "btn-2", "install is an inline button");
    router.OnEvent({"set-1", "action", "pause"});
    ExpectEqual(verb(), "APP pause 1", "pause glyph");
    router.OnEvent({"btn-2", "click", ""});
    ExpectEqual(verb(), "APP install 2", "install button");
    router.OnEvent({"set-1", "menu", ""});
    Expect(surface.props["sheet"]["actions"].size() == 2 && surface.props["sheet"]["actions"][1]["hold"].get<bool>(),
           "«⋯» lists the rare verbs; delete is held");
    router.OnEvent({"menu-delete", "click", ""});
    ExpectEqual(verb(), "APP delete 1", "held delete");
    router.SetState("launcher", Json{{"screen", "downloads"}, {"language", "english"},
                                     {"downloads", {{"sets", sets}, {"dialog", {{"kind", "files"}, {"id", "1"}, {"probe", false},
                                                    {"files", {{{"index", 4}, {"path", "a"}, {"bytes", 10}}, {{"index", 7}, {"path", "b"}, {"bytes", 20}}}}}}}}});
    router.OnEvent({"file-0", "change", ""});
    router.OnEvent({"dialog-download", "click", ""});
    ExpectEqual(verb(), "APP download 1:7", "unchecked files stay out of the download");

    router.SetState("launcher", Json{{"screen", "about"}, {"language", "english"},
                                     {"about", {{"app", {{"appSource", "https://example.org/app/tree/1"}, {"engineSource", "https://example.org/engine/tree/2"}}},
                                                {"notices", {"ui-RmlUi-MIT.txt", "storm-GPLv3.txt", "fmt-LICENSE.rst"}}}}});
    ExpectEqual(surface.tag, "s-about", "about screen");
    ExpectEqual(surface.props["notices"][0]["title"].get<std::string>(), "Storm Engine", "the engine's licence first");
    ExpectEqual(surface.props["notices"][1]["title"].get<std::string>(), "fmt", "LICENSE suffix dropped; then by name");
    ExpectEqual(surface.props["notices"][2]["title"].get<std::string>() + "|" + surface.props["notices"][2]["value"].get<std::string>(), "RmlUi|MIT",
                "notice named by its file");
    router.OnEvent({"notice-2", "click", ""});
    ExpectEqual(verb(), "APP license ui-RmlUi-MIT.txt", "a notice opens");
    router.OnEvent({"source-app", "click", ""});
    ExpectEqual(verb(), "APP open-url https://example.org/app/tree/1", "source link");

    const size_t errors = surface.errors.size();
    router.SetState("launcher", Json{{"screen", "nowhere"}});
    Expect(surface.errors.size() == errors + 1, "an unknown application screen is an error");

}

int main(int argc, char **argv)
{
    const std::string models = argc > 1 ? argv[1] : "tests/models";
    Admission_();
    Roles();
    Commands();
    Generic(models);
    Store(models);
    Commerce(models);
    Crew(models);
    Shipyard(models);
    Atlas();
    Dialogue(models);
    DialogueText(models);
    DialogueNumber(models);
    Nations(models);
    Launcher();
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("screen routing checks passed\n");
    return failures ? 1 : 0;
}
