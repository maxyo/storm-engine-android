// Host checks for screen routing: admission (KNOWN / REQUIRED / extras), the generic
// classification and the mapping of component events to engine actions. Synthetic
// models only (tests/models); no GL context, no game data.
#include "../src/hud/hud.h"
#include "../src/picture_atlas.h"
#include "../src/screens/controls.h"

#include <algorithm>
#include <cstdio>
#include <functional>
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
    std::vector<std::string> toasts;
    mutable std::vector<std::string> shown; // "<tone> (<source>): <text>", as the host logs them
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
    void Alert(const std::string &text) override
    {
        Toast(text);
    }
    void Toast(const std::string &line) override
    {
        toasts.push_back(line);
    }
    void SetLanguage(const std::string &game_language) override
    {
        language = game_language;
    }
    std::string Text(const std::string &key) override
    {
        return "<" + key + ">"; // the tables live in the pack; screens only name keys
    }
    void Shown(const std::string &source, const std::string &tone, const std::string &line) const override
    {
        shown.push_back(tone + " (" + source + "): " + line);
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
    book.screen = "questbook"; // a journal its ported screen rejects (no tabs): the generic layout draws it
    book.opening = 5;
    book.widgets = {Make("TITLE", "heading", "Журнал торговли"), Make("I_SHIP", "button", "Корабли", {"press"}),
                    Make("I_SHIP_2", "button", "Корабли", {"press"}), Make("STR_BUTTONS", "text", "Корабли")};
    router.SetInterface(book);
    surface.now += 2.0;
    router.Tick();
    Expect(surface.props["sections"] == "journal" && surface.props["left"].empty(), "section buttons and their captions leave the content");
    router.OnEvent({"header", "section", "ship"});
    ExpectEqual(surface.Last(), "widget:questbook\tI_SHIP\t-1\tpress", "a header section presses its button");

    router.CloseInterface();
    Expect(surface.tag.empty() && !router.Active(), "closing unmounts the screen");
}

// The shared quantity flow (AmountDraft, controls.h) on one open quantity control: changing the amount (steps,
// a drag, its end) sends nothing to the game, however long it settles; «Отмена» moves nothing; the confirm
// button sends exactly one move (`one_move` checks its commands); a second press before the game answers sends
// nothing more. `moves` tells a command that moves something; `reopen` brings the control back after «Отмена».
static void DraftFlow(const std::string &name, Router &router, FakeSurface &surface, const std::string &control, int value, const std::string &label,
                      const std::function<bool(const std::string &)> &moves, const std::function<bool(const std::vector<std::string> &)> &one_move,
                      const std::function<void()> &reopen)
{
    const auto dump = [&surface]() { return Json(surface.commands).dump(); };
    surface.commands.clear();
    router.OnEvent({control, "change", std::to_string(value - 1)});
    router.OnEvent({control, "change", std::to_string(value)});
    router.OnEvent({control, "commit", std::to_string(value)});
    surface.now += 2.0;
    router.Tick();
    Expect(surface.commands.empty(), name + ": a quantity change sends nothing to the game: " + dump());
    const Json &actions = surface.props["actions"];
    Expect(surface.props["primary"]["label"] == label && surface.props["primary"]["disabled"] == false && actions.is_array() && !actions.empty() &&
               actions.back()["id"] == "deal-cancel" && actions.back()["label"] == "<common.cancel>",
           name + ": the changed amount offers its confirm and «Отмена»: " + surface.props["primary"].dump() + actions.dump());
    router.OnEvent({"deal-cancel", "click", ""});
    router.Tick();
    Expect(std::none_of(surface.commands.begin(), surface.commands.end(), moves), name + ": «Отмена» moves nothing: " + dump());
    Expect(surface.props["primary"]["label"] != label, name + ": «Отмена» drops the amount set");
    reopen();
    router.OnEvent({control, "commit", std::to_string(value)});
    surface.commands.clear();
    router.OnEvent({"primary", "click", ""});
    router.Tick();
    Expect(one_move(surface.commands), name + ": the confirm sends one move: " + dump());
    surface.commands.clear();
    router.OnEvent({"primary", "click", ""});
    router.Tick();
    Expect(surface.commands.empty() && surface.props["primary"]["busy"] == true, name + ": a second press before the game answers sends nothing: " + dump());
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
    {
        FakeSurface sel_surface;
        Router sel(sel_surface);
        sel.SetInterface(Load(models, "store.json"));
        sel.OnEvent({"goods", "row", "1"});
        // The game selects the tapped row in one loop and opens its window in the next: the frame
        // between is not shown, the selection and its deal appear together.
        InterfaceModel chosen = Load(models, "store.json");
        chosen.revision = 30;
        for (Widget &widget : chosen.widgets)
            if (widget.node == "TABLE_LIST")
                widget.value = widget.index == 1 ? "1" : "0";
        const Json before = sel_surface.props;
        sel.SetInterface(chosen);
        Expect(sel_surface.props == before, "the selection without its deal is held back: the screen stays as it was");
        InterfaceModel opened = Load(models, "store-deal.json");
        opened.revision = 31;
        for (Widget &widget : opened.widgets)
            if (widget.node == "TABLE_LIST")
                widget.value = widget.index == 1 ? "1" : "0";
        sel.SetInterface(opened);
        Expect(sel_surface.props["list"]["rows"][1]["selected"] == true && sel_surface.props.contains("deal") && !sel_surface.props["deal"].is_null(),
               "the deal arrives: the row selected and its deal in the same frame: " + sel_surface.props["list"]["rows"][1].dump());
        // A tap whose window never comes is shown selected after the hold (and said).
        sel.OnEvent({"goods", "row", "2"});
        InterfaceModel lone = Load(models, "store.json");
        lone.revision = 32;
        for (Widget &widget : lone.widgets)
            if (widget.node == "TABLE_LIST")
                widget.value = widget.index == 2 ? "1" : "0";
        sel.SetInterface(lone);
        Expect(sel_surface.props["list"]["rows"][2]["selected"] != true, "held while its window may still come");
        sel_surface.now += 0.6;
        sel.Tick();
        Expect(sel_surface.props["list"]["rows"][2]["selected"] == true && !sel_surface.errors.empty(),
               "no window within the hold: shown selected, and said: " + (sel_surface.errors.empty() ? std::string() : sel_surface.errors.back()));
        sel_surface.errors.clear();
    }

    // The open deal (store-deal.json: «Ядра», 200 in the hold, 1 840 in the store, 5 to buy and 3 to sell a pack of 20
    // that weighs 1 cwt; the hold is 189 / 199, the purse 1 500). The model is the game's own window mid-deal
    // (QTY_EDIT 100, «Деньги 25, вес 5 ц»): none of that moves the control, which is the player's.
    const InterfaceModel open_deal = Load(models, "store-deal.json");
    router.SetInterface(open_deal);
    const auto quantity = [&surface]() { return surface.props["deal"]; };
    // store.c ChangeQTY_EDIT, written out again here: makeint(price * qty / iUnits + 0.5) and
    // GetGoodWeightByType (whole packs, rounded at 0.05).
    const auto game_total = [](int price, int amount) { return static_cast<int>(static_cast<double>(price) * amount / 20 + 0.5); };
    const auto game_weight = [](int amount) { return static_cast<int>(((amount + 20 - 1) / 20) * 1.0 + 0.05); };
    Expect(quantity()["value"] == 200 && quantity()["origin"] == 200 && quantity()["min"] == 0 && quantity()["max"] == 400 &&
               surface.props["primary"].is_null(),
           "a deal opens at the pivot (what we hold, amount 0) between all of ours sold and the most the hold and purse take, with nothing to confirm: " + quantity().dump());
    Expect(quantity()["zones"] == true && quantity()["lessLabel"] == "<trade.sell>" && quantity()["moreLabel"] == "<trade.buy>",
           "trade zones: selling left of the pivot, buying right");
    {
        // A drag of 200 samples: no round-trip to the game, the pivot and both ends never move, every sample
        // shows the game's total and weight, the button never waits. The hold stops the amount at the most store.c
        // takes as it is: 199 - 189 = 10 cwt of room = 10 packs = 200 pieces (201 would be an eleventh pack).
        surface.commands.clear();
        bool steady = true, exact = true, ready = true;
        std::string first_wrong;
        for (int sample = 1; sample <= 200; ++sample)
        {
            router.OnEvent({"goods", "change", std::to_string(200 + sample)});
            const int amount = std::min(sample, 200);
            const Json q = quantity();
            steady = steady && q["origin"] == 200 && q["min"] == 0 && q["max"] == 400 && q["value"] == 200 + amount;
            const std::string label = "<trade.buy> " + std::to_string(amount);
            const std::string sub = std::to_string(game_total(5, amount)) + " \xC2\xB7 " + std::to_string(game_weight(amount)) + " <transfer.cwt>";
            if (surface.props["primary"]["label"] != label || surface.props["primary"]["sub"] != sub)
            {
                if (exact)
                    first_wrong = "sample " + std::to_string(sample) + ": " + surface.props["primary"].dump() + " want " + label + " / " + sub;
                exact = false;
            }
            ready = ready && surface.props["primary"]["busy"] == false && surface.props["primary"]["disabled"] == false;
        }
        Expect(surface.commands.empty(), "a drag of 200 samples sends nothing to the game: " + std::to_string(surface.commands.size()) + " commands");
        Expect(steady, "the pivot and the ends stay put through the drag; the hold's room stops the amount at 200");
        Expect(exact, "every sample shows the game's total and weight (store.c): " + first_wrong);
        Expect(ready, "the button is ready at every sample: no spinner while choosing");
    }
    router.OnEvent({"goods", "change", "300"});
    Expect(surface.props["primary"]["label"] == "<trade.buy> 100" && surface.props["primary"]["sub"] == "25 \xC2\xB7 5 <transfer.cwt>",
           "100 bought: 25 and 5 cwt, as the game's own window says for it («Деньги 25, вес 5 ц»)");
    Expect(surface.props["stats"][0]["value"] == "189" && surface.props["stats"][0]["preview"] == "194" &&
               surface.props["stats"][1]["value"] == "1 500" && surface.props["stats"][1]["preview"] == "1 475",
           "the header shows what the deal changes: the hold 189 → 194, the purse 1 500 → 1 475: " + surface.props["stats"].dump());
    {
        // The game republishes its window while the amount is chosen (its own QTY_EDIT, labels): nothing moves.
        InterfaceModel again = open_deal;
        again.revision += 1;
        for (Widget &widget : again.widgets)
            if (widget.node == "QTY_INFO_SHIP_QTY")
                widget.label = "777";
        router.SetInterface(again);
        Expect(quantity()["value"] == 300 && quantity()["origin"] == 200, "a revision of the game while choosing does not move the control");
    }
    router.OnEvent({"goods", "commit", "150"});
    Expect(surface.commands.empty() && surface.props["primary"]["label"] == "<trade.sell> 50" &&
               surface.props["primary"]["sub"] == std::to_string(game_total(3, 50)) + " \xC2\xB7 3 <transfer.cwt>" &&
               surface.props["stats"][0]["preview"] == "186" && surface.props["stats"][1]["preview"] == "1 508",
           "left of the pivot is a sale: 50 for 8, the hold lighter by 3 cwt; still nothing sent: " + surface.props["primary"].dump());
    router.OnEvent({"goods", "change", "0"});
    Expect(quantity()["value"] == 0 && surface.props["primary"]["label"] == "<trade.sell> 200", "«Все» to the left: all we hold");
    router.OnEvent({"goods", "change", "2040"});
    Expect(quantity()["value"] == 400 && surface.props["primary"]["label"] == "<trade.buy> 200", "«Все» to the right: what the hold takes");
    {
        // The purse stops it too: 60 pesos buy 241 pieces at 5 for 20 (makeint(5 * 241 / 20 + 0.5) = 60, 242 cost 61).
        FakeSurface poor_surface;
        Router poor(poor_surface);
        InterfaceModel model = open_deal;
        for (Widget &widget : model.widgets)
        {
            if (widget.node == "OUR_GOLD")
                widget.label = "Наши деньги 60 песо";
            if (widget.node == "CAPACITY")
                widget.label = "Трюм:\n189 / 700";
        }
        poor.SetInterface(model);
        poor.OnEvent({"goods", "change", "2040"});
        Expect(poor_surface.props["deal"]["value"] == 441 && poor_surface.props["primary"]["sub"] == "60 \xC2\xB7 13 <transfer.cwt>",
               "the purse limits the purchase as store.c does: " + poor_surface.props["primary"].dump());
    }
    {
        // A deal of which not even one can be had: the rail stays on the pivot, a tap or a drag says why, and the
        // dock shows the deal disabled with the same reason. (Nothing to sell either: none in the hold.)
        const auto blocked = [&](const char *file, const char *node, const char *label, const char *reason) {
            InterfaceModel model = Load(models, file);
            for (Widget &widget : model.widgets)
            {
                if (widget.node == node)
                    widget.label = label;
                if (widget.node == "TABLE_LIST" && widget.value == "1")
                    for (Cell &cell : widget.cells)
                        if (cell.column == 0)
                            cell.text = "0";
            }
            FakeSurface blocked_surface;
            Router blocked_router(blocked_surface);
            blocked_router.SetInterface(model);
            const Json q = blocked_surface.props["deal"];
            blocked_router.OnEvent({"goods", "change", "20"});
            const Json after = blocked_surface.props["deal"];
            Expect(q["value"] == 0 && q["max"] == 0 && q["disabled"] == true && q["reason"] == reason && after["value"] == 0 &&
                       blocked_surface.commands.empty() && blocked_surface.toasts.size() == 1 && blocked_surface.toasts[0] == reason &&
                       blocked_surface.props["primary"]["disabled"] == true && blocked_surface.props["primary"]["reason"] == reason,
                   std::string("a deal that cannot start says ") + reason + ": " + q.dump() + blocked_surface.props["primary"].dump());
        };
        blocked("cannons-deal.json", "OUR_GOLD", "Наши деньги 100 песо", "<trade.no_money>"); // a cannon is 290
        blocked("store-deal.json", "CAPACITY", "Трюм:\n199 / 199", "<trade.no_room>");
        // With room for some, the «+» side says why it ends where it does; selling stays open.
        router.OnEvent({"goods", "change", "2040"});
        Expect(quantity()["moreReason"] == "<trade.no_room>" && quantity()["disabled"] == false && surface.toasts.back() == "<trade.no_room>",
               "past what the hold takes: the reason, the control still live");
    }
    // Only the confirmation goes to the game: zero and the signed amount (zero leaves a previous «give»), then its OK.
    router.OnEvent({"goods", "change", "300"});
    surface.commands.clear();
    router.OnEvent({"primary", "click", ""});
    router.Tick();
    {
        const std::string edit = "widget:store\tQTY_EDIT\t-1\tguard@7:goods:0:ship:0|";
        Expect(surface.commands.size() == 3 && surface.commands[0] == edit + "0" && surface.commands[1] == edit + "100" &&
                   surface.commands[2] == "widget:store\tQTY_OK_BUTTON\t-1\tguard@7:goods:0:ship:0|press",
               "«Купить 100»: the amount and the game's OK in one flush: " + Json(surface.commands).dump());
    }
    {
        // The store's «Купить / Продать N» is the same shared flow.
        const std::string edit = "widget:store\tQTY_EDIT\t-1\tguard@7:goods:0:ship:0|";
        const auto moves = [](const std::string &command) { return command.find("\tQTY_EDIT\t") != std::string::npos || command.find("\tQTY_OK_BUTTON\t") != std::string::npos; };
        FakeSurface draft_surface;
        Router draft_router(draft_surface);
        draft_router.SetInterface(open_deal);
        DraftFlow("store", draft_router, draft_surface, "goods", 150, "<trade.sell> 50", moves,
                  [&](const std::vector<std::string> &commands) {
                      return commands == std::vector<std::string>{edit + "0", edit + "-50", "widget:store\tQTY_OK_BUTTON\t-1\tguard@7:goods:0:ship:0|press"};
                  },
                  [&]() {
                      ExpectEqual(draft_surface.Last(), "widget:store\tQTY_CANCEL_BUTTON\t-1\tguard@7:goods:0:ship:0|press", "store: «Отмена» closes the game's window");
                      InterfaceModel again = open_deal;
                      again.revision += 5;
                      draft_router.SetInterface(again);
                  });
    }
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
    ExpectEqual(router.Rejection(), "TABLE_LIST(choice|table)", "the rejection names the missing required node");
    Expect(surface.errors.size() == 1 &&
               surface.errors[0] == "screen store: required nodes missing or mistyped: TABLE_LIST(choice|table); drawn by the generic layout",
           "a ported screen that cannot be used is an error");
    broken.revision = 99;
    router.SetInterface(broken);
    Expect(surface.errors.size() == 1, "reported once per node set");
    broken.widgets.pop_back();
    router.SetInterface(broken);
    Expect(surface.errors.size() == 2, "and again when the node set changes");
    router.SetInterface(Load(models, "store.json"));
    Expect(router.Using() == "store" && router.Rejection().empty(), "the ported screen returns when its nodes are back");

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

    // The game's own storage figures: the hold on one line, the load of the storage in a sentence.
    router.SetInterface(Load(models, "storage.json"));
    Expect(surface.props["stats"].size() == 2 && surface.props["stats"][0]["value"] == "189 / 700" &&
               surface.props["stats"][1]["label"] == "Склад" && surface.props["stats"][1]["value"] == "0 / 50000 ц.",
           "storage: hold and storage load in the header");

    // Storage: a free transfer is confirmed like a money deal (AmountDraft): «Взять / Отдать N», «Отмена».
    router.SetInterface(Load(models, "storage-deal.json"));
    Expect(surface.props["primary"].is_null() && surface.props["list"]["cols"].size() == 2, "storage: two amount columns, nothing to confirm at the pivot");
    ExpectEqual(surface.props["list"]["rows"][0]["image"]["texture"], "atlas:GOODS/Sugar", "rows carry the game's picture reference");
    Expect(surface.props["stats"].size() == 2 && surface.props["stats"][1]["value"] == "2328 / 10000", "hold and storage figures in the header");
    {
        const std::string edit = "widget:storage\tQTY_EDIT\t-1\tguard@7:row:0|";
        const std::string ok = "widget:storage\tQTY_OK_BUTTON\t-1\tguard@7:row:0|press";
        const std::string cancel = "widget:storage\tQTY_CANCEL_BUTTON\t-1\tguard@7:row:0|press";
        const auto moves = [](const std::string &command) { return command.find("\tQTY_EDIT\t") != std::string::npos || command.find("\tQTY_OK_BUTTON\t") != std::string::npos; };
        const auto reopen = [&]() {
            ExpectEqual(surface.Last(), cancel, "storage: «Отмена» closes the game's window");
            InterfaceModel again = Load(models, "storage-deal.json");
            again.revision += 5;
            router.SetInterface(again);
        };
        DraftFlow("storage: take", router, surface, "goods", 90, "<trade.take> 30", moves,
                  [&](const std::vector<std::string> &commands) {
                      return commands == std::vector<std::string>{edit + "0", edit + "30", ok};
                  },
                  reopen);
        router.SetInterface(Load(models, "storage.json")); // the game closed its window after the transfer
        Expect(surface.commands.empty() && surface.props["primary"]["label"] != "<trade.take> 30", "the moved row is not opened again by itself");
        router.SetInterface(Load(models, "storage-deal.json"));
        DraftFlow("storage: give", router, surface, "goods", 10, "<trade.give> 50", moves,
                  [&](const std::vector<std::string> &commands) {
                      return commands == std::vector<std::string>{edit + "0", edit + "-50", ok};
                  },
                  reopen);
    }

    // Chest / body: the game's category tabs, take all, remove body.
    router.SetInterface(Load(models, "itemsbox.json"));
    Expect(surface.props["list"]["filters"].size() == 4 && surface.props["list"]["filters"][0]["selected"] == true, "itemsbox: the game's tabs");
    Expect(surface.props["list"]["cols"][0]["label"] == "<trade.yours>" && surface.props["list"]["cols"][1]["label"] == "<trade.theirs>",
           "itemsbox: the game names both amount columns alike, so the string table names them");
    ExpectEqual(surface.language, "russian", "the router passes the model's language on");
    ExpectEqual(surface.props["primary"]["label"], "Взять всё", "itemsbox: take all is the primary");
    Expect(surface.props["actions"].size() == 1 && surface.props["actions"][0]["label"] == "Убрать тело", "itemsbox: remove body beside it");
    router.OnEvent({"goods", "filter", "2"});
    ExpectEqual(surface.Last(), "widget:itemsbox\tTABBTN_3\t-1\tpress", "a tab presses the game's tab button");
    {
        // The chest's four tabs carry our short captions; a fifth (a mod's) keeps the game's.
        const Json &chips = surface.props["list"]["filters"];
        Expect(chips.size() == 4 && chips[0]["label"] == "<common.all>" && chips[1]["label"] == "<trade.tab_mine>" &&
                   chips[2]["label"] == "<trade.tab_here>" && chips[3]["label"] == "<trade.tab_weapons>",
               "itemsbox: the game's tabs as chips with our short captions: " + chips.dump());
        InterfaceModel modded = Load(models, "itemsbox.json");
        Widget button, caption;
        for (const Widget &widget : modded.widgets)
        {
            if (widget.node == "TABBTN_4")
                button = widget;
            if (widget.node == "TABSTR_4")
                caption = widget;
        }
        button.node = "TABBTN_5";
        caption.node = "TABSTR_5";
        caption.label = "Карты";
        modded.widgets.push_back(button);
        modded.widgets.push_back(caption);
        FakeSurface mod_surface;
        Router mod(mod_surface);
        mod.SetInterface(modded);
        Expect(mod_surface.props["list"]["filters"].size() == 5 && mod_surface.props["list"]["filters"][4]["label"] == "Карты",
               "itemsbox: a tab we do not know is appended with the game's caption");
        // The hand at a row's end: the game opens the row's amount window, then all of the stack is taken.
        const Json &loot = mod_surface.props["list"]["rows"];
        size_t with = loot.size();
        for (size_t i = 0; i < loot.size(); ++i)
            if (loot[i]["quick"] == true && with == loot.size())
                with = i;
        Expect(mod_surface.props["list"]["quick"] == "hand" && with < loot.size(), "itemsbox: rows with something to take offer the quick take");
        mod_surface.commands.clear();
        mod.OnEvent({"goods", "quick", loot[with]["id"].get<std::string>()});
        Expect(mod_surface.commands.size() == 1 && mod_surface.commands[0].find("\tTABLE_LIST\t") != std::string::npos &&
                   mod_surface.commands[0].find("activate@") != std::string::npos,
               "quick take: first the row's amount window: " + (mod_surface.commands.empty() ? std::string() : mod_surface.commands[0]));
    }
    {
        // The amount window opened for the quick take: the whole stack is entered and confirmed, the window is not reopened.
        FakeSurface quick_surface;
        Router quick(quick_surface);
        quick.SetInterface(Load(models, "itemsbox.json"));
        InterfaceModel open = Load(models, "itemsbox-deal.json");
        std::string row_index;
        int there = 0;
        for (const Widget &widget : open.widgets)
            if (widget.node == "TABLE_LIST" && widget.value == "1")
                row_index = std::to_string(widget.index), there = std::atoi(widget.CellText(4).c_str());
        quick.OnEvent({"goods", "quick", row_index});
        quick_surface.commands.clear();
        open.revision += 10;
        quick.SetInterface(open);
        quick.Tick();
        Expect(quick_surface.commands.size() == 3 && quick_surface.commands[1].find("QTY_EDIT") != std::string::npos &&
                   quick_surface.commands[1].rfind("|" + std::to_string(there)) == quick_surface.commands[1].size() - std::to_string(there).size() - 1 &&
                   quick_surface.commands[2].find("QTY_OK_BUTTON") != std::string::npos,
               "quick take: all of the other side, confirmed: " + Json(quick_surface.commands).dump());
    }
    {
        // A chest (the game hides its take-all there): «Взять всё» asks the engine to run the game's
        // own take-all in one go (ANDROID_TAKE_ALL); what is left after it was too heavy to carry.
        const auto chest = [&](int revision, std::map<std::string, std::string> there, bool offered) {
            InterfaceModel model = Load(models, "itemsbox.json");
            for (Widget &widget : model.widgets)
                if (widget.node == "GETALL_BUTTON")
                    widget.node = "ANDROID_TAKE_ALL"; // as the engine publishes it for a chest
            std::erase_if(model.widgets, [offered](const Widget &widget) {
                return widget.node == "DELBODY_BUTTON" || (!offered && widget.node == "ANDROID_TAKE_ALL");
            });
            model.revision = static_cast<uint64_t>(revision);
            for (Widget &widget : model.widgets)
                if (widget.node == "TABLE_LIST" && there.count(widget.row_id))
                    for (Cell &cell : widget.cells)
                        if (cell.column == 4)
                            cell.text = there[widget.row_id];
            return model;
        };
        const std::map<std::string, std::string> empty{{"row:0", "0"}, {"row:1", "0"}, {"row:2", "0"}, {"row:3", "0"}};
        FakeSurface chest_surface;
        Router box(chest_surface);
        box.SetInterface(chest(1, {}, true));
        ExpectEqual(chest_surface.props["primary"]["label"], "<trade.take_all>", "a chest: our take all is the primary");
        chest_surface.commands.clear();
        box.OnEvent({"primary", "click", ""});
        box.Tick();
        Expect(chest_surface.commands.size() == 1 && chest_surface.commands[0] == "widget:itemsbox\tANDROID_TAKE_ALL\t-1\tpress",
               "take all: one request for the game's take-all, no stack by stack: " + Json(chest_surface.commands).dump());
        box.SetInterface(chest(2, empty, true));
        Expect(chest_surface.toasts.empty() && chest_surface.props["primary"].is_null(), "all of it taken: no toast, an empty chest offers no take all");
        box.SetInterface(chest(3, {}, true));
        box.OnEvent({"primary", "click", ""});
        box.Tick();
        box.SetInterface(chest(4, {{"row:0", "0"}, {"row:1", "0"}, {"row:2", "0"}, {"row:3", "1"}}, true));
        Expect(chest_surface.toasts.size() == 1 && chest_surface.toasts[0] == "<trade.too_heavy>",
               "a stack left after the game's take-all: the weight limit, said once: " + Json(chest_surface.toasts).dump());
        box.SetInterface(chest(5, {{"row:0", "0"}, {"row:1", "0"}, {"row:2", "0"}, {"row:3", "1"}}, true));
        Expect(chest_surface.toasts.size() == 1, "… only for the answer to the press");
        box.SetInterface(chest(6, {}, false));
        Expect(chest_surface.props["primary"].is_null(), "without the engine's take-all no button");
    }
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:itemsbox\tGETALL_BUTTON\t-1\tpress", "take all");
    router.OnEvent({"action-0", "click", ""});
    ExpectEqual(surface.Last(), "widget:itemsbox\tDELBODY_BUTTON\t-1\tpress", "remove body");
    router.SetInterface(Load(models, "itemsbox-deal.json"));
    Expect(surface.props["deal"]["steps"].size() == 2 && surface.props["deal"]["max"] == 26, "single items: steps 1 and 10, up to both sides");
    // A chest, a body, an officer's pockets: the owner's bug was a pick of the amount moving the items at once.
    {
        const auto moves = [](const std::string &command) { return command.find("\tQTY_EDIT\t") != std::string::npos || command.find("\tQTY_OK_BUTTON\t") != std::string::npos; };
        DraftFlow("itemsbox", router, surface, "goods", 25, "<trade.take> 2", moves,
                  [](const std::vector<std::string> &commands) {
                      return commands.size() == 3 && commands[1].find("\tQTY_EDIT\t") != std::string::npos && commands[1].substr(commands[1].size() - 2) == "|2" &&
                             commands[2].find("\tQTY_OK_BUTTON\t") != std::string::npos;
                  },
                  [&]() {
                      ExpectEqual(surface.Last().substr(0, surface.Last().find("\tguard")), "widget:itemsbox\tQTY_CANCEL_BUTTON\t-1", "itemsbox: «Отмена» closes the game's window");
                      InterfaceModel again = Load(models, "itemsbox-deal.json");
                      again.revision += 5;
                      router.SetInterface(again);
                  });
    }

    // The row's own facts: the trade type by the name's colour, the pack in the dock's second line.
    router.SetInterface(Load(models, "store.json"));
    router.SetInterface(Load(models, "store-deal.json"));
    ExpectEqual(surface.props["list"]["rows"][5]["meta"], "<trade.export>", "the trade type the colour tells is named from the string table");
    ExpectEqual(surface.props["list"]["rows"][5]["sub"], "<trade.export> · Пачка 10", "the dock's second line adds the pack");
    {
        // Buy and sell are columns of their own; a caption's tap orders the rows by it (largest, smallest, the game's order).
        const Json &cols = surface.props["list"]["cols"];
        Expect(cols.size() == 4 && cols[2]["label"] == "<trade.buy>" && cols[3]["label"] == "<trade.sell>" && cols[2]["sortable"] == true,
               "store: hold, store, buy, sell columns");
        const std::string first = surface.props["list"]["rows"][0]["id"];
        router.OnEvent({"goods", "sort", "2"});
        const Json sorted = surface.props["list"]["rows"];
        bool descending = true;
        for (size_t i = 1; i < sorted.size(); ++i)
            descending = descending && std::atoi(sorted[i - 1]["cells"][2]["text"].get<std::string>().c_str()) >=
                                           std::atoi(sorted[i]["cells"][2]["text"].get<std::string>().c_str());
        Expect(descending && surface.props["list"]["cols"][2]["sort"] == "desc", "sort by the buy price, largest first");
        router.OnEvent({"goods", "sort", "2"});
        Expect(surface.props["list"]["cols"][2]["sort"] == "asc", "second tap: smallest first");
        router.OnEvent({"goods", "sort", "2"});
        Expect(surface.props["list"]["cols"][2]["sort"] == "" && surface.props["list"]["rows"][0]["id"] == first, "third tap: the game's order again");
    }

    // The other money trades use the same local arithmetic with their own script's formula.
    {
        // itemstrade.c: pieces, makeint(price * qty + 0.5), fWeight * qty with one decimal; a doubloon merchant's
        // balance is named. «Лечебное зелье»: 3 ours, 12 theirs, 150 to buy, 75 to sell, 0.5 a piece; 34 / 120 carried.
        InterfaceModel doubloons = Load(models, "itemstrade-deal.json");
        for (Widget &widget : doubloons.widgets)
            if (widget.node == "OUR_GOLD")
                widget.context["currency"] = "doubloons";
        router.SetInterface(doubloons);
        ExpectEqual(surface.props["subject"]["label"], "Шарль де Мор", "items trade: the character selector is the subject");
        Expect(surface.props["deal"]["value"] == 3 && surface.props["deal"]["max"] == 13 && surface.props["primary"].is_null(),
               "items trade opens at the pivot too");
        surface.commands.clear();
        router.OnEvent({"goods", "change", "8"});
        Expect(surface.commands.empty() && surface.props["primary"]["label"] == "<trade.buy> 5" && surface.props["primary"]["sub"] == "750 \xC2\xB7 2.5" &&
                   surface.props["stats"][1]["label"] == "<trade.doubloons>" && !surface.props["stats"][1].contains("icon") &&
                   surface.props["stats"][1]["preview"] == "750",
               "five pieces: 750 and 2.5 of weight as itemstrade.c says («Деньги 750, вес 2.5»), the named balance 1 500 → 750: " +
                   surface.props["primary"].dump() + surface.props["stats"].dump());
        router.OnEvent({"goods", "change", "15"});
        Expect(surface.props["deal"]["value"] == 13 && surface.props["primary"]["label"] == "<trade.buy> 10",
               "the purse stops it at ten pieces (ten cost 1 500)");
        router.OnEvent({"goods", "change", "1"});
        router.OnEvent({"primary", "click", ""});
        router.Tick();
        Expect(surface.commands.size() == 3 && surface.commands[1] == "widget:itemstrade\tQTY_EDIT\t-1\tguard@7:row:0|-2" &&
                   surface.commands[2].find("QTY_OK_BUTTON") != std::string::npos,
               "«Продать 2»: the signed amount and the game's OK: " + Json(surface.commands).dump());
    }
    // cannons.c: the goods formula with pieces of one (290 each, 6 cwt each).
    router.SetInterface(Load(models, "cannons-deal.json"));
    ExpectEqual(surface.props["list"]["rows"][1]["sub"], "Дист. 450 · Урон x1.5 · Время 11", "cannons: range, damage, reload in the dock's second line");
    router.OnEvent({"goods", "change", "6"});
    Expect(surface.props["primary"]["label"] == "<trade.buy> 5" && surface.props["primary"]["sub"] == "1 450 \xC2\xB7 30 <transfer.cwt>",
           "cannons: a purse of 1 500 buys five at 290: " + surface.props["primary"].dump());
    {
        InterfaceModel rich = Load(models, "cannons-deal.json");
        for (Widget &widget : rich.widgets)
            if (widget.node == "OUR_GOLD")
                widget.label = "Наши деньги 5 000 песо";
        FakeSurface rich_surface;
        Router rich_router(rich_surface);
        rich_router.SetInterface(rich);
        rich_router.OnEvent({"goods", "change", "6"});
        Expect(rich_surface.props["primary"]["label"] == "<trade.buy> 6" && rich_surface.props["primary"]["sub"] == "1 740 \xC2\xB7 36 <transfer.cwt>",
               "six cannons: 1 740 and 36 cwt as cannons.c says («Деньги 1740, вес 36 ц»): " + rich_surface.props["primary"].dump());
    }
    // contraband.c: a good without a buy price can only be sold: the pivot is the scale's right end.
    router.SetInterface(Load(models, "contraband-deal.json"));
    Expect(surface.props["deal"]["max"] == 50 && surface.props["deal"]["min"] == 0 &&
               surface.props["deal"]["origin"] == 50 && surface.props["deal"]["value"] == 50,
           "contraband: only a sale, the pivot at the right end, the knob on it");
    router.OnEvent({"goods", "change", "0"});
    Expect(surface.props["primary"]["label"] == "<trade.sell> 50" && surface.props["primary"]["sub"] == "230 \xC2\xB7 5 <transfer.cwt>",
           "contraband: all 50 for 230 (46 a pack of 10): " + surface.props["primary"].dump());
}

static void Crew(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "hirecrew.json"));
    Expect(router.Using() == "hirecrew" && surface.tag == "s-crew" && surface.errors.empty(), "hirecrew is drawn by the crew screen");
    Json row = surface.props["list"]["rows"][0];
    Expect(row["q"]["value"] == 40 && row["q"]["max"] == 61 && row["cells"][2]["text"] == "23", "crew on the ship, up to what the purse hires of the tavern, price per man");
    Expect(surface.props["primary"].is_null(), "nothing to confirm yet: no deal button");
    Expect(surface.props["ship"].size() == 4 && surface.props["ship"][0]["v"] == "Хорошая" && surface.props["tavern"][1]["v"] == "неопытные",
           "morale and experience of both crews");
    // hirecrew.json: 40 on the ship, 25 in the tavern, 23 a man, a purse of 500; the ship takes 94, its hold has room.
    // The amount is the player's: a drag sends nothing, the cost is hirecrew.c's makeint(price * men), the purse
    // stops the hire at makeint(500 / 23) = 21 men.
    const auto crew_q = [&surface]() { return surface.props["list"]["rows"][0]["q"]; };
    Expect(crew_q()["value"] == 40 && crew_q()["origin"] == 40 && crew_q()["min"] == 0 && crew_q()["max"] == 61 &&
               crew_q()["moreReason"] == "<trade.no_money>" && surface.errors.empty(),
           "the hire opens on the pivot; the rail ends where the purse does and knows why: " + crew_q().dump());
    surface.commands.clear();
    {
        bool steady = true, exact = true;
        for (int sample = 1; sample <= 200; ++sample)
        {
            router.OnEvent({"crew", "change", std::to_string(40 + sample % 26)});
            const int men = std::min(sample % 26, 21);
            const Json q = crew_q();
            steady = steady && q["origin"] == 40 && q["max"] == 61 && q["value"] == 40 + men;
            if (men > 0)
                exact = exact && surface.props["primary"]["label"] == "<crew.hire> " + std::to_string(men) &&
                        surface.props["primary"]["sub"] == std::to_string(23 * men) && surface.props["primary"]["busy"] == false &&
                        surface.props["stats"][1]["preview"] == std::to_string(500 - 23 * men);
            else
                exact = exact && surface.props["primary"].is_null();
        }
        Expect(surface.commands.empty(), "200 samples of a crew drag send nothing to the game: " + std::to_string(surface.commands.size()));
        Expect(steady, "the pivot and the end stay put; the purse stops the hire at 21");
        Expect(exact, "every sample shows the game's cost (price * men) on a ready button and the purse after it");
        Expect(!surface.toasts.empty() && surface.toasts.back() == "<trade.no_money>", "a push past the end says why");
    }
    router.OnEvent({"crew", "commit", "50"});
    Expect(surface.props["primary"]["label"] == "<crew.hire> 10" && surface.props["primary"]["sub"] == "230" &&
               surface.props["stats"][0]["value"] == "40" && surface.props["stats"][0]["preview"] == "50",
           "ten men for 230, the crew 40 → 50 in the header");
    surface.commands.clear();
    router.OnEvent({"primary", "click", ""});
    router.Tick();
    Expect(surface.commands.size() == 3 && surface.commands[0] == "widget:hirecrew\tQTY_EDIT\t-1\tguard@7:ship:0|0" &&
               surface.commands[1] == "widget:hirecrew\tQTY_EDIT\t-1\tguard@7:ship:0|10" &&
               surface.commands[2] == "widget:hirecrew\tQTY_OK_BUTTON\t-1\tguard@7:ship:0|press",
           "«Нанять 10»: the amount and the game's OK, bound to the ship: " + Json(surface.commands).dump());
    router.OnEvent({"crew", "commit", "35"});
    Expect(surface.props["primary"]["label"] == "<crew.fire> 5" && surface.props["primary"]["sub"] == "", "left of the pivot fires, at no cost");
    surface.commands.clear();
    router.OnEvent({"primary", "click", ""});
    router.Tick();
    Expect(surface.commands.size() == 3 && surface.commands[1] == "widget:hirecrew\tQTY_EDIT\t-1\tguard@7:ship:0|-5", "«Уволить 5»: a negative amount");
    {
        // The ship's own limits come from the adapter: a full crew, or no room in the hold (hirecrew.c QtyMax).
        const auto with = [&](const char *key, const char *value) {
            InterfaceModel model = Load(models, "hirecrew.json");
            for (Widget &widget : model.widgets)
                if (widget.node == "ANDROID_CREWHIRE")
                    widget.context[key] = value;
            FakeSurface limited_surface;
            Router limited(limited_surface);
            limited.SetInterface(model);
            return limited_surface.props["list"]["rows"][0]["q"];
        };
        Expect(with("max", "45")["max"] == 45 && with("max", "45")["moreReason"] == "<crew.full>", "the ship takes five more: the rail ends at 45");
        Expect(with("space", "0")["max"] == 40 && with("space", "0")["moreReason"] == "<trade.no_room>", "no room in the hold: nobody is hired");
        // Without the adapter's facts the screen says so aloud (the game may then hire fewer than shown).
        InterfaceModel bare = Load(models, "hirecrew.json");
        bare.widgets.erase(std::remove_if(bare.widgets.begin(), bare.widgets.end(), [](const Widget &w) { return w.node == "ANDROID_CREWHIRE"; }), bare.widgets.end());
        FakeSurface bare_surface;
        Router bare_router(bare_surface);
        bare_router.SetInterface(bare);
        Expect(bare_surface.errors.size() == 1 && bare_surface.errors[0].find("ANDROID_CREWHIRE") != std::string::npos, "missing adapter facts are an error, not a quiet guess");
    }
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
    Expect(surface.props["tabs"].size() == 3 && surface.props["tabs"][0]["selected"] == true && surface.props["tabs"][2]["id"] == "ships" &&
               surface.props["ships"].is_null(),
           "cargo, crew and ships tabs, cargo open");
    {
        // The other ship's card of the dock, and the «Корабли» tab: both ships' figures side by side.
        const Json &prize = surface.props["prize"];
        Expect(prize["title"] == "Корабль 2" && prize["facts"].size() == 3 && prize["facts"][0]["k"] == "Корпус", "the other ship's card: " + prize.dump());
        surface.commands.clear();
        router.OnEvent({"header", "tab", "2"});
        const Json &ships = surface.props["ships"];
        Expect(surface.commands.empty() && surface.props["tabs"][2]["selected"] == true && ships["ours"]["title"] == "Корабль 1" &&
                   ships["rows"].size() >= 8 && ships["rows"][0]["k"] == "Корпус" && ships["rows"][0]["icon"] == "hull",
               "the ships tab is ours: nothing is sent, the figures are listed row by row: " + ships["rows"][0].dump());
        bool toned = false;
        for (const Json &row : ships["rows"])
            toned = toned || row.contains("tone");
        Expect(toned, "a figure where the other ship is plainly better or worse is toned");
        router.OnEvent({"header", "tab", "0"});
        Expect(surface.props["ships"].is_null() && surface.props["tabs"][0]["selected"] == true, "back to the cargo");
    }
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
    {
        const auto arrows = [&prefix](const std::string &command) {
            return command == prefix + "B_LEFT\t-1\tpress" || command == prefix + "B_RIGHT\t-1\tpress";
        };
        const auto presses = [&prefix](const std::string &node, size_t count) {
            return [&prefix, node, count](const std::vector<std::string> &commands) {
                return commands.size() == count && std::all_of(commands.begin(), commands.end(), [&](const std::string &c) { return c == prefix + node + "\t-1\tpress"; });
            };
        };
        DraftFlow("transfer cargo: take", router, surface, "goods", 145, "<trade.take> 45", arrows, presses("B_LEFT", 3), [] {});
        Expect(surface.props["list"]["rows"][0]["q"]["value"] == 145, "the control shows what was sent until the game answers");
        InterfaceModel answered = Load(models, "transfer-loot-row.json");
        answered.revision += 5;
        router.SetInterface(answered); // a revision with the same figures: not the answer yet
        Expect(surface.props["list"]["rows"][0]["q"]["value"] == 145 && surface.props["primary"]["busy"] == true,
               "a revision that does not change the figures is not the answer: the sent amount waits");
        surface.now += 2.0;
        router.Tick(); // (the game refused: no change within the answer time)
        Expect(surface.props["list"]["rows"][0]["q"]["value"] == 100 && surface.props["primary"]["label"] == "<transfer.take_all>",
               "a refused move opens the amount again at the game's figure; the bar is the place's again");
        DraftFlow("transfer cargo: give", router, surface, "goods", 0, "<trade.give> 100", arrows, presses("B_RIGHT", 5), [] {});
        surface.now += 2.0;
        answered.revision += 5;
        router.SetInterface(answered);
        surface.commands.clear();
        router.OnEvent({"goods", "change", "300"});
        router.OnEvent({"header-back", "click", ""});
        Expect(surface.commands.empty() && surface.props["primary"]["label"] == "<transfer.take_all>", "back drops the amount set first, moving nothing");
    }
    {
        // The hand at a row's end: the row is selected, then all of the other hold is asked for by the game's arrow.
        FakeSurface quick_surface;
        Router quick(quick_surface);
        quick.SetInterface(Load(models, "transfer-loot.json"));
        Expect(quick_surface.props["list"]["quick"] == "hand" && quick_surface.props["list"]["rows"][0]["quick"] == true, "cargo: rows the other hold has offer the quick take");
        quick.OnEvent({"goods", "quick", "0"});
        ExpectEqual(quick_surface.Last(), prefix + "TABLE_LIST\t0\tselect", "quick take: the row is selected first");
        quick_surface.commands.clear();
        quick.SetInterface(Load(models, "transfer-loot-row.json"));
        quick.Tick();
        Expect(quick_surface.commands.size() == 25 && quick_surface.commands[0] == prefix + "B_LEFT\t-1\tpress" && quick_surface.props["dealing"] == true,
               "quick take: 495 of theirs = 25 packs of 20 taken: " + std::to_string(quick_surface.commands.size()));
    }

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

    {
        // The surrendered captain: his portrait beside the question, overboard held, prison the plain answer.
        InterfaceModel capture = Load(models, "transfer-loot.json");
        capture.widgets.push_back(Make("CAPTURE_CAPTION", "text", "Решение по пленным"));
        capture.widgets.push_back(Make("CAPTURE_TEXT", "text", "Выберите участь сдавшегося капитана."));
        capture.widgets.push_back(Make("CAPTURE_CAPTAIN_KILL", "button", "Выкинуть за борт", {"press"}));
        capture.widgets.push_back(Make("CAPTURE_CAPTAIN_PRISON", "button", "Взять пленником", {"press"}));
        capture.widgets.push_back(Make("CAPTURE_CANCEL", "button", "Отмена", {"press"}));
        Widget face = Make("CAPTUR_PICTURE", "image", "");
        ImageRef portrait;
        portrait.kind = "portrait";
        portrait.image = "100";
        face.images.push_back(portrait);
        capture.widgets.push_back(face);
        FakeSurface capture_surface;
        Router capture_router(capture_surface);
        capture_router.SetInterface(capture);
        const Json &asked = capture_surface.props["modal"];
        Expect(capture_surface.errors.empty() && capture_surface.props["extras"]["rows"].empty() &&
                   asked["image"] == Json({{"kind", "portrait"}, {"texture", "INTERFACES/PORTRAITS/128/face_100"}}) &&
                   asked["actions"][0]["hold"] == true && asked["actions"][0]["variant"] == "danger" && asked["actions"][1]["variant"] == "primary",
               "the captain's fate: his portrait, overboard held: " + asked.dump());
        capture_surface.commands.clear();
        capture_router.OnEvent({"modal", "dismiss", ""});
        Expect(capture_surface.commands.empty() && capture_surface.errors.empty(), "a tap beside the question answers nothing and is no error");
        capture_router.OnEvent({"capture-prison", "click", ""});
        ExpectEqual(capture_surface.Last(), prefix + "CAPTURE_CAPTAIN_PRISON\t-1\tpress", "prison is the game's own button");
    }
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
    DraftFlow("transfer crew", router, surface, "crew", 100, "<trade.take> 4",
              [&prefix](const std::string &command) { return command == prefix + "CREW_LEFT\t-1\tpress" || command == prefix + "CREW_RIGHT\t-1\tpress"; },
              [&prefix](const std::vector<std::string> &commands) {
                  return commands == std::vector<std::string>(4, prefix + "CREW_LEFT\t-1\tpress");
              },
              [] {});
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
    Expect(surface.props["offers"].size() == 3 && surface.props["offers"][2]["value"] == "23 824" &&
               surface.props["offers"][0]["title"] == "Шлюпка «Чайка»" && surface.props["offers"][0]["meta"] == "<yard.class> 6",
           "the yard's offers: type «name», class, grouped price: " + surface.props["offers"][0].dump());
    Expect(surface.props["fleet"].size() == 2 && surface.props["fleet"][0]["title"] == "Кеч «Марта»" && surface.props["fleet"][0]["meta"] == "Класс 5" &&
               surface.props["fleet"][0]["value"] == "11\u00a0575" && surface.props["fleet"][0]["selected"] == true &&
               surface.props["fleet"][1]["title"] == "<yard.empty_slot>" && surface.props["subject"].is_null(),
           "our ships in the list: each slot with a ship and its trade-in value, one free place of the four");
    {
        const Json &detail = surface.props["detail"];
        Expect(detail["title"] == "Кеч «Марта»" && detail["actions"].size() == 3 && detail["actions"][0]["id"] == "repair" &&
                   detail["actions"][1]["sub"] == "6 фунт / 12" && detail["actions"][2]["id"] == "sell" && detail["actions"][2]["hold"] == true &&
                   detail["actions"][2]["sub"] == "11\u00a0575" && surface.props["sheet"].is_null(),
               "our ship on the card: repair, cannons, sell held with its price; no buy button for it");
        Expect(detail["facts"].size() == 8 && detail["facts"][0] == Json({{"k", "Корпус"}, {"v", "610 / 610"}, {"icon", "hull"}}) && detail["against"] == "",
               "the figures with their icons, nothing to compare our ship with");
    }

    router.OnEvent({"offer-1", "click", ""});
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
    router.OnEvent({"fleet-1", "click", ""});
    ExpectEqual(surface.Last(), Yard(main, "SHIPS_SCROLL", 1, "choice", "select"), "the free place is a fleet slot of the list");
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
    Expect(surface.commands.empty() && surface.props["detail"]["actions"][2]["disabled"] == true, "a state that is not ready offers nothing");
    Expect(surface.errors.empty(), "a state settling for a moment is not an error");
    surface.now += 2.5;
    router.Tick();
    Expect(surface.errors.size() == 1, "a state that stays unusable is reported");

    const InterfaceModel preview = Load(models, "shipyard-preview.json");
    router.SetInterface(preview);
    {
        // shipyard.c: a ship in the chosen slot is traded in; 2 170 for a ship worth 11 575 returns 9 405.
        const Json &detail = surface.props["detail"];
        Expect(detail["title"] == "Шлюпка «Чайка»" && detail["actions"].size() == 1 && detail["actions"][0]["label"] == "<yard.trade_get>" &&
                   detail["actions"][0]["sub"] == "9 405" && detail["actions"][0]["hold"] == true && surface.props["offers"][0]["selected"] == true &&
                   surface.props["fleet"][0]["selected"] == true && detail["against"] == "Марта",
               "an offer on the card: traded for our chosen ship, the difference on the held button: " + detail["actions"].dump());
        Expect(detail["facts"][0]["own"] == "610 / 610" && detail["facts"][0]["tone"] == "danger" && detail["facts"][3]["tone"] == "ok" &&
                   !detail["facts"][1].contains("tone"),
               "its figures beside our ship's: a weaker hull red, a better turn rate green, sails plain: " + detail["facts"].dump());
        // Into the free place it is a plain purchase.
        InterfaceModel free_place = preview;
        for (Widget &widget : free_place.widgets)
            if (widget.node == "SHIPS_SCROLL" && widget.type == "choice")
                widget.value = widget.index == 1 ? "1" : "0";
        FakeSurface free_surface;
        Router free_router(free_surface);
        free_router.SetInterface(free_place);
        Expect(free_surface.props["detail"]["actions"][0]["label"] == "Купить" && free_surface.props["detail"]["actions"][0]["sub"] == "2 170" &&
                   !free_surface.props["detail"]["facts"][0].contains("own"),
               "the free place chosen: buy with the full price, nothing to compare with");
    }
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), Yard(preview, "BUTTON_BUY", -1, "button", "press"), "buy presses the game's button");
    InterfaceModel looking = preview;
    looking.revision += 1;
    router.SetInterface(looking);
    router.OnEvent({"fleet-0", "click", ""});
    ExpectEqual(surface.Last(), Yard(looking, "SHIPS_SCROLL", 0, "choice", "select"), "our ship's row brings it back to the card");
    {
        // A deal held on its button is not asked about again: the game's question is answered at once.
        FakeSurface held_surface;
        Router held(held_surface);
        held.SetInterface(Load(models, "shipyard-main.json"));
        held.OnEvent({"sell", "click", ""});
        const InterfaceModel asked = Load(models, "shipyard-confirm-sell.json");
        held.SetInterface(asked);
        ExpectEqual(held_surface.Last(), Yard(asked, "MSG_OK", -1, "button", "press"), "held sell: the game's confirmation is answered");
        Expect(held_surface.props["sheet"].is_null(), "and no sheet asks a second time");
    }

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

// Caught stealing (TEHO itemsbox.c, LAi_owner.c): the chest's interface does not pause the game
// and the owner who saw the hero in it starts his dialogue the moment it closes. The game's
// close of the chest reaches the UI after a grace (engine_ui.cpp), when the dialogue panel may
// already be up: the close must not take it, or the game waits in a dialogue nobody sees.
static void DialogueOverClosingInterface(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "itemsbox.json"));
    ExpectEqual(router.Model().screen, "itemsbox", "the chest is open");
    router.SetState("dialogue", LoadJson(models, "dialogue.json"));
    ExpectEqual(surface.tag, "s-dialogue", "the owner's dialogue comes while the chest is still closing");
    router.CloseGameInterface("itemsbox");
    Expect(surface.tag == "s-dialogue" && router.Model().screen == "dialogue", "the chest's late close leaves the dialogue up");
    Expect(!surface.logs.empty() && surface.logs.back().find("interface itemsbox closed under the dialogue screen") != std::string::npos,
           "and says so in the log");
    router.OnEvent({"reply-0", "click", ""});
    ExpectEqual(surface.Last(), "dialogue:4:0", "its reply still answers the dialogue");

    // A game interface the dialogue's script opens over it: when it closes, the dialogue the
    // engine still holds comes back (the engine sends a node once).
    router.SetInterface(Load(models, "itemsbox.json"));
    Expect(surface.tag != "s-dialogue", "an interface opened from the dialogue covers it");
    router.CloseGameInterface("itemsbox");
    Expect(surface.tag == "s-dialogue" && router.Model().opening == 4, "its close brings the same dialogue back");

    // A dialogue that ended under the interface does not come back.
    router.SetInterface(Load(models, "itemsbox.json"));
    router.SetState("dialogue", Json{{"active", false}});
    ExpectEqual(router.Model().screen, "itemsbox", "the end of a covered dialogue leaves the interface");
    router.CloseGameInterface("itemsbox");
    Expect(surface.tag.empty() && !router.Active(), "and nothing is left when the interface closes");
    Expect(surface.errors.empty(), "no errors on the way");
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

static Widget *Row(InterfaceModel &model, const char *node, int index)
{
    for (Widget &widget : model.widgets)
        if (widget.node == node && widget.index == index)
            return &widget;
    return nullptr;
}

static void Hero(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel model = Load(models, "hero.json");
    router.SetInterface(model);
    ExpectEqual(router.Using(), "lscchar_all", "the character draws on the hero screen");
    ExpectEqual(surface.tag, "s-hero", "s-hero mounted");
    Json view = surface.props;
    Expect(view["sections"] == "hero" && view["tabs"].size() == 4 && view["tabs"][0]["badge"] == 2 && view["tabs"][1]["id"] == "perks1" &&
               view["tabs"][1]["badge"] == 1 && view["tabs"][2]["id"] == "perks2" && view["tabs"][3]["id"] == "officers",
           "skills, the two perk groups and officers; free skill and perk points as badges: " + view["tabs"].dump());
    Expect(view["card"]["title"] == "Anne Bonny" && view["card"]["portrait"] == "INTERFACES/PORTRAITS/128/face_7" &&
               view["card"]["health"]["value"] == 75 && view["card"]["health"]["text"] == "90" && view["card"]["free"] == "2" &&
               view["card"]["lines"][0]["v"] == "excellent" && view["card"]["lines"][1]["v"] == "63 / 100",
           "the identity rail from TABLE_OTHER and the adapter");
    Expect(view["special"]["rows"].size() == 2 && view["special"]["rows"][1]["cells"][1]["tone"] == "ok", "the read-only PIRATES column");
    Expect(view["points"] == true && view["personal"]["rows"][0]["cells"][1]["arrows"] == true &&
               view["personal"]["rows"][0]["cells"][1]["noLeft"] == true,
           "free points: ‹ › on the skills, ‹ only for points added in this visit");
    Expect(view["subject"]["sub"] == "1 <common.of> 2" && view["subject"]["prev"] == false && view["subject"]["next"] == true,
           "the captain and one officer in the switch");
    // The subject list: a tap on the name lists the captain and the officers; a picked row is the same select as ›.
    Expect(view["subject"]["list"] == true && !view["subject"].contains("open"), "two subjects offer the list, closed");
    router.OnEvent({"header", "subject-list", ""});
    view = surface.props;
    Expect(view["subject"]["open"] == true && view["subject"]["title"] == "<hero.subjects>" && view["subject"]["rows"].size() == 2 &&
               view["subject"]["rows"][0]["label"] == "Anne Bonny" && view["subject"]["rows"][0]["selected"] == true &&
               view["subject"]["rows"][1]["selected"] == false,
           "the list of the captain and the officer on a post, the current one selected");
    Expect(router.WantsBack(), "back closes the subject list");
    router.Back();
    Expect(!surface.props["subject"].contains("open"), "back closed the list");
    router.OnEvent({"header", "subject-list", ""});
    const size_t sent = surface.commands.size();
    router.OnEvent({"header-subject-0", "", ""});
    Expect(surface.commands.size() == sent && !surface.props["subject"].contains("open"), "picking the current subject only closes the list");
    router.OnEvent({"header", "subject-list", ""});
    router.OnEvent({"header-subject-1", "", ""});
    ExpectEqual(surface.Last(), "widget:lscchar_all\tCHARACTERS_SCROLL\t1\tselect@4:1:1", "a picked officer is selected as with ›");
    Expect(!surface.props["subject"].contains("open"), "the pick closes the list");
    Expect(view["extras"]["rows"].empty(), "every widget is known: no extras");

    // A skill arrow: select the row, then the game's arrow once the engine shows the row selected.
    router.OnEvent({"personal", "inc", "0"});
    ExpectEqual(surface.Last(), "widget:lscchar_all\tTABLE_SKILL_1\t0\tselect@4:Leadership", "› selects the skill row first");
    model.revision += 1;
    Row(model, "TABLE_SKILL_1", 0)->value = "1";
    router.SetInterface(model);
    ExpectEqual(surface.Last(), "widget:lscchar_all\tB_SKILLUP\t-1\tpress", "then presses the game's arrow");
    Expect(surface.props["personal"]["rows"][0]["cells"][1]["noLeft"] == false, "a point added in this visit may be taken back");

    // Learning a perk: select, open its window, accept.
    surface.commands.clear();
    router.OnEvent({"header-tabs", "select", "perks1"});
    view = surface.props;
    Expect(surface.commands.empty() && view["tab"] == "perks" && view["tabs"][1]["selected"] == true && view["perks"].size() == 2 &&
               view["detail"]["title"] == "Basic defense" && view["detail"]["learn"]["disabled"] == false &&
               view["detail"]["learn"]["sub"] == "<hero.points> 1" && view["progress"] == "<hero.next_point> 340 / 1\xC2\xA0" "000",
           "the game's open perk group: its tiles, the detail and the progress to the next point");
    Expect(view["perks"][0]["state"] == "open" && view["perks"][1]["state"] == "locked" && view["perks"][1]["image"]["enamel"] == "iron",
           "a perk with a missing requirement is locked");
    Expect(view["filters"][0]["count"] == 2 && view["filters"][1]["count"] == 1 && view["filters"][2]["count"] == 0, "the chips count all, open, learned");
    router.OnEvent({"perk-filter", "select", "1"});
    Expect(surface.props["perks"].size() == 1 && surface.props["perks"][0]["name"] == "Basic defense", "«open» keeps what can be learned now");
    router.OnEvent({"perk-filter", "select", "0"});
    router.OnEvent({"perks", "row", "1"});
    Expect(surface.props["detail"]["learn"]["disabled"] == true &&
               surface.props["detail"]["conditions"] == Json::array({Json{{"name", "Basic defense"}, {"met", false}}}),
           "the locked perk's detail names what it needs");
    router.OnEvent({"perk-learn", "click", ""});
    Expect(surface.props["dialog"].is_null(), "a perk that cannot be learned opens no card");
    router.OnEvent({"perks", "row", "0"});
    router.OnEvent({"perk-learn", "click", ""});
    {
        const Json &card = surface.props["dialog"];
        Expect(surface.commands.empty() && card["kind"] == "learn" && card["title"] == "Basic defense" && card["text"] == "Damage taken is 10% lower." &&
                   card["actions"][1]["id"] == "learn-ok" && card["actions"][1]["sub"] == "<hero.points> 1" && router.WantsBack(),
               "learning asks on a card: the perk's text and its cost; nothing is sent yet: " + card.dump());
    }
    router.OnEvent({"learn-cancel", "click", ""});
    Expect(surface.commands.empty() && surface.props["dialog"].is_null(), "«Отмена» closes the card and learns nothing");
    router.OnEvent({"perk-learn", "click", ""});
    router.OnEvent({"learn-ok", "click", ""});
    ExpectEqual(surface.Last(), "widget:lscchar_all\tTABLE_PERKS\t0\tactivate@4:basicdefense", "confirmed: the selected perk's window is opened");
    Expect(surface.props["dialog"].is_null(), "the card is gone while the game learns");
    model.revision += 1;
    model.widgets.push_back(Make("PERK_ACCEPT", "button", "Learn", {"press"}));
    model.widgets.push_back(Make("PERK_CANCEL", "button", "Cancel", {"press"}));
    router.SetInterface(model);
    ExpectEqual(surface.Last(), "widget:lscchar_all\tPERK_ACCEPT\t-1\tpress", "and accepted in the game's window");
    Expect(router.WantsBack(), "while the game's window is open the back key closes it");
    model.widgets.resize(model.widgets.size() - 2);
    // The other perk group is its own tab: the game's group button is pressed, the tab shows chosen at once.
    model.revision += 1;
    router.SetInterface(model);
    router.OnEvent({"header-tabs", "select", "perks2"});
    ExpectEqual(surface.Last(), "widget:lscchar_all\tTABBTN_2\t-1\tpress", "the ship perks: the game's second group");
    Expect(surface.props["tabs"][2]["selected"] == true && surface.props["tabs"][1]["selected"] == false, "the asked group shows as chosen while the game switches");

    // Officers: a post opens the game's remove window; the dialog removes or closes.
    model.revision += 1;
    router.SetInterface(model);
    router.OnEvent({"header-tabs", "select", "officers"});
    view = surface.props;
    Expect(view["posts"].size() == 1 && view["freePosts"].size() == 1 && view["passengers"].size() == 1 && view["postsCount"] == "1 <common.of> 2",
           "posts, free posts, passengers");
    router.OnEvent({"post-1", "click", ""});
    ExpectEqual(surface.Last(), "widget:lscchar_all\tCHARACTERS_SCROLL\t1\tselect@4:1:1", "a post selects its slot");
    model.revision += 1;
    Row(model, "CHARACTERS_SCROLL", 0)->value = "0";
    Row(model, "CHARACTERS_SCROLL", 1)->value = "1";
    Row(model, "CHARACTERS_SCROLL", 1)->actions = {"select", "activate"};
    router.SetInterface(model);
    ExpectEqual(surface.Last(), "widget:lscchar_all\tCHARACTERS_SCROLL\t1\tactivate@4:1:1", "then activates it");
    model.revision += 1;
    model.widgets.push_back(Make("REMOVE_ACCEPT_OFFICER", "button", "Yes", {"press"}));
    model.widgets.push_back(Make("REMOVE_CANCEL_OFFICER", "button", "No", {"press"}));
    router.SetInterface(model);
    view = surface.props;
    Expect(view["dialog"]["kind"] == "officer" && view["dialog"]["title"] == "Jack Rackham" && view["dialog"]["facts"][0]["v"] == "34" &&
               view["dialog"]["facts"][2]["v"] == "1\xC2\xA0" "500 <hero.per_month>",
           "the officer dialog with the post's skill, loyalty and salary");
    router.OnEvent({"officer-remove", "click", ""});
    ExpectEqual(surface.Last(), "widget:lscchar_all\tREMOVE_ACCEPT_OFFICER\t-1\tpress", "«relieve» is the game's accept");
    model.revision += 1;
    router.SetInterface(model);
    router.Back();
    ExpectEqual(surface.Last(), "widget:lscchar_all\tREMOVE_CANCEL_OFFICER\t-1\tpress", "back closes the game's window");
    model.widgets.resize(model.widgets.size() - 2);

    // The post chooser: a candidate is selected, then accepted.
    model.revision += 1;
    model.widgets.push_back(Make("ACCEPT_ADD_OFFICER", "button", "Accept", {"press"}));
    model.widgets.push_back(Make("CANCEL_ADD_OFFICER", "button", "Cancel", {"press"}));
    for (int i = 0; i < 2; ++i)
    {
        Widget row = Make("PASSENGERSLIST", "choice", i ? "Mary Read" : "Ned Low", {"select"});
        row.index = i;
        row.value = i ? "0" : "1";
        row.row_id = "3:" + std::to_string(i);
        model.widgets.push_back(row);
        Widget fact = Make("ANDROID_CANDIDATES", "fact", i ? "Mary Read" : "Ned Low");
        fact.index = i;
        fact.context = {{"index", std::to_string(i)}, {"rank", "6"}, {"skill", "Grappling"}, {"skillvalue", "41"}, {"face", "30"}};
        model.widgets.push_back(fact);
    }
    router.SetInterface(model);
    Expect(surface.props["dialog"]["kind"] == "assign" && surface.props["dialog"]["rows"].size() == 2 &&
               surface.props["dialog"]["rows"][1]["value"] == "Grappling 41",
           "the post chooser lists the game's candidates with the post's skill");
    router.OnEvent({"candidate-1", "click", ""});
    ExpectEqual(surface.Last(), "widget:lscchar_all\tPASSENGERSLIST\t1\tselect@4:3:1", "a candidate is selected in the game's list");
    model.revision += 1;
    Row(model, "PASSENGERSLIST", 0)->value = "0";
    Row(model, "PASSENGERSLIST", 1)->value = "1";
    router.SetInterface(model);
    ExpectEqual(surface.Last(), "widget:lscchar_all\tACCEPT_ADD_OFFICER\t-1\tpress", "and accepted");

    // Sections wait for the game's window to close.
    model.revision += 1;
    router.SetInterface(model);
    router.OnEvent({"header", "section", "ship"});
    ExpectEqual(surface.Last(), "widget:lscchar_all\tCANCEL_ADD_OFFICER\t-1\tpress", "a section first closes the open window");
    model.widgets.erase(std::remove_if(model.widgets.begin(), model.widgets.end(), [](const Widget &w) {
                            return w.node == "ACCEPT_ADD_OFFICER" || w.node == "CANCEL_ADD_OFFICER" || w.node == "PASSENGERSLIST" ||
                                   w.node == "ANDROID_CANDIDATES";
                        }),
                        model.widgets.end());
    model.revision += 1;
    router.SetInterface(model);
    ExpectEqual(surface.Last(), "widget:lscchar_all\tI_SHIP\t-1\tpress", "then opens the section");

    // Without the adapter's facts the screen is not used, loudly.
    InterfaceModel bare = Load(models, "hero.json");
    bare.opening = 50;
    bare.widgets.erase(std::remove_if(bare.widgets.begin(), bare.widgets.end(), [](const Widget &w) { return w.type == "fact"; }),
                       bare.widgets.end());
    const size_t errors = surface.errors.size();
    router.SetInterface(bare);
    Expect(router.Using() == "lscchar_all" && surface.errors.size() == errors + 1, "without the interface adapter's facts: drawn, and reported");
    bare.revision += 1;
    router.SetInterface(bare);
    Expect(surface.errors.size() == errors + 1, "reported once per opening (a closing interface loses its facts)");
}

static void Items(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel model = Load(models, "items.json");
    router.SetInterface(model);
    ExpectEqual(router.Using(), "items", "the items draw on the items screen");
    ExpectEqual(surface.tag, "s-items", "s-items mounted");
    Json view = surface.props;
    Expect(view["slots"].size() == 10 && view["slots"][0]["label"] == "<items.slot_blade>" && view["slots"][0]["name"] == "Rapier" &&
               view["slots"][0]["image"]["texture"] == "atlas:ITEMS_1/itm1" && view["slots"][1]["empty"] == true,
           "ten slots: the slot's caption under what the hero wears, the empty ones dashed");
    Expect(view["rows"].size() == 3 && !view.contains("columns") && view["rows"][0]["cells"][0]["badge"] == "check" && view["rows"][2]["cells"][1] == "\xC3\x97" "3",
           "the list: worn mark, a count only above one");
    Expect(view["stats"][0]["value"] == "4.6 / 140" && view["subject"]["label"] == "Anne Bonny" && view["tabs"].size() == 4,
           "weight in the header, the officer switch, the game's tabs");
    Expect(view["detail"]["title"] == "Rapier" && view["detail"]["text"] == "A light blade." && view["detail"]["action"]["label"] == "Take off" &&
               view["detail"]["action"]["disabled"] == false,
           "the selected item with the game's own action");
    {
        // The description in parts: the worn rapier's own figures (nothing to compare with), its kind, then the text.
        const Json &facts = view["detail"]["facts"];
        Expect(facts.size() == 4 && facts[0] == Json({{"k", "<items.attack>"}, {"v", "60.0"}}) && facts[2]["k"] == "<items.weight>" &&
                   facts[3] == Json({{"k", "<items.kind>"}, {"v", "Rapiers"}}),
               "the worn weapon: attack, balance, weight, kind: " + facts.dump());
        // The dagger against the worn rapier: weaker (red), lighter (green); the balance has no better side.
        FakeSurface dagger_surface;
        Router dagger(dagger_surface);
        InterfaceModel selected = model;
        for (Widget &widget : selected.widgets)
        {
            if (widget.node == "TABLE_ITEMS")
                widget.value = widget.index == 1 ? "1" : "0";
            if (widget.node == "ANDROID_INFO")
                widget.context = {{"row", "tr2"}, {"text", "A short blade."}, {"kind", "Rapiers"}, {"weight", "1.5"}};
        }
        dagger.SetInterface(selected);
        const Json &against = dagger_surface.props["detail"]["facts"];
        Expect(against[0] == Json({{"k", "<items.attack>"}, {"v", "34.5"}, {"d", "\xE2\x88\x92" "25.5"}, {"dTone", "danger"}}) &&
                   against[1] == Json({{"k", "<items.balance>"}, {"v", "2.0"}, {"d", "+0.6"}}) &&
                   against[2] == Json({{"k", "<items.weight>"}, {"v", "1.5"}, {"d", "\xE2\x88\x92" "0.9"}, {"dTone", "ok"}}) &&
                   dagger_surface.props["detail"]["text"] == "A short blade.",
               "an item against the worn one of its slot: " + against.dump());
        // A row the adapter did not describe keeps the game's own text.
        for (Widget &widget : selected.widgets)
            if (widget.node == "ANDROID_INFO")
                widget.context["row"] = "tr9";
        selected.revision += 1;
        dagger.SetInterface(selected);
        Expect(dagger_surface.props["detail"]["text"] == "A light blade.", "no description from the adapter: the game's INFO_TEXT");
    }
    Expect(view["extras"]["rows"].empty(), "every widget is known: no extras");
    {
        // An item worn nowhere (the potion: no slot) has no slot line and is no error.
        FakeSurface potion_surface;
        Router potion(potion_surface);
        InterfaceModel selected = model;
        for (Widget &widget : selected.widgets)
            if (widget.node == "TABLE_ITEMS")
                widget.value = widget.index == 2 ? "1" : "0";
        potion.SetInterface(selected);
        Expect(potion_surface.props["detail"]["title"] == "Potion" && potion_surface.props["detail"]["sub"] == "" && potion_surface.errors.empty(),
               "an item without a slot: no slot line, no missing-string error");
    }

    router.OnEvent({"item-equip", "click", ""});
    ExpectEqual(surface.Last(), "widget:items\tEQUIP_BUTTON\t-1\tpress@5:inventory:7:character:1:role:0", "the action is the game's guarded press");
    router.OnEvent({"items", "row", "2"});
    ExpectEqual(surface.Last(), "widget:items\tTABLE_ITEMS\t2\tselect@5:inventory:30:character:1:role:0", "a row is selected");

    // A slot filters the list; a row of the filtered list is put on.
    router.OnEvent({"slot-blade", "click", ""});
    Expect(surface.props["rows"].size() == 2 && surface.props["slots"][0]["selected"] == true, "the blade slot lists the blades");
    router.OnEvent({"items", "row", "1"});
    ExpectEqual(surface.Last(), "widget:items\tTABLE_ITEMS\t1\tselect@5:inventory:9:character:1:role:0", "the dagger is selected");
    model.revision += 1;
    for (Widget &widget : model.widgets)
        if (widget.node == "TABLE_ITEMS")
            widget.value = widget.index == 1 ? "1" : "0";
        else if (widget.node == "EQUIP_BUTTON")
            widget.row_id = "inventory:9:character:1:role:0", widget.label = "Equip";
    router.SetInterface(model);
    ExpectEqual(surface.Last(), "widget:items\tEQUIP_BUTTON\t-1\tpress@5:inventory:9:character:1:role:0", "then put on");

    router.OnEvent({"item-tabs", "select", "3"});
    ExpectEqual(surface.Last(), "widget:items\tTABBTN_3\t-1\tpress", "a game tab");
    router.OnEvent({"header", "subject-next", ""});
    ExpectEqual(surface.Last(), "widget:items\tCHARACTERS_SCROLL\t1\tselect@5:1:1", "the next officer's items");
    router.OnEvent({"header", "subject-list", ""});
    Expect(surface.props["subject"]["open"] == true && surface.props["subject"]["rows"].size() == 2, "the items screen lists the subjects too");
    router.OnEvent({"header-subjects", "dismiss", ""});
    Expect(!surface.props["subject"].contains("open"), "a tap beside the list closes it");

    // The game's map window.
    model.revision += 1;
    model.widgets.push_back(Make("MAP_CAPTION", "text", "Treasure map"));
    model.widgets.push_back(Make("MAP_TEXT", "text", "A cross on the south shore."));
    model.widgets.push_back(Make("MAP_EXIT_BTN", "button", "", {"press"}));
    Widget picture = Make("MAP_PICTURE", "image");
    picture.images.push_back(ImageRef{});
    picture.images.back().kind = "texture";
    picture.images.back().image = "interfaces/maps/treasure";
    model.widgets.push_back(picture);
    router.SetInterface(model);
    Expect(surface.props["map"]["title"] == "Treasure map" && surface.props["map"]["image"]["texture"] == "interfaces/maps/treasure",
           "the map window: the game's picture by its texture name");
    Expect(router.WantsBack(), "back closes the map first");
    router.Back();
    ExpectEqual(surface.Last(), "widget:items\tMAP_EXIT_BTN\t-1\tpress", "back is the map's close");
}

// A HUD tap opens the character or the ship screen on its subject (hud::Hud::RequestSubject):
// the screen selects it with the header's switch, as the player would, once.
struct SubjectMount final : storm::ui::hud::Mount
{
    double now = 0.0;
    bool Measure(const std::string &, const std::string &, storm::ui::hud::Placement &) const override { return false; }
    bool ShowHud(const storm::ui::hud::View &) override { return true; }
    void HideHud() override {}
    void Send(const Action &) override {}
    void SetLanguage(const std::string &) override {}
    std::string Text(const std::string &key) override { return key; }
    void Log(const std::string &) override {}
    void Error(const std::string &) override {}
    double Now() const override { return now; }
    bool InterfaceActive() const override { return true; }
    bool ScreenMounted() const override { return true; }
    void Alert(const std::string &) override {}
};

static void OpenOnSubject(const std::string &models)
{
    {
        FakeSurface surface;
        Router router(surface);
        SubjectMount mount;
        storm::ui::hud::Hud hud(mount);
        router.AttachHud(&hud);
        hud.RequestSubject({"character", 9, -1});
        router.SetInterface(Load(models, "hero.json"));
        Expect(surface.Last().rfind("widget:lscchar_all\tCHARACTERS_SCROLL\t1\tselect@", 0) == 0,
               "a tapped officer (character 9, the navigator's post) is selected in the officer switch: " + surface.Last());
        const size_t sent = surface.commands.size();
        InterfaceModel again = Load(models, "hero.json");
        again.revision += 1;
        router.SetInterface(again);
        Expect(surface.commands.size() == sent, "the request is taken once");
    }
    {
        FakeSurface surface;
        Router router(surface);
        SubjectMount mount;
        storm::ui::hud::Hud hud(mount);
        router.AttachHud(&hud);
        hud.RequestSubject({"character", 1, -1});
        router.SetInterface(Load(models, "hero.json"));
        Expect(surface.commands.empty() && !hud.TakeSubject("character"), "the hero's portrait: the captain is already the subject, nothing to select");
        hud.RequestSubject({"character", 77, -1});
        InterfaceModel again = Load(models, "hero.json");
        again.revision += 1;
        router.SetInterface(again);
        Expect(surface.commands.empty() && !surface.errors.empty(), "a person without a post here: the captain stays, and that is said");
    }
    {
        FakeSurface surface;
        Router router(surface);
        SubjectMount mount;
        storm::ui::hud::Hud hud(mount);
        router.AttachHud(&hud);
        hud.RequestSubject({"ship", -1, 0});
        router.SetInterface(Load(models, "ship.json"));
        Expect(surface.Last().rfind("widget:ship\tSHIPS_SCROLL\t0\tselect@", 0) == 0, "a tapped flagship is selected in the squadron switch: " + surface.Last());
    }
    {
        FakeSurface surface;
        Router router(surface);
        SubjectMount mount;
        storm::ui::hud::Hud hud(mount);
        router.AttachHud(&hud);
        hud.RequestSubject({"ship", -1, 1});
        router.SetInterface(Load(models, "ship.json"));
        Expect(surface.commands.empty() && surface.errors.empty(), "the tapped ship is the one shown already: nothing to select");
        hud.RequestSubject({"ship", -1, 4});
        InterfaceModel again = Load(models, "ship.json");
        again.revision += 1;
        router.SetInterface(again);
        Expect(surface.commands.empty() && !surface.errors.empty(), "a ship the squadron does not have: said aloud");
    }
}

// The ship screen (hero_ship.cpp): the game's figures in three tabs, its windows as our dialogs.
static void Ship(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel model = Load(models, "ship.json");
    const auto revise = [&]() { model.revision += 1; router.SetInterface(model); };
    const auto remove = [&](const char *prefix) {
        std::erase_if(model.widgets, [prefix](const Widget &w) { return w.node.rfind(prefix, 0) == 0; });
    };
    router.SetInterface(model);
    ExpectEqual(router.Using(), "ship", "the ship draws on the ship screen");
    ExpectEqual(surface.tag, "s-ship", "s-ship mounted");
    Json view = surface.props;
    Expect(view["extras"]["rows"].empty() && surface.errors.empty(), "every widget is known: no extras, no errors");
    Expect(view["card"]["title"] == "Swallow" && view["card"]["sub"] == "Sloop \xC2\xB7 <yard.class> 6" &&
               view["card"]["captain"] == "<ship.captain> Peter Marsh \xC2\xB7 <ship.companion>" && view["card"]["rename"] == true &&
               view["card"]["image"]["texture"] == "interfaces/ships/Sloop",
           "the card: name, type and class, the companion captain, the picture, the quill");
    Expect(view["card"]["hull"]["value"] == 50 && view["card"]["crew"]["mark"] == 22 && view["card"]["sails"]["text"] == "100%",
           "hull, crew (minimum marked) and sails bars");
    Expect(view["left"][0]["cells"][1]["text"] == "13 <common.knots_short> <common.of> 15" && view["left"][2]["cells"][1]["text"] == "65\xC2\xB0" &&
               view["right"][0]["cells"][1]["text"] == "12 \xC3\x97 6 lb  \xE2\x80\xBA" && view["right"][2]["cells"][1]["text"] == "1\xC2\xA0" "240",
           "the figures rounded, the guns row opens the cannons window");
    Expect(view["subject"]["label"] == "Swallow" && view["subject"]["sub"] == "2 <common.of> 2", "the squadron's ships are the header subject");

    Expect(view["subject"]["list"] == true, "two ships offer the list");
    router.OnEvent({"header", "subject-list", ""});
    Expect(surface.props["subject"]["open"] == true && surface.props["subject"]["title"] == "<ship.squadron>" &&
               surface.props["subject"]["rows"].size() == 2 && surface.props["subject"]["rows"][1]["selected"] == true &&
               surface.props["subject"]["rows"][0]["icon"] == "ship",
           "the squadron's ships listed, the current one selected");
    router.OnEvent({"header-subject-0", "", ""});
    ExpectEqual(surface.Last(), "widget:ship\tSHIPS_SCROLL\t0\tselect@7:1:0", "a picked ship is selected as with ‹");
    Expect(!surface.props["subject"].contains("open"), "the pick closes the list");
    router.OnEvent({"header", "subject-prev", ""});
    ExpectEqual(surface.Last(), "widget:ship\tSHIPS_SCROLL\t0\tselect@7:1:0", "the previous ship");
    router.OnEvent({"ship-rename", "click", ""});
    ExpectEqual(surface.text.text, "Swallow", "the quill asks for the new name");
    router.SubmitText(surface.text.token, "Kestrel", true);
    ExpectEqual(surface.Last(), "widget:ship\tEDIT_BOX\t-1\tKestrel", "the name goes to the game's field");
    router.OnEvent({"stats-right", "row", "0"});
    ExpectEqual(surface.Last(), "widget:ship\tTABLE_OTHER\t8\tactivate@7:CannonType", "the guns row opens the cannons window");

    // «Обмен»: the game asks, the intent answers.
    surface.now += 1.0;
    revise();
    router.OnEvent({"ship-exchange", "click", ""});
    ExpectEqual(surface.Last(), "widget:ship\tSHIPS_SCROLL\t1\tactivate@7:1:1", "the exchange activates the current ship");
    model.widgets.push_back(Make("REMOVE_WINDOW_CAPTION", "text", "Companion ship"));
    model.widgets.push_back(Make("REMOVE_WINDOW_TEXT", "text", "Switch to the exchange?"));
    model.widgets.push_back(Make("REMOVE_ACCEPT_OFFICER", "button", "Ok"));
    model.widgets.push_back(Make("REMOVE_CANCEL_OFFICER", "button", "Cancel"));
    revise();
    ExpectEqual(surface.Last(), "widget:ship\tREMOVE_ACCEPT_OFFICER\t-1\tpress", "and the question is answered");
    Expect(surface.props["dialog"].is_null(), "the exchange question is not shown");
    remove("REMOVE_");
    revise();

    // «Команда»: bars, morale price, the morale question and the shares.
    router.OnEvent({"header-tabs", "select", "crew"});
    view = surface.props;
    Expect(view["crew"]["groups"].size() == 3 && view["crew"]["groups"][0]["value"] == 62 && view["crew"]["groups"][1]["word"] == "green" &&
               view["crew"]["morale"] == 45 && view["crew"]["moraleWord"] == "normal",
           "crew experience and morale from the adapter, the game's words");
    Expect(view["facts"][1]["title"] == "1 240 pesos" && view["facts"][1]["meta"] == "Ship upkeep" && view["crewActions"][1]["sub"] == "340 <ship.money>",
           "food and upkeep lines, the morale price");
    router.OnEvent({"crew-morale", "click", ""});
    ExpectEqual(surface.Last(), "widget:ship\tCREW_MORALE_BUTTON\t-1\tpress", "raise morale asks the game");
    model.widgets.push_back(Make("REMOVE_WINDOW_CAPTION", "text", "Raise morale"));
    model.widgets.push_back(Make("REMOVE_ACCEPT_OFFICER", "button", "Ok"));
    model.widgets.push_back(Make("REMOVE_CANCEL_OFFICER", "button", "Cancel"));
    revise();
    Expect(surface.props["dialog"]["kind"] == "question" && surface.props["dialog"]["title"] == "<ship.raise_morale_for> 340 <ship.money>?",
           "the morale question with its price");
    router.OnEvent({"dialog-accept", "click", ""});
    ExpectEqual(surface.Last(), "widget:ship\tREMOVE_ACCEPT_OFFICER\t-1\tpress", "accepted");
    Expect(router.WantsBack(), "back closes the question first");
    router.Back();
    ExpectEqual(surface.Last(), "widget:ship\tREMOVE_CANCEL_OFFICER\t-1\tpress", "back cancels it");
    remove("REMOVE_");
    model.widgets.push_back(Make("PARTITION_WINDOW_CAPTION", "text", "Crew shares"));
    model.widgets.push_back(Make("PARTITION_OK", "button", "Pay"));
    model.widgets.push_back(Make("PARTITION_CANCEL", "button", "Cancel"));
    revise();
    view = surface.props["dialog"];
    Expect(view["kind"] == "shares" && view["shares"][2]["v"] == "500" && view["debts"][0]["v"] == "1\xC2\xA0" "200" && view["debts"][1]["v"] == "\xE2\x80\x94" &&
               view["actions"][0]["label"] == "<ship.pay> 500" && view["subtitle"] == "<ship.last_payment> 1 September 1654",
           "the shares window as figures, the pay button with its sum");
    router.OnEvent({"shares-pay", "click", ""});
    ExpectEqual(surface.Last(), "widget:ship\tPARTITION_OK\t-1\tpress", "paid by the game's button");
    remove("PARTITION_");
    revise();

    // «Трюм»: a row opens the drop window; the control keeps what stays; «За борт N» held.
    router.OnEvent({"header-tabs", "select", "cargo"});
    view = surface.props;
    Expect(view["list"]["rows"].size() == 3 && view["list"]["rows"][1]["meta"] == "Pack 20 \xC2\xB7 Pack weight 1" &&
               view["list"]["rows"][0]["image"]["texture"] == "atlas:GOODS/Balls" && view["load"]["text"] == "189 <common.of> 700 <transfer.cwt>",
           "the goods with their packs and pictures, the deadweight");
    router.OnEvent({"goods", "row", "1"});
    ExpectEqual(surface.Last(), "widget:ship\tTABLE_LIST\t1\tactivate@7:ship:good:5:owner:31", "the row opens its drop window by the good's identity");
    model.widgets.push_back(Make("QTY_CAPTION", "text", "Gunpowder"));
    model.widgets.push_back(Make("QTY_INFO_SHIP_QTY", "text", "220"));
    Widget edit = Make("QTY_EDIT", "edit");
    edit.value = "0";
    model.widgets.push_back(edit);
    model.widgets.push_back(Make("QTY_OK_BUTTON", "button", "Ok"));
    model.widgets.push_back(Make("QTY_CANCEL_BUTTON", "button", "Cancel"));
    model.widgets.push_back(Make("QTY_BOMB_BUTTON", "button", "Mine"));
    revise();
    view = surface.props;
    Expect(view["list"]["rows"][1]["q"]["value"] == 220 && view["list"]["rows"][1]["q"]["max"] == 220 && view["cargoActions"].size() == 2 &&
               view["cargoActions"][1]["disabled"] == true && view["cargoActions"][1]["hold"] == true,
           "the open row: everything stays, nothing to drop yet, the mine button for powder");
    router.OnEvent({"goods", "change", "200"});
    surface.now += 1.0;
    router.Tick();
    ExpectEqual(surface.Last(), "widget:ship\tQTY_EDIT\t-1\t20", "the control sets the amount to drop");
    // The game takes the amount out of the hold while it is set: the row and the deadweight follow.
    for (Widget &widget : model.widgets)
        if (widget.node == "QTY_EDIT")
            widget.value = "220";
        else if (widget.node == "QTY_INFO_SHIP_QTY")
            widget.label = "0";
        else if (widget.node == "TABLE_OTHER" && widget.row_id == "Capacity")
            widget.cells[2].text = "178 / 700";
    std::erase_if(model.widgets, [](const Widget &w) { return w.node == "TABLE_LIST" && w.index == 1; });
    surface.now += 2.0;
    revise();
    view = surface.props;
    Expect(view["list"]["rows"].size() == 3 && view["list"]["rows"][1]["name"] == "Gunpowder" && view["list"]["rows"][1]["q"]["value"] == 0 &&
               view["list"]["rows"][1]["cells"][0] == "220" && view["load"]["text"] == "189 \xE2\x80\xBA 178 <common.of> 700 <transfer.cwt>" &&
               view["cargoActions"][1]["label"] == "<ship.overboard> 220",
           "all of it set to go: the row stays in place, the deadweight shows the drop");
    router.OnEvent({"goods-drop", "click", ""});
    ExpectEqual(surface.Last(), "widget:ship\tQTY_OK_BUTTON\t-1\tpress", "«За борт» is the game's OK");
    router.OnEvent({"goods", "row", "0"});
    ExpectEqual(surface.Last(), "widget:ship\tQTY_CANCEL_BUTTON\t-1\tpress", "another good first closes the open window");
    Expect(router.WantsBack(), "back closes the drop window first");
    router.Back();
    ExpectEqual(surface.Last(), "widget:ship\tQTY_CANCEL_BUTTON\t-1\tpress", "back cancels the drop");
    remove("QTY_");
    revise();
    ExpectEqual(surface.Last(), "widget:ship\tTABLE_LIST\t0\tactivate@7:ship:good:0:owner:31", "then the other good opens");

    // The cannons window: guns per side walked one arrow press per revision.
    router.OnEvent({"header-tabs", "select", "overview"});
    model.widgets.push_back(Make("CANNONS_CAPTION", "text", "Cannons"));
    model.widgets.push_back(Make("CANNONS_TEXT", "text", "Cannon 6 lb"));
    for (const char *side : {"F", "B", "R", "L"})
    {
        model.widgets.push_back(Make(("CANNONS_QTY_" + std::string(side)).c_str(), "text", "1"));
        model.widgets.push_back(Make(("CANNONS_LEFT_" + std::string(side)).c_str(), "button"));
        model.widgets.push_back(Make(("CANNONS_RIGHT_" + std::string(side)).c_str(), "button"));
    }
    model.widgets.push_back(Make("CANNONS_OK", "button", "Ok"));
    model.widgets.push_back(Make("CANNONS_CANCEL", "button", "Cancel"));
    revise();
    view = surface.props["sheet"];
    Expect(view["title"] == "Cannons" && view["sides"].size() == 4 && view["sides"][0]["q"]["max"] == 1 && view["actions"].size() == 1,
           "the cannons sheet: four sides, no spare guns in the hold");
    router.OnEvent({"guns-port", "change", "3"});
    ExpectEqual(surface.Last(), "widget:ship\tCANNONS_RIGHT_L\t-1\tpress", "one more gun on the port side");
    for (Widget &widget : model.widgets)
        if (widget.node == "CANNONS_QTY_L")
            widget.label = "2";
    revise();
    ExpectEqual(surface.Last(), "widget:ship\tCANNONS_RIGHT_L\t-1\tpress", "and another after the engine answered");
    const size_t sent = surface.commands.size();
    revise(); // the game took no more (2 again): the intent ends
    Expect(surface.commands.size() == sent, "no press without progress");
    router.Back();
    ExpectEqual(surface.Last(), "widget:ship\tCANNONS_CANCEL\t-1\tpress", "back cancels the cannons window");
}

static void Journal(const std::string &)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel model;
    model.screen = "questbook";
    model.language = "english";
    model.opening = 8;
    model.revision = 30;
    model.widgets.push_back(Make("TITLE", "heading", " Log "));
    for (const char *tab : {"TABBTN_QUEST", "TABBTN_QUESTARJ", "TABBTN_INFO", "TABBTN_CASHBOOK", "TABBTN_SHIP_PLACE", "TABBTN_STOREBOOK", "TABBTN_STATISTIC"})
    {
        Widget button = Make(tab, "image", "", {"press"});
        button.value = std::string(tab) == "TABBTN_QUEST" ? "1" : "0";
        model.widgets.push_back(button);
        model.widgets.push_back(Make((std::string("TABSTR_") + (tab + 7)).c_str(), "text", tab + 7));
    }
    for (int i = 0; i < 2; ++i)
    {
        Widget title = Make("QUEST_TITLE", "choice", i ? "Debts" : "Burden", {"open"});
        title.index = i;
        title.row_id = i ? "debts" : "burden";
        model.widgets.push_back(title);
    }
    router.SetInterface(model);
    ExpectEqual(router.Using(), "questbook", "the journal draws on the journal screen");
    Json view = surface.props;
    Expect(view["section"] == "quests" && view["list"].size() == 2 && view["subtabs"].size() == 2 && view["entry"].is_null() &&
               view["sections"][0]["count"] == "2",
           "quests: active and completed, the titles, nothing open");
    Expect(view["extras"]["rows"].empty(), "every widget is known: no extras");

    router.OnEvent({"entries", "row", "1"});
    ExpectEqual(surface.Last(), "widget:questbook\tQUEST_TITLE\t1\topen@8:debts", "a title opens its text");
    InterfaceModel text = model;
    text.revision += 1;
    text.widgets.erase(std::remove_if(text.widgets.begin(), text.widgets.end(), [](const Widget &w) { return w.node == "QUEST_TITLE"; }), text.widgets.end());
    text.widgets.push_back(Make("QUEST_TEXT", "text", "1 May 1654 10:02\nArrived.\nMet the brother.\n2 May 1654 09:00\nLeft."));
    router.SetInterface(text);
    view = surface.props;
    Expect(view["list"].size() == 2 && view["list"][1]["selected"] == true && view["entry"]["title"] == "Debts" &&
               view["entry"]["entries"].size() == 2 && view["entry"]["entries"][0]["text"] == "Arrived.\nMet the brother.",
           "the titles stay while the text is open; the text split into dated entries");
    const size_t sent = surface.commands.size();
    router.OnEvent({"entries", "row", "0"});
    ExpectEqual(surface.Last(), "back", "another entry: back to the titles first");
    model.revision = text.revision + 1;
    router.SetInterface(model);
    ExpectEqual(surface.Last(), "widget:questbook\tQUEST_TITLE\t0\topen@8:burden", "then open it");
    Expect(surface.commands.size() == sent + 2, "two steps");

    text.revision = model.revision + 1;
    router.SetInterface(text);
    Expect(router.WantsBack(), "an open text: back is the screen's");
    router.Back();
    ExpectEqual(surface.Last(), "back", "back closes the text");
    model.revision = text.revision + 1;
    router.SetInterface(model);
    Expect(surface.commands.back() == "back" && surface.commands[surface.commands.size() - 2] == "back", "and goes on to leave the journal");

    router.OnEvent({"section-stats", "click", ""});
    ExpectEqual(surface.Last(), "widget:questbook\tTABBTN_STATISTIC\t-1\tpress", "a section presses its game tab");
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

    // Nothing reaches the screen unlogged, the launcher's notices included: each is logged when it appears,
    // once while it stays, and again when it comes back.
    const auto shownCount = [&surface](const std::string &line) {
        return std::count(surface.shown.begin(), surface.shown.end(), line);
    };
    const std::string volumes = "danger (notice on screen app:launcher): <launcher.failed> "
                                "<launcher.result.import_archive_volumes> setup-2.bin <launcher.kept>";
    Expect(shownCount(volumes) == 1, "a failed import's notice is logged");
    for (const char *key : {"picker_unavailable", "exit_game_first", "picker_unavailable"})
    {
        router.SetState("launcher", launcher({{"phase", "setup"}, {"library", true}, {"hasGame", true}, {"files", true},
                                              {"result", {{"key", key}, {"args", Json::array()}}}}));
        router.SetState("launcher", launcher({{"phase", "setup"}, {"library", true}, {"hasGame", true}, {"files", true},
                                              {"result", {{"key", key}, {"args", Json::array()}}}}));
    }
    Expect(shownCount("info (notice on screen app:launcher): <launcher.result.picker_unavailable>") == 2,
                "picker_unavailable is logged at each appearance, not at each build");
    Expect(shownCount("info (notice on screen app:launcher): <launcher.result.exit_game_first>") == 1, "exit_game_first is logged");
    router.SetState("launcher", launcher({{"phase", "ready"}, {"library", true}, {"hasGame", true},
                                          {"exit", {{"reason", "anr"}, {"when", "today"}}}}));
    Expect(shownCount("warn (notice on screen app:launcher): <launcher.exit_previous>") == 1, "the previous exit's notice is logged");
    ExpectEqual(surface.props["view"].get<std::string>(), "ready", "ready view");
    ExpectEqual(surface.props["notices"][0]["text"].get<std::string>(), "<launcher.exit_previous>", "previous exit noticed");
    router.OnEvent({"play", "click", ""});
    ExpectEqual(verb(), "APP play ", "play");
    router.OnEvent({"about", "click", ""});
    ExpectEqual(verb(), "APP about ", "about from the header");
    // While the app checks the files and installs the layers before the game starts, «Играть»
    // shows it, for as long as it takes (not only the router's pending second and a half).
    surface.now += 5.0;
    router.SetState("launcher", launcher({{"phase", "ready"}, {"library", true}, {"hasGame", true}, {"starting", true}}));
    Expect(surface.props["play"]["busy"].get<bool>(), "starting: «Играть» is busy");
    router.SetState("launcher", launcher({{"phase", "ready"}, {"library", true}, {"hasGame", true}, {"starting", false}}));
    Expect(!surface.props["play"]["busy"].get<bool>(), "started (or failed): «Играть» is free again");

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
                                   Json{{"id", "2"}, {"kind", "archive"}, {"name", "a.zip"}, {"state", "complete"}, {"main", "import"}, {"menu", {"delete"}}},
                                   Json{{"id", "3"}, {"kind", "magnet"}, {"name", "Quiet"}, {"state", "metadata"}, {"stalled", true}, {"peers", 0.0},
                                        {"networkError", "Tracker x: 404"}, {"main", "pause"}},
                                   Json{{"id", "4"}, {"kind", "magnet"}, {"name", "Full disk"}, {"state", "error"}, {"errorKind", "storage-full"},
                                        {"error", "No space left on device"}, {"main", "resume"}, {"menu", {"delete"}}}});
    router.SetState("launcher", Json{{"screen", "downloads"}, {"language", "english"}, {"downloads", {{"sets", sets}}}});
    ExpectEqual(surface.tag, "s-downloads", "downloads screen");
    ExpectEqual(surface.props["sets"][0]["action"].get<std::string>(), "pause", "an active set pauses inline");
    Expect(surface.props["sets"][0]["progress"].get<double>() == 50, "download progress");
    ExpectEqual(surface.props["sets"][1]["button"]["id"].get<std::string>(), "btn-2", "import is an inline button");
    Expect(surface.props["sets"][2]["meta"].get<std::string>().find("<downloads.stalled_no_peers>: Tracker x: 404") != std::string::npos &&
               surface.props["sets"][2]["metaTone"].get<std::string>() == "danger",
           "a stalled set says why");
    Expect(surface.props["sets"][3]["meta"].get<std::string>().find("<downloads.storage_full> — No space left on device") != std::string::npos,
           "a full disk is named");
    Expect(shownCount("danger (notice on screen app:downloads): Full disk: No space left on device") == 1, "a set's error is logged");
    Expect(shownCount("warn (notice on screen app:downloads): Quiet: <downloads.stalled_no_peers>: Tracker x: 404") == 1,
                "a stalled set's reason is logged");
    router.OnEvent({"set-1", "action", "pause"});
    ExpectEqual(verb(), "APP pause 1", "pause glyph");
    router.OnEvent({"btn-2", "click", ""});
    ExpectEqual(verb(), "APP import 2", "import button");
    router.OnEvent({"set-1", "menu", ""});
    Expect(surface.props["sheet"]["actions"].size() == 2 && surface.props["sheet"]["actions"][1]["hold"].get<bool>(),
           "«⋯» lists the rare verbs; delete is held");
    router.OnEvent({"menu-delete", "click", ""});
    ExpectEqual(verb(), "APP delete 1", "held delete");
    router.SetState("launcher", Json{{"screen", "downloads"}, {"language", "english"},
                                     {"downloads", {{"sets", sets}, {"dialog", {{"kind", "candidates"}, {"id", "1"},
                                                    {"candidates", {{{"label", "Game"}, {"kind", "loose-game-candidate"}, {"files", 3}, {"bytes", 10}},
                                                                    {{"label", "setup.exe"}, {"kind", "installer-candidate"}, {"files", 2}, {"bytes", 20}}}},
                                                    {"all", {{"files", 9}, {"bytes", 40}}}}}}}});
    ExpectEqual(surface.props["dialog"]["kind"].get<std::string>(), "pick", "candidates are picked one at a time");
    Expect(surface.props["dialog"]["rows"].size() == 3 && surface.props["dialog"]["rows"][1]["title"].get<std::string>() ==
                                                              "setup.exe · <downloads.kind.installer-candidate>",
           "every candidate and all files");
    router.OnEvent({"candidate-1", "click", ""});
    ExpectEqual(verb(), "APP download 1:1", "one candidate");
    router.OnEvent({"candidate-all", "click", ""});
    ExpectEqual(verb(), "APP download 1:-1", "every file");

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
    ExpectEqual(surface.props["shareLog"]["title"].get<std::string>(), "<about.share_log>", "«Поделиться логом» row");
    router.OnEvent({"share-log", "click", ""});
    ExpectEqual(verb(), "APP share-log ", "the log goes to the host's share sheet");

    const size_t errors = surface.errors.size();
    router.SetState("launcher", Json{{"screen", "nowhere"}});
    Expect(surface.errors.size() == errors + 1, "an unknown application screen is an error");

    // Game interface ids are ini stems: the game's credits (about.ini) are "about" and must
    // never reach the application's about screen.
    for (const auto &entry : PortedScreens())
        Expect(std::string_view(entry.id).find(':') == std::string_view::npos ||
                   std::string_view(entry.id).starts_with("app:"),
               std::string("only application screens use a ':' id: ") + entry.id);
    InterfaceModel game;
    game.screen = "about";
    game.opening = 1;
    game.revision = 1;
    router.SetInterface(game);
    Expect(router.Using() == "generic", "the game's about interface is not the application's about screen");
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
    Expect(surface.commands.empty() && surface.props["primary"].is_null(), "opening a row sends nothing and offers nothing to save");
    router.OnEvent({"goods", "change", "500"});
    Expect(surface.props["list"]["rows"][3]["cells"][1] == "500", "the amount shows at once");
    {
        // The owner: «у казначея тоже нужна кнопка» — the standing order is the same draft flow, saved by «Сохранить N».
        const std::string order = "widget:goodstransfer\tANDROID_GOODS_ORDER\t3\torder@5:Powder=";
        DraftFlow("goodstransfer", router, surface, "goods", 520, "<goods.save> 520",
                  [](const std::string &command) { return command.find("\torder@") != std::string::npos; },
                  [&](const std::vector<std::string> &commands) { return commands == std::vector<std::string>{order + "520"}; }, [] {});
        Expect(surface.props["list"]["rows"][3]["cells"][1] == "520", "the saved amount shows until the game's order catches up");
        InterfaceModel saved = list;
        for (Widget &widget : saved.widgets)
            if (widget.node == "ANDROID_GOODS_ORDER" && widget.row_id == "Powder")
                widget.context["order"] = "520";
        saved.revision += 5;
        router.SetInterface(saved);
        Expect(surface.props["list"]["rows"][3]["q"]["value"] == 520 && surface.props["primary"].is_null(), "the game's new order: nothing left to save");
        router.OnEvent({"goods", "change", "300"});
        surface.commands.clear();
        router.OnEvent({"header-back", "click", ""});
        Expect(surface.commands.empty() && surface.props["list"]["rows"][3]["q"]["value"] == 520, "← first drops a changed order, saving nothing");
        router.SetInterface(list);
    }
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
    DialogueText(models);
    DialogueOverClosingInterface(models);
    DialogueNumber(models);
    Nations(models);
    Hero(models);
    Items(models);
    Ship(models);
    OpenOnSubject(models);
    Journal(models);
    Launcher();
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("screen routing checks passed\n");
    return failures ? 1 : 0;
}
