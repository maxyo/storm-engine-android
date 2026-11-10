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
    router.SetInterface(Load(models, "store.json"));
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
    surface.commands.clear();
    router.OnEvent({"goods", "row", "2"});
    Expect(surface.commands.size() == 2 && surface.commands[0] == "widget:store\tQTY_CANCEL_BUTTON\t-1\tguard@7:goods:0:ship:0|press" &&
               surface.commands[1] == "widget:store\tTABLE_LIST\t2\tactivate@7:goods:2:ship:0",
           "another row: cancel the open deal, then open the new one");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "widget:store\tQTY_CANCEL_BUTTON\t-1\tguard@7:goods:0:ship:0|press", "← closes the deal first");
    router.OnEvent({"header", "subject-next", ""});
    Expect(surface.Last().find("SHIPS_SCROLL") == std::string::npos, "the ship cannot change during a deal");

    // Without its required table the store falls back to the generic layout.
    InterfaceModel broken = Load(models, "store.json");
    broken.widgets.erase(std::remove_if(broken.widgets.begin(), broken.widgets.end(), [](const Widget &w) { return w.node == "TABLE_LIST"; }),
                         broken.widgets.end());
    broken.opening = 8;
    router.SetInterface(broken);
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
    router.OnEvent({"goods", "change", "80"});
    router.OnEvent({"goods", "change", "90"});
    Expect(surface.commands.empty(), "steps wait for the amount to settle");
    surface.now += 0.5;
    router.Tick();
    Expect(surface.commands.size() == 2 && surface.commands[0] == "widget:storage\tQTY_EDIT\t-1\tguard@7:row:0|30" &&
               surface.commands[1] == "widget:storage\tQTY_OK_BUTTON\t-1\tguard@7:row:0|press",
           "then the last amount is entered and confirmed, in that order");
    surface.commands.clear();
    router.OnEvent({"goods", "commit", "10"});
    router.Tick();
    Expect(surface.commands.size() == 2 && surface.commands[0] == "widget:storage\tQTY_EDIT\t-1\tguard@7:row:0|-50", "the end of a drag applies at once; less than we hold = give");
    surface.commands.clear();
    router.OnEvent({"goods", "commit", "60"});
    router.Tick();
    Expect(surface.commands.size() == 1, "an unchanged amount confirms nothing");

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
    Expect(router.Using() == "generic" && router.Rejection() == "QTY_EDIT(edit)" && surface.errors.size() == 1, "without its edit box the crew screen is rejected loudly");
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
    router.SetState("vitals", Json::object());
    Expect(surface.tag.empty() && surface.errors.size() == 1 && surface.errors[0] == "state channel vitals has no consumer",
           "a channel without a consumer is an error");
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
    Atlas();
    Dialogue(models);
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("screen routing checks passed\n");
    return failures ? 1 : 0;
}
