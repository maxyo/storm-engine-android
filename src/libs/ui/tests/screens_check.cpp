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
    book.screen = "tradebook";
    book.opening = 5;
    book.widgets = {Make("TITLE", "heading", "Журнал торговли"), Make("I_SHIP", "button", "Корабли", {"press"}),
                    Make("I_SHIP_2", "button", "Корабли", {"press"}), Make("STR_BUTTONS", "text", "Корабли")};
    router.SetInterface(book);
    Expect(surface.props["sections"] == "tradebook" && surface.props["left"].empty(), "section buttons and their captions leave the content");
    router.OnEvent({"header", "section", "ship"});
    ExpectEqual(surface.Last(), "widget:tradebook\tI_SHIP\t-1\tpress", "a header section presses its button");

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

static void Repair(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    const InterfaceModel model = Load(models, "repair.json");
    router.SetInterface(model);
    Expect(router.Using() == "repair" && surface.tag == "s-repair" && surface.errors.empty(), "repair is drawn by the repair screen");
    const Json &parts = surface.props["parts"];
    Expect(parts.size() == 3 && parts[0]["q"]["value"] == 75 && parts[0]["q"]["min"] == 62 && parts[0]["q"]["max"] == 80,
           "hull: set 75 %, from 62 % now up to the 80 % the materials allow");
    ExpectEqual(parts[0]["need"], "<repair.planks> 17", "the planks the set amount takes (linear in the full-repair need)");
    Expect(parts[2]["short"] == true && parts[2]["disabled"] == true, "sails without sailcloth: the row is off and says why");
    Expect(surface.props["stock"][0]["value"] == "40" && surface.props["stock"][1]["value"] == "0", "planks and sailcloth in stock");
    ExpectEqual(surface.props["primary"]["sub"], "6 <save.hours> · <repair.planks> 17", "repair button: time and planks");
    std::string prefix = "widget:repair\t";

    surface.commands.clear();
    router.OnEvent({"hull", "commit", "78"});
    Expect(surface.commands.size() == 3 && surface.commands[0] == prefix + "REPAIR_RIGHT_H\t-1\tpress", "+3 % = three presses of the right arrow");
    Expect(surface.props["parts"][0]["q"]["value"] == 78, "the amount set is shown until the game answers");
    surface.commands.clear();
    router.OnEvent({"hull", "change", "70"});
    Expect(surface.commands.empty(), "a drag waits until it settles");
    surface.now += 0.5;
    router.Tick();
    Expect(surface.commands.size() == 8 && surface.commands[0] == prefix + "REPAIR_LEFT_H\t-1\tpress",
           "then 70 before the game answered: counted from the 78 the presses lead to, eight left");
    surface.commands.clear();
    router.OnEvent({"masts", "commit", "3"});
    Expect(surface.commands.size() == 1 && surface.commands[0] == prefix + "REPAIR_RIGHT_M\t-1\tpress", "masts move all at once: one press");

    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), prefix + "REPAIR_OK\t-1\tpress", "repair");
    router.OnEvent({"all-ships", "click", ""});
    ExpectEqual(surface.Last(), prefix + "REPAIR_ALL_SQUADRON\t-1\tpress", "all ships");
    router.OnEvent({"header", "subject-next", ""});
    ExpectEqual(surface.Last(), prefix + "SHIPS_SCROLL\t1\tselect@13:ship:1", "the next ship");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), prefix + "REPAIR_CANCEL\t-1\tpress", "back cancels");
}

static void Transfer(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    const std::string prefix = "widget:transfer_main\t";
    router.SetInterface(Load(models, "transfer-loot.json"));
    Expect(router.Using() == "transfer_main" && surface.tag == "s-transfer" && surface.errors.empty(), "loot is drawn by the transfer screen");
    Expect(surface.props["tabs"].size() == 2 && surface.props["tabs"][0]["selected"] == true, "cargo and crew tabs, cargo open");
    Expect(surface.props["stats"][1]["value"] == "<transfer.no_captain>", "the captured ship has no captain");
    Expect(surface.props["list"]["cols"][0]["label"] == "Корабль 1" && surface.props["list"]["rows"].size() > 5, "goods columns named by the ships");
    ExpectEqual(surface.props["primary"]["label"], "<transfer.take_all>", "take all the cargo");
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), prefix + "TAKE_GOODS\t-1\tpress", "take all = the game's take goods");
    router.OnEvent({"header", "tab", "1"});
    ExpectEqual(surface.Last(), prefix + "CREW_BUTTON\t-1\tpress", "crew tab opens the crew window");
    router.OnEvent({"action-CAPTAN_BUTTON", "click", ""});
    ExpectEqual(surface.Last(), prefix + "CAPTAN_BUTTON\t-1\tpress", "captain button");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), prefix + "EXIT_BTN\t-1\tpress", "back leaves through the game's exit (it asks about the ship)");

    router.OnEvent({"goods", "row", "0"});
    ExpectEqual(surface.Last(), prefix + "TABLE_LIST\t0\tselect", "a tap selects the cargo row (the game's rows carry no identity here)");
    router.SetInterface(Load(models, "transfer-loot-row.json"));
    const Json &shot = surface.props["list"]["rows"][0];
    Expect(shot["q"]["value"] == 100 && shot["q"]["max"] == 595 && shot["q"]["steps"][0] == 20, "the selected row: ours of both, in packs of 20");
    surface.commands.clear();
    router.OnEvent({"goods", "commit", "145"});
    Expect(surface.commands.size() == 3 && surface.commands[0] == prefix + "B_LEFT\t-1\tpress", "+45 = three packs taken (the game moves whole packs)");
    surface.commands.clear();
    router.OnEvent({"goods", "commit", "0"});
    Expect(surface.commands.size() == 8 && surface.commands[0] == prefix + "B_RIGHT\t-1\tpress",
           "all given before the game answered the three packs: counted from 160, eight packs");

    router.SetInterface(Load(models, "transfer-hire.json"));
    const Json &hire = surface.props["modal"];
    Expect(hire["step"] == "1/2" && hire["q"]["value"] == 8 && hire["q"]["max"] == 8, "defectors: all 8 willing are taken by default");
    ExpectEqual(hire["actions"][1]["label"], "<transfer.take> 8", "take 8");
    surface.commands.clear();
    router.OnEvent({"hire", "commit", "5"});
    Expect(surface.commands.size() == 3 && surface.commands[0] == prefix + "HIRE_QTY_REMOVE_BUTTON\t-1\tpress", "8 → 5 = three presses down");
    router.OnEvent({"hire-ok", "click", ""});
    ExpectEqual(surface.Last(), prefix + "HIRE_QTY_OK_BUTTON\t-1\tpress", "accept the defectors");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), prefix + "HIRE_QTY_CANCEL_BUTTON\t-1\tpress", "back refuses them");

    router.SetInterface(Load(models, "transfer-crew-capture.json"));
    const Json &fate = surface.props["modal"];
    Expect(fate["step"] == "2/2" && fate["actions"].size() == 3 && fate["actions"][0]["hold"] == true && fate["actions"][0]["variant"] == "danger",
           "crew fate after the defectors: overboard is held, never asked");
    router.OnEvent({"fate-kill", "click", ""});
    ExpectEqual(surface.Last(), prefix + "CAPTURE_CREW_KILL\t-1\tpress", "overboard once the hold completes");

    router.SetInterface(Load(models, "transfer-fate.json"));
    const Json &ship = surface.props["modal"];
    Expect(ship["actions"].size() == 2 && ship["actions"][0]["label"] == "<transfer.sink>" && ship["actions"][0]["hold"] == true, "a ship without a captain: sink (held) or keep");
    router.OnEvent({"fate-ship", "click", ""});
    ExpectEqual(surface.Last(), prefix + "REMOVE_ACCEPT_OFFICER\t-1\tpress", "sink = the game's yes");
    router.OnEvent({"fate-keep", "click", ""});
    ExpectEqual(surface.Last(), prefix + "REMOVE_CANCEL_OFFICER\t-1\tpress", "keep = back to appoint a captain");

    router.SetInterface(Load(models, "transfer-crew.json"));
    Expect(surface.props["crew"]["rows"][0]["q"]["value"] == 96 && surface.props["crew"]["rows"][0]["q"]["max"] == 154 &&
               surface.props["tabs"][1]["selected"] == true && surface.props["primary"].is_null(),
           "crew window: our 96 of both crews, crew tab open");
    surface.commands.clear();
    router.OnEvent({"crew", "commit", "100"});
    Expect(surface.commands.size() == 4 && surface.commands[0] == prefix + "CREW_LEFT\t-1\tpress", "4 more men = four takes");
    router.OnEvent({"header", "tab", "0"});
    ExpectEqual(surface.Last(), prefix + "CREW_EXIT_BTN\t-1\tpress", "cargo tab closes the crew window");
}

static void Alchemy(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    const std::string prefix = "widget:alchemy\t";
    router.SetInterface(Load(models, "alchemy.json"));
    Expect(router.Using() == "alchemy" && surface.tag == "s-alchemy" && surface.errors.empty(), "alchemy is drawn by the alchemy screen");
    const Json &recipes = surface.props["recipes"]["rows"];
    Expect(recipes.size() == 4 && recipes[0]["selected"] == true && recipes[0]["meta"] == "<alchemy.kind_ammo>" && recipes[1]["meta"] == "",
           "the four real recipes, the shown one selected, the item group as its word");
    const Json &parts = surface.props["parts"]["rows"];
    Expect(parts.size() == 2 && parts[0]["cells"][0]["text"] == "<alchemy.none>" && parts[1]["cells"][0] == "28" && parts[1]["cells"][1] == "1",
           "components: none of the bullets, 28 powder, one each per batch");
    Expect(surface.props["q"]["value"] == 0 && surface.props["q"]["disabled"] == true && surface.props["primary"]["disabled"] == true,
           "nothing to craft: no batches, the craft is off");
    router.OnEvent({"recipes", "row", "potion1"});
    ExpectEqual(surface.Last(), prefix + "ANDROID_ALCHEMY_RECIPE\t1\tchoose@11:potion1", "another recipe: the scroll's own tap, bound to the opening");
    surface.commands.clear();
    router.OnEvent({"recipes", "row", "cartridge"});
    Expect(surface.commands.empty(), "the shown recipe is not chosen again");
    router.OnEvent({"info", "click", ""});
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), prefix + "EXIT_BTN\t-1\tpress", "back leaves through the game's exit");

    router.SetInterface(Load(models, "alchemy-craft.json"));
    Expect(surface.props["q"]["value"] == 1 && surface.props["q"]["max"] == 22, "bullets bought: one batch set, up to 22");
    ExpectEqual(surface.props["primary"]["label"], "<alchemy.craft> ×3", "one batch makes three cartridges");
    router.OnEvent({"batches", "commit", "2"});
    ExpectEqual(surface.props["primary"]["label"], "<alchemy.craft> ×6", "two batches make six");
    ExpectEqual(surface.props["primary"]["sub"], "20 <alchemy.minutes>", "15 minutes and 5 for the second batch");
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), prefix + "ANDROID_ALCHEMY\t-1\tcraft@11:cartridge=2", "craft two batches of the shown recipe");
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

static void Dialogue(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    std::ifstream file(models + "/dialogue.json", std::ios::binary);
    const Json state = Json::parse(file, nullptr, false);
    router.SetState("dialogue", state);
    ExpectEqual(surface.tag, "s-dialogue", "dialogue channel mounts the dialogue");
    Expect(surface.props["replies"].size() == 3, "replies listed");
    router.OnEvent({"reply-1", "click", ""});
    ExpectEqual(surface.Last(), "dialogue:4:1", "reply goes back with its generation");
    router.SetState("dialogue", Json{{"active", false}});
    Expect(surface.tag.empty(), "inactive dialogue unmounts");
    router.SetState("weather", Json::object());
    Expect(surface.tag.empty() && surface.errors.size() == 1 && surface.errors[0] == "state channel weather has no consumer",
           "a channel without a consumer is an error");
}

static void Games(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    // Dice, our reroll: two dice marked (already paid), the cup throws them.
    InterfaceModel reroll = Load(models, "boal_dice-reroll.json");
    router.SetInterface(reroll);
    Expect(router.Using() == "boal_dice" && surface.tag == "s-table-game" && router.Rejection().empty() && surface.errors.empty(),
           "boal_dice is drawn by the table-game screen");
    Expect(surface.props["extras"]["rows"].empty() && surface.props["extras"]["note"] == "", "every exported node is known");
    Expect(surface.props["kind"] == "dice" && surface.props["locked"] == true, "back is locked during a round");
    Expect(surface.props["stats"][0]["value"] == "200" && surface.props["stats"][1]["value"] == "7 600", "stake and our money");
    Expect(surface.props["table"]["opponent"] == "Жак Дюбуа" && surface.props["table"]["portrait"] == "INTERFACES/PORTRAITS/128/face_377" &&
               surface.props["table"]["bank"] == "2 400" && surface.props["table"]["coins"] == "silver",
           "opponent, portrait, bank and the coins the script lays");
    const Json ours = surface.props["bottom"]["pieces"];
    Expect(ours.size() == 5 && ours[0]["pips"].size() == 3 && ours[0]["action"] == "mark" && ours[0]["disabled"] == false,
           "our dice: faces from the script pictures, tappable");
    Expect(ours[3]["lift"] == true && ours[3]["pips"].size() == 6 && ours[3]["cost"] == "−200" && ours[3].count("action") == 0,
           "a marked die keeps its face, rises, shows the stake and cannot be unmarked");
    Expect(surface.props["bottom"]["label"] == "" && surface.props["top"]["label"] == "<games.combo_6>",
           "the combination of a complete row");
    Expect(surface.props["buttons"].size() == 1 && surface.props["buttons"][0]["label"] == "<games.roll>" &&
               surface.props["buttons"][0]["sub"] == "2 <games.die_few>" && surface.props["buttons"][0]["disabled"] == false,
           "throw the two marked dice");
    router.OnEvent({"bottom", "mark", "2"});
    ExpectEqual(surface.Last(), "widget:boal_dice\tB_HeroDice2\t-1\tpress", "a tap on our die marks it in the script");
    router.OnEvent({"throw", "click", ""});
    ExpectEqual(surface.Last(), "widget:boal_dice\tB_PACK\t-1\tpress", "the primary throws the cup");
    router.OnEvent({"pack", "press", ""});
    ExpectEqual(surface.Last(), "widget:boal_dice\tB_PACK\t-1\tpress", "the cup itself throws too");
    const size_t sent = surface.commands.size();
    router.OnEvent({"header-back", "click", ""});
    Expect(surface.commands.size() == sent, "no way out during a round");

    // Nothing marked in our reroll: keep the dice (B_ICON_1).
    InterfaceModel keep = reroll;
    for (Widget &widget : keep.widgets)
        if (widget.node == "ANDROID_TABLE_GAME")
            widget.context["heroMix"] = "0,0,0,0,0";
    keep.revision = 2;
    router.SetInterface(keep);
    Expect(surface.props["buttons"][0]["label"] == "<games.keep>", "nothing marked: keep the dice");
    router.OnEvent({"throw", "click", ""});
    ExpectEqual(surface.Last(), "widget:boal_dice\tB_ICON_1\t-1\tpress", "keeping is the script's pass");

    // The round ends with our win: the bank is ours, another round or leave.
    InterfaceModel result = Load(models, "boal_dice-result.json");
    result.revision = 3;
    router.SetInterface(result);
    Expect(surface.props["locked"] == false && surface.props["table"]["bankLabel"] == "<games.won>" &&
               surface.props["table"]["bank"] == "+2 400" && surface.props["table"]["bankTone"] == "win",
           "after the round: what we won");
    Expect(surface.props["bottom"]["label"] == "<games.combo_4>" && surface.props["bottom"]["pieces"][0].count("action") == 0,
           "our combination; dice are not tappable between rounds");
    Expect(surface.props["buttons"][1]["id"] == "again" && surface.props["buttons"][1]["variant"] == "primary" &&
               surface.props["buttons"][1]["sub"] == "<games.stake_short> 200",
           "another round is the primary");
    router.OnEvent({"again", "click", ""});
    ExpectEqual(surface.Last(), "widget:boal_dice\tB_PACK\t-1\tpress", "another round is B_PACK");
    router.OnEvent({"leave", "click", ""});
    ExpectEqual(surface.Last(), "widget:boal_dice\tEXIT_BTN\t-1\tpress", "leave the table");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "widget:boal_dice\tEXIT_BTN\t-1\tpress", "back leaves between rounds");

    // The opponent has no money left: only leaving.
    router.SetInterface(Load(models, "boal_dice-end.json"));
    Expect(surface.props["buttons"][0]["id"] == "again" && surface.props["buttons"][0]["disabled"] == true &&
               surface.props["buttons"][1]["id"] == "leave" && surface.props["buttons"][1]["variant"] == "primary",
           "the opponent quits: leaving is the primary");

    // A fact the script lacks is an error, never a guess.
    InterfaceModel broken = Load(models, "boal_dice-reroll.json");
    broken.widgets.back().unavailable_reason = "table-game:missing=heroDice";
    surface.errors.clear();
    router.SetInterface(broken);
    Expect(surface.errors.size() == 1, "a missing script fact is reported");

    // Cards, our turn.
    FakeSurface cards;
    Router table(cards);
    table.SetInterface(Load(models, "boal_cards.json"));
    Expect(table.Using() == "boal_cards" && cards.tag == "s-table-game" && cards.errors.empty() && cards.props["kind"] == "cards",
           "boal_cards is drawn by the table-game screen");
    Expect(cards.props["table"]["coins"] == "gold" && cards.props["pack"][0]["kind"] == "deck", "gold coins, the deck");
    const Json hand = cards.props["bottom"]["pieces"];
    Expect(hand.size() == 3 && hand[0]["rank"] == "7" && hand[0]["suit"] == "spades" && hand[0]["red"] == false &&
               hand[1]["rank"] == "<games.rank_J>" && hand[1]["red"] == true,
           "our cards: rank and suit from the picture names");
    Expect(cards.props["bottom"]["label"] == "19 <games.point_many>", "our points (7 + J 2 + 10)");
    Expect(cards.props["top"]["pieces"].size() == 2 && cards.props["top"]["pieces"][0]["kind"] == "back" && cards.props["top"]["label"] == "",
           "the opponent's cards face down, no points");
    Expect(cards.props["buttons"][0]["id"] == "stand" && cards.props["buttons"][1]["id"] == "take" &&
               cards.props["buttons"][1]["sub"] == "−500" && cards.props["buttons"][1]["disabled"] == false,
           "stand, or take a card for the stake");
    table.OnEvent({"take", "click", ""});
    ExpectEqual(cards.Last(), "widget:boal_cards\tB_PACK\t-1\tpress", "take a card is B_PACK");
    table.OnEvent({"stand", "click", ""});
    ExpectEqual(cards.Last(), "widget:boal_cards\tB_ICON_1\t-1\tpress", "stand is B_ICON_1");

    // The opponent's turn: nothing to press.
    InterfaceModel opponent = Load(models, "boal_cards-opponent.json");
    opponent.revision = 2;
    table.SetInterface(opponent);
    Expect(cards.props["buttons"][0]["disabled"] == true && cards.props["buttons"][1]["disabled"] == true &&
               cards.props["buttons"][1]["reason"] == "<games.wait>",
           "the opponent plays: both buttons wait");

    // Opened: he has 20, the bank is his.
    InterfaceModel opened = Load(models, "boal_cards-result.json");
    opened.revision = 3;
    table.SetInterface(opened);
    Expect(cards.props["top"]["label"] == "20 <games.point_many>" && cards.props["top"]["pieces"][0]["rank"] == "<games.rank_A>",
           "the opponent's cards open with his points");
    Expect(cards.props["table"]["bankLabel"] == "<games.taken> Жак" && cards.props["table"]["bank"] == "3 000" &&
               cards.props["table"]["bankTone"] == "loss",
           "the bank went to the opponent");
    Expect(cards.props["buttons"][1]["id"] == "again" && cards.props["buttons"][1]["disabled"] == false, "another round");
}

static void GoodsOrder(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel list = Load(models, "goodstransfer.json");
    router.SetInterface(list);
    Expect(router.Using() == "goodstransfer" && surface.tag == "s-goods-order" && router.Rejection().empty() && surface.errors.empty(),
           "goodstransfer is drawn by the purchase list");
    Expect(surface.props["extras"]["rows"].empty(), "every exported node is known");
    const Json rows = surface.props["list"]["rows"];
    Expect(rows.size() == 6 && rows[0]["name"] == "Ядра" && rows[0]["image"]["texture"] == "atlas:GOODS/Balls" &&
               rows[0]["cells"][0] == "400" && rows[0]["cells"][1] == "600" && rows[4]["cells"][0] == "—",
           "one row per good: picture, in the hold, the order");
    Expect(rows[0].count("q") == 0, "no quantity until a good is opened");
    Expect(surface.props["subject"]["label"] == "Люггер 'Ласточка'" && surface.props["subject"]["sub"] == "1 <common.of> 2" &&
               surface.props["subject"]["prev"] == false && surface.props["subject"]["next"] == true,
           "the ship the list is for, 1 of 2");
    Expect(surface.props["contraband"]["label"] == "Закупать контрабанду" && surface.props["contraband"]["on"] == false, "contraband switch");

    router.OnEvent({"goods", "row", "3"});
    const Json open = surface.props["list"]["rows"][3];
    Expect(open["selected"] == true && open["q"]["value"] == 400 && open["q"]["min"] == 0 && open["q"]["max"] == 1000 &&
               open["q"]["label"] == "<goods.order>" && open["q"]["all"] == false,
           "a tap opens the good's order as a quantity");
    Expect(surface.commands.empty(), "opening a row sends nothing");
    router.OnEvent({"goods", "change", "500"});
    Expect(surface.props["list"]["rows"][3]["cells"][1] == "500", "the amount shows at once");
    Expect(surface.commands.empty(), "a step waits for the amount to settle");
    surface.now += 0.5;
    router.Tick();
    ExpectEqual(surface.Last(), "widget:goodstransfer\tANDROID_GOODS_ORDER\t3\torder@5:Powder=500",
                "the standing order is saved by itself, through one engine action");
    router.OnEvent({"goods", "commit", "520"});
    router.Tick();
    ExpectEqual(surface.Last(), "widget:goodstransfer\tANDROID_GOODS_ORDER\t3\torder@5:Powder=520", "the end of a drag saves at once");
    router.OnEvent({"contraband", "change", "1"});
    ExpectEqual(surface.Last(), "widget:goodstransfer\tCHECK_BUYCONTRABAND\t0\t1", "the contraband switch");
    router.OnEvent({"header", "subject-next", ""});
    ExpectEqual(surface.Last(), "widget:goodstransfer\tBTN_CHOOSESHIP_RIGHT\t-1\tpress", "the next ship through the game's arrow");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "widget:goodstransfer\tEXIT_BTN\t-1\tpress", "← leaves");

    // The game's own amount window: that good is open, the list takes no orders, ← closes the window.
    FakeSurface surface2;
    Router window(surface2);
    window.SetInterface(Load(models, "goodstransfer-window.json"));
    const Json powder = surface2.props["list"]["rows"][3];
    Expect(window.Using() == "goodstransfer" && powder["selected"] == true && powder["q"]["disabled"] == true &&
               surface2.props["list"]["rows"][0]["disabled"] == true,
           "the game's window marks its good and locks the list");
    window.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface2.Last(), "widget:goodstransfer\tTG_CANCEL_BUTTON\t-1\tpress", "← closes the game's amount window first");
}

static void SailEmblem(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "sailsgerald.json"));
    Expect(router.Using() == "sailsgerald" && surface.tag == "s-sail-emblem" && router.Rejection().empty() && surface.errors.empty(),
           "sailsgerald is drawn by the sail emblem screen");
    Expect(surface.props["extras"]["rows"].empty(), "every exported node is known");
    const Json emblems = surface.props["emblems"];
    Expect(emblems.size() == 7 && emblems[2]["selected"] == true && emblems[0]["applied"] == true && emblems[2]["texture"] == "ships/gerald/emblem_03",
           "emblems with their textures, the chosen one and the one on the sails");
    Expect(surface.props["title"] == "Выбрать символику на парус" && surface.props["stats"][0]["value"] == "12 480",
           "the game's caption and the hero's money");
    Expect(surface.props["primary"]["label"] == "<sails.apply>" && surface.props["primary"]["sub"] == "1 250" && surface.props["primary"]["disabled"] == false,
           "another emblem chosen: paint it for the price");
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:sailsgerald\tB_OK\t-1\tpress", "paint");
    router.OnEvent({"emblems", "emblem", "5"});
    ExpectEqual(surface.Last(), "widget:sailsgerald\tSCROLL_GERALD\t5\tselect@5:emblem_06.tga.tx", "a tap chooses that emblem in the game");
    router.OnEvent({"emblem-prev", "click", ""});
    ExpectEqual(surface.Last(), "widget:sailsgerald\tSCROLL_GERALD\t1\tselect@5:emblem_02.tga.tx", "‹ chooses the previous one");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "widget:sailsgerald\tEXIT_BTN\t-1\tpress", "← leaves");

    // The emblem on the sails chosen, and not enough money: take it off, disabled with the shortfall.
    router.SetInterface(Load(models, "sailsgerald-applied.json"));
    Expect(surface.props["primary"]["label"] == "<sails.remove>" && surface.props["primary"]["disabled"] == true &&
               surface.props["primary"]["reason"] == "<sails.short> 350" && surface.props["prev"] == false,
           "the current emblem: take it off; short of money: disabled with the shortfall");
    const size_t sent = surface.commands.size();
    router.OnEvent({"primary", "click", ""});
    Expect(surface.commands.size() == sent, "a disabled primary sends nothing");
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
    Games(models);
    Repair(models);
    Transfer(models);
    Alchemy(models);
    GoodsOrder(models);
    SailEmblem(models);
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("screen routing checks passed\n");
    return failures ? 1 : 0;
}
