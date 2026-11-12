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
    void Alert(const std::string &text) override
    {
        Toast(text);
    }
    void Toast(const std::string &text) override
    {
        toasts.push_back(text);
    }
    std::vector<std::string> toasts;
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
    Expect(actions[0]["disabled"] == true && actions[0]["variant"] == "quiet" && actions[0]["reason"] == "<reading.cannot_leave>",
           "a forced encounter shows «sail on» disabled with its reason, as the same quiet control");
    router.OnEvent({"ok", "click", ""});
    ExpectEqual(surface.Last(), "widget:map\tB_OK\t-1\tpress", "engage presses B_OK");
    const size_t sent = surface.commands.size();
    router.OnEvent({"card", "dismiss", ""});
    Expect(surface.commands.size() == sent && surface.errors.empty(), "a tap beside the card answers nothing");

    router.SetInterface(Load(models, "reading-dirsailenc.json"));
    Expect(surface.props["actions"].size() == 1 && surface.props["actions"][0]["label"] == "Есть!" && surface.props["icon"] == "spyglass" &&
               surface.errors.empty(),
           "the straight-sail signal: one answer, the primary");
    router.OnEvent({"ok", "click", ""});
    ExpectEqual(surface.Last(), "widget:dirsailenc\tB_OK\t-1\tpress", "«Есть!» presses B_OK");

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
    InterfaceModel fort;
    fort.screen = "fortcapture";
    fort.opening = 9;
    router.SetInterface(fort);
    Expect(router.Using() == "fortcapture" && surface.props["mode"] == "none" && surface.errors.empty(),
           "the fort capture moment draws nothing (the game closes it at once)");
    router.SetInterface(Load(models, "reading-frame.json"));
    router.OnEvent({"frame", "click", ""});
    ExpectEqual(surface.Last(), "back", "a tap ends the interlude the game's way (the global back)");
}

static void FramePicture(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "reading-frame.json"));
    Expect(surface.props["picture"].is_null(), "the interlude without a quest picture shows none (the ini's frame ornament is left out)");
    router.SetInterface(Load(models, "reading-frame-picture.json"));
    Expect(surface.props["picture"]["texture"] == "loading/inside/kiss" && surface.props["picture"]["uv"] == "0,0,1,1",
           "the quest's picture is the interlude's backdrop, by its texture");
}

static void Maps(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "map-view.json"));
    Expect(router.Using() == "mapview" && surface.tag == "s-map", "the atlas is a ported screen");
    Expect(surface.errors.empty() && surface.props["extras"]["rows"].empty(), "every node of the atlas is known");
    const Json &map = surface.props["map"];
    Expect(map["texture"] == "interfaces/maps/martinica" && map["base"] == Json::array({2048, 2048}) &&
               map["view"] == Json::array({0, 0, 2048, 2048}) && map["markers"].empty(),
           "the atlas map: the game's texture, its base size and view");
    Expect(surface.props["maps"].size() == 5 && surface.props["maps"][1]["selected"] == true &&
               surface.props["maps"][1]["cells"][0]["text"] == "Карта Мартиники" && surface.props["info"].is_null(),
           "the atlas lists the hero's maps, the shown one selected");
    router.OnEvent({"maps", "row", "2"});
    ExpectEqual(surface.Last(), "widget:mapview\tTABLE_MAPS\t3\tselect@7:3", "a map row selects it");
    router.OnEvent({"map", "point", "1024.0,900.5"});
    ExpectEqual(surface.Last(), "widget:mapview\tMAP\t-1\tpoint@7:1024.0,900.5", "a tap on the map is the game's right click there");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "widget:mapview\tEXIT_BTN\t-1\tpress", "← is the exit button");

    router.SetInterface(Load(models, "map-best.json"));
    const Json &markers = surface.props["map"]["markers"];
    Expect(surface.props["map"]["texture"] == "interfaces/maps/map_good" && surface.errors.empty(),
           "the excellent map's picture is MAIN_BACK under its transparent MAP");
    Expect(router.Using() == "mapbest" && markers.size() == 8 && markers[0]["texture"] == "atlas:NATIONS/France" &&
               markers[0]["x"] == 1530 && markers[0]["w"] == 16 && markers[7]["id"] == "PShip",
           "the excellent map carries the game's markers in base units");
    Expect(!router.WantsBack(), "no colony open: back is the game's");

    router.SetInterface(Load(models, "map-best-colony.json"));
    const Json &info = surface.props["info"];
    Expect(surface.errors.empty() && surface.props["extras"]["rows"].empty(), "every node of the colony window is known");
    Expect(info["title"] == "Сен-Пьер" && info["nation"] == "nation-france" && info["sub"] == "1–3 дня пути",
           "colony sheet: name, flag, travel");
    Expect(info["facts"].size() == 4 && info["facts"][1] == Json({{"k", "Казна"}, {"v", "86 300"}}) && info["factsTitle"] == "Сведения о колонии",
           "colony facts pair the game's two columns");
    Expect(info["page"] == "trade" && info["tabs"].size() == 3 && info["tabs"][0]["selected"] == true && info["tabs"][1]["id"] == "assistant" &&
               info["tabs"][2]["id"] == "city",
           "the colony sheet opens on its goods; the trade assistant and the town are pages of their own");
    const Json &sections = info["sections"];
    Expect(sections.size() == 3 && sections[0]["title"] == "Экспорт" && sections[0]["goods"].size() == 2 &&
               sections[0]["goods"][1] == Json({{"texture", "atlas:GOODS/Coffee"}, {"uv", ""}, {"label", "Кофе"}}) &&
               sections[0]["lines"].empty() && sections[1]["goods"][0]["label"] == "Ром" &&
               sections[2]["title"] == "Помощник: прибыль из Бас-Тера" && sections[2]["lines"].size() == 2,
           "sections: goods pictures with their names, the trade assistant's lines");
    Expect(router.WantsBack(), "an open colony takes the back key");
    router.OnEvent({"info-close", "click", ""});
    ExpectEqual(surface.Last(), "widget:mapbest\tMAP\t-1\trelease", "the cross lets go of the colony (the game's right button up)");
    router.Back();
    ExpectEqual(surface.Last(), "widget:mapbest\tMAP\t-1\trelease", "so does the back key");

    // As the game really sends it (MapBest.c): the goods are pictures the script drew at its own places (the engine
    // exports them with x, y and the good's name), the assistant's profits two joined columns of numbers.
    {
        InterfaceModel real = Load(models, "map-best-colony.json");
        Widget picture_template;
        for (const Widget &widget : real.widgets)
            if (widget.node == "EXPORT1_PICTURE")
                picture_template = widget;
        real.widgets.erase(std::remove_if(real.widgets.begin(), real.widgets.end(),
                                          [](const Widget &w) {
                                              return w.node.find("_PICTURE") != std::string::npos && w.node != "INFO_NATION_PICTURE" ||
                                                     w.node == "EXPORT_INFO" || w.node == "IMPORT_INFO" || w.node.rfind("TRADEASSISTANT_", 0) == 0;
                                          }),
                           real.widgets.end());
        int index = 0;
        const auto picture = [&](const char *id, const char *name, int x, int y) {
            Widget widget = picture_template;
            widget.node = "GOODS_PICTURES";
            widget.index = index++;
            widget.row_id = id;
            widget.label = name;
            widget.images[0].group = "GOODS";
            widget.images[0].image = id;
            widget.context = {{"x", std::to_string(x)}, {"y", std::to_string(y)}};
            real.widgets.push_back(widget);
        };
        const auto text = [&](const char *node, const char *label) {
            Widget widget;
            widget.node = node;
            widget.type = "text";
            widget.label = label;
            real.widgets.push_back(widget);
        };
        picture("Sugar", "Сахар", 410, 245);
        picture("Coffee", "Кофе", 438, 245);
        picture("Rum", "Ром", 410, 330);
        picture("Weapon", "Оружие", 410, 415);
        picture("Rum", "Ром", 230, 395);      // the assistant, bought here and sold there
        picture("Sugar", "Сахар", 230, 423);
        picture("Coffee", "Кофе", 310, 395);  // and the way back
        text("TRADEASSISTANT_CAPTION", "Торговый помощник");
        text("TRADEASSISTANT_TO", "Туда");
        text("TRADEASSISTANT_FROM", "Обратно");
        text("TRADEASSISTANT_PRICES1", "6.4 5.7");
        text("TRADEASSISTANT_PRICES2", "2.1");
        text("CONTRABAND_CAPTION", "Контрабанда");
        FakeSurface real_surface;
        Router real_router(real_surface);
        real_router.SetInterface(real);
        const Json &rows = real_surface.props["info"]["sections"];
        Expect(real_surface.errors.empty() && rows.size() == 5 && rows[0]["title"] == "Экспорт" && rows[0]["goods"].size() == 2 &&
                   rows[1]["goods"][0]["label"] == "Ром" && rows[2]["title"] == "Контрабанда" && rows[2]["goods"][0]["texture"] == "atlas:GOODS/Weapon",
               "the colony's goods by the script's bands: " + rows.dump());
        Expect(rows[3]["title"] == "Туда" && rows[3]["page"] == "assistant" && rows[0]["page"] == "trade" && rows[3]["goods"].size() == 2 && rows[3]["goods"][0]["label"] == "Ром" &&
                   rows[3]["goods"][0]["value"] == "+6.4" && rows[3]["goods"][1]["value"] == "+5.7" && rows[4]["title"] == "Обратно" &&
                   rows[4]["goods"][0] == Json({{"texture", "atlas:GOODS/Coffee"}, {"uv", ""}, {"label", "Кофе"}, {"value", "+2.1"}}) && rows[4]["page"] == "assistant",
               "the assistant: each good with its profit on the same row, both directions: " + rows[3].dump() + rows[4].dump());
        // The town page is the other tab.
        real_router.OnEvent({"info-tabs", "select", "city"});
        Expect(real_surface.props["info"]["page"] == "city" && real_surface.props["info"]["tabs"][2]["selected"] == true, "«Город» shows the facts");
        real_router.OnEvent({"info-tabs", "select", "assistant"});
        Expect(real_surface.props["info"]["page"] == "assistant", "«Помощник» shows the profits");
    }
}

static void Notices(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "reading-colonyinfo.json"));
    Expect(surface.tag == "s-reading" && surface.props["mode"] == "notice" && surface.errors.empty(), "colony news is a notice");
    Expect(surface.props["title"] == "<reading.colony_news>" && surface.props["flags"].size() == 1 &&
               surface.props["flags"][0]["icon"] == "nation-spain",
           "colony news: our title and the besieger's flag");
    Expect(surface.props["actions"].size() == 1 && surface.props["actions"][0]["label"] == "Ok", "the game's OK stays");
    for (const char *id : {"ok", "card-close", "card"})
    {
        router.OnEvent({id, std::string(id) == "card" ? "dismiss" : "click", ""});
        ExpectEqual(surface.Last(), "widget:colonyinfo\tB_OK\t-1\tpress", std::string(id) + " closes the news with OK");
    }
    router.SetInterface(Load(models, "reading-nationlegend.json"));
    const Json &flags = surface.props["flags"];
    Expect(surface.props["title"] == "События" && flags.size() == 3 && flags[0]["icon"] == "nation-england" &&
               flags[1]["icon"] == "rel-war" && flags[1]["word"] == "<reading.war>" && flags[2]["icon"] == "nation-spain",
           "nation news: flag, relation, flag");
    Expect(surface.props["actions"].empty() && surface.errors.empty(), "a news line closes with the cross only");
    router.OnEvent({"card-close", "click", ""});
    ExpectEqual(surface.Last(), "widget:nationlegend\tB_OK\t-1\tpress", "the cross presses OK");
}

static void Rest(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel model = Load(models, "rest.json");
    router.SetInterface(model);
    ExpectEqual(surface.tag, "s-rest", "rest is a ported screen");
    Expect(surface.errors.empty() && surface.props["extras"]["rows"].empty(), "every node of the rest window is known");
    const Json &hours = surface.props["hours"];
    Expect(hours.size() == 5 && hours[3]["selected"] == true && hours[3]["id"] == "12", "half the slider is the 12-hour span");
    Expect(surface.props["actions"][0]["label"] == "<rest.go>" && !surface.props["actions"][0].contains("sub"), "a preset span needs no sub line");
    const char *const spans[][2] = {{"1", "5"}, {"3", "13"}, {"6", "25"}, {"12", "50"}, {"24", "100"}};
    for (const auto &[span, position] : spans)
    {
        router.OnEvent({"hours", "select", span});
        ExpectEqual(surface.Last(), std::string("widget:tavernwait\tHOURS_SLIDE\t-1\tset@5:") + position,
                    std::string(span) + " h is the smallest slider position the game reads as that many hours");
    }
    router.OnEvent({"go", "click", ""});
    ExpectEqual(surface.Last(), "widget:tavernwait\tOK_BTN\t-1\tpress", "rest presses OK");
    router.OnEvent({"rest-close", "click", ""});
    ExpectEqual(surface.Last(), "widget:tavernwait\tCANCEL_BTN\t-1\tpress", "the cross is the game's Cancel");
    model.widgets[0].value = "29"; // 6.96 h: the game says 6
    model.revision = 5;
    router.SetInterface(model);
    Expect(surface.props["hours"][2]["selected"] == true, "the span follows the game's truncation");
    model.widgets[0].value = "33"; // 7 h: no preset
    model.revision = 6;
    router.SetInterface(model);
    ExpectEqual(surface.props["actions"][0]["sub"], "Полдня", "another span is named by the game's text");
}

static void TradeBook(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "tradebook.json"));
    ExpectEqual(surface.tag, "s-tradebook", "trade book is a ported screen");
    Expect(surface.errors.empty() && surface.props["extras"]["rows"].empty(), "every node of the trade book is known");
    const Json &towns = surface.props["towns"];
    Expect(towns.size() == 3 && towns[1]["selected"] == true && towns[0]["cells"][0]["icon"] == "nation-spain" &&
               towns[0]["cells"][0]["sub"] == "Куба" && towns[0]["cells"][1]["text"] == "14.09.1654",
           "towns: flag, town, island, date");
    Expect(towns[2]["cells"][1]["text"] == "<tradebook.no_data>" && towns[2]["cells"][0]["icon"] == "nation-pirates", "unknown prices say so");
    const Json &columns = surface.props["columns"];
    Expect(columns.size() == 4 && columns[0]["label"] == "Порт-Ройал" && columns[1]["label"] == "Продажа" && columns[3]["label"] == "Запас",
           "price list headed by the chosen town and the game's column names");
    const Json &prices = surface.props["prices"];
    Expect(prices.size() == 3 && prices[1]["cells"][0]["tag"] == "export" && prices[2]["cells"][0]["tag"] == "contraband" &&
               !prices[0]["cells"][0].contains("tag") && prices[0]["cells"][0]["image"]["texture"] == "atlas:GOODS/Food" &&
               prices[0]["cells"][3] == "1835",
           "goods: picture, trade type tag, sell, buy, stock");
    Expect(surface.props["sections"] == "tradebook", "hero sections in the header");
    router.OnEvent({"towns", "row", "0"});
    ExpectEqual(surface.Last(), "widget:tradebook\tTABLE_CITY\t0\tselect@8:city:0", "a town row selects it");
    router.OnEvent({"header", "section", "ship"});
    ExpectEqual(surface.Last(), "widget:tradebook\tI_SHIP\t-1\tpress", "a section icon presses its button");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "widget:tradebook\tEXIT_BTN\t-1\tpress", "← is the exit button");
}

// The structure the game really sends (recorded on a device, texts replaced): no exit button,
// the section buttons twice, a padded title, the first town's prices with no row selected and
// "hour date" in the date column; an unvisited book sends both tables empty.
static void TradeBookAsSent(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "tradebook-first.json"));
    Expect(router.Using() == "tradebook" && surface.tag == "s-tradebook" && surface.errors.empty() &&
               surface.props["extras"]["rows"].empty(),
           "the trade book as the game sends it is admitted without extras");
    ExpectEqual(surface.props["title"], "Книга цен", "the game's padded title is trimmed");
    const Json &town = surface.props["towns"][0];
    Expect(town["selected"] == true && surface.props["columns"][0]["label"] == "Городок",
           "before a choice the first town is the one shown and named over the prices");
    Expect(town["cells"][1]["text"] == "01.10.1654" && town["cells"][1]["sub"] == "13:00", "the date in the column, the hour under it");
    Expect(surface.props["prices"].size() == 2 && surface.props["prices"][1]["cells"][3] == "374", "the shown town's prices");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "back", "without an exit button ← is the global back");

    router.SetInterface(Load(models, "tradebook-unvisited.json"));
    Expect(router.Using() == "tradebook" && router.Rejection().empty() && surface.errors.empty(),
           "an unvisited trade book (both tables empty) is the trade book, not a rejection");
    Expect(surface.props["towns"].empty() && surface.props["prices"].empty() && surface.props["columns"][0]["label"] == "",
           "empty book: no towns, no prices (the list shows its empty text)");

    InterfaceModel stale = Load(models, "tradebook-unvisited.json");
    stale.widgets.erase(std::remove_if(stale.widgets.begin(), stale.widgets.end(), [](const Widget &w) { return w.type == "table"; }),
                        stale.widgets.end());
    stale.opening = 30;
    router.SetInterface(stale);
    surface.now += 1.0;
    router.Tick();
    ExpectEqual(router.Rejection(), "TABLE_CITY(choice|table)", "without the table at all the screen still refuses loudly");
}

// The same contract for the commerce screens: an empty chest is a chest, not a rejection.
static void EmptyChest(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel chest = Load(models, "itemsbox.json");
    Widget table;
    for (const Widget &row : chest.widgets)
        if (row.node == "TABLE_LIST")
        {
            table = row;
            break;
        }
    chest.widgets.erase(std::remove_if(chest.widgets.begin(), chest.widgets.end(), [](const Widget &w) { return w.node == "TABLE_LIST"; }),
                        chest.widgets.end());
    table.type = "table";
    table.index = -1;
    table.label.clear();
    table.cells.clear();
    table.actions.clear();
    chest.widgets.push_back(table);
    router.SetInterface(chest);
    Expect(router.Using() == "itemsbox" && surface.errors.empty() && surface.props["extras"]["rows"].empty(),
           "an empty chest (TABLE_LIST as one table widget) opens the chest screen");
}

static void ColonyLoot(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "colonycapture-deal.json"));
    Expect(router.Using() == "colonycapture" && surface.tag == "s-trade", "colony loot uses the trade layout");
    Expect(surface.errors.empty() && surface.props["extras"]["rows"].empty(), "every node of the colony loot is known");
    ExpectEqual(surface.props["header"]["title"].is_string() ? surface.props["header"]["title"].get<std::string>() : surface.props["title"].get<std::string>(),
                "Город: Грабёж", "the game's caption is the title");

    // «Забрать самое ценное»: the hold is filled with what is worth most per unit of its space, one good after
    // another through the game's own amount window. Packs of 10; a pack weighs: sugar 1, planks 2, sailcloth 1,
    // coffee 1; the adapter's values of a pack: 100, 60, 150, 120. So: sailcloth (150 a cwt), coffee (120), …
    const double weights[] = {1, 2, 1, 1};
    const int costs[] = {100, 60, 150, 120};
    int revision = 100;
    // The town's stock by row, what the hold carries, and the row whose amount window is open (-1: none).
    const auto state = [&](std::vector<int> there, int load, int capacity, int open) {
        InterfaceModel model = Load(models, "colonycapture-deal.json");
        model.revision = static_cast<uint64_t>(++revision);
        Widget fact_template;
        for (Widget &widget : model.widgets)
        {
            if (widget.node == "TABLE_LIST")
            {
                fact_template = widget;
                widget.value = widget.index == open ? "1" : "0";
                for (Cell &cell : widget.cells)
                {
                    if (cell.column == 3)
                        cell.text = std::to_string(there[static_cast<size_t>(widget.index)]);
                    if (cell.column == 5)
                        cell.text = std::to_string(static_cast<int>(weights[widget.index]));
                }
            }
            if (widget.node == "CAPACITY")
                widget.label = "Трюм:\n" + std::to_string(load) + " / " + std::to_string(capacity);
        }
        if (open < 0)
            model.widgets.erase(std::remove_if(model.widgets.begin(), model.widgets.end(), [](const Widget &w) { return w.node.rfind("QTY_", 0) == 0; }),
                                model.widgets.end());
        for (int row = 0; row < 4; ++row)
        {
            Widget fact = fact_template;
            fact.node = "ANDROID_LOOT";
            fact.type = "fact";
            fact.index = row;
            fact.row_id = "tr" + std::to_string(row + 1);
            fact.value.clear();
            fact.actions.clear();
            fact.cells.clear();
            fact.context = {{"cost", std::to_string(costs[row])}};
            model.widgets.push_back(fact);
        }
        return model;
    };
    const std::string prefix = "widget:colonycapture\t";
    InterfaceModel town = state({60, 400, 150, 300}, 201, 240, -1);
    router.SetInterface(town);
    Expect(surface.props["primary"]["label"] == "<trade.take_best>" && surface.props["primary"]["sub"] == "39 <transfer.cwt>" &&
               surface.props["primary"]["disabled"] == false && surface.props["place"]["title"] == "Город" &&
               surface.props["place"]["facts"][0]["v"] == "39 <transfer.cwt>" && surface.errors.empty(),
           "town loot: one button that fills the 39 cwt of room: " + surface.props["primary"].dump());
    surface.commands.clear();
    router.OnEvent({"primary", "click", ""});
    // The plan in ONE action: all 150 of the sailcloth (150 a cwt, 15 cwt), then coffee (120 a cwt): 24 cwt of room are
    // 24 packs of its 30: 240 pieces; nothing of the sugar and the planks: the hold is full. Rows are the table's tr numbers.
    Expect(surface.commands.size() == 1 && surface.commands[0] == prefix + "ANDROID_LOOT\t0\ttake@" + std::to_string(town.opening) + ":3=150,4=240",
           "one script call takes the most valuable per unit of space, greedily, to a full hold: " + Json(surface.commands).dump());
    {
        // A good too heavy for the room that is left is passed over for a lighter one further down.
        FakeSurface tight_surface;
        Router tight(tight_surface);
        InterfaceModel model = state({60, 400, 0, 0}, 239, 240, -1); // 1 cwt of room: no planks (2 a pack), ten of sugar
        tight.SetInterface(model);
        tight.OnEvent({"primary", "click", ""});
        Expect(tight_surface.commands.size() == 1 && tight_surface.commands[0].rfind(":1=10") == tight_surface.commands[0].size() - 5 &&
                   tight_surface.props["primary"]["sub"] == "1 <transfer.cwt>",
               "1 cwt of room: a pack of sugar, no planks: " + Json(tight_surface.commands).dump());
    }
    router.SetInterface(state({60, 400, 0, 60}, 240, 240, -1));
    surface.commands.clear();
    router.OnEvent({"primary", "click", ""});
    Expect(surface.commands.empty() && surface.props["primary"]["disabled"] == true && surface.props["primary"]["reason"] == "<trade.no_room>",
           "a full hold: the button is off and says why: " + surface.props["primary"].dump());
    {
        // Without the adapter's values the button is off and the screen says so aloud.
        InterfaceModel bare = state({60, 400, 150, 300}, 201, 240, -1);
        bare.widgets.erase(std::remove_if(bare.widgets.begin(), bare.widgets.end(), [](const Widget &w) { return w.node == "ANDROID_LOOT"; }), bare.widgets.end());
        FakeSurface bare_surface;
        Router bare_router(bare_surface);
        bare_router.SetInterface(bare);
        Expect(bare_surface.props["primary"]["disabled"] == true && bare_surface.errors.size() == 1, "no values from the adapter: off, and an error");
    }
}

int main(int argc, char **argv)
{
    const std::string models = argc > 1 ? argv[1] : "tests/models";
    Card(models);
    Frame(models);
    FramePicture(models);
    Maps(models);
    Rest(models);
    Notices(models);
    TradeBook(models);
    TradeBookAsSent(models);
    EmptyChest(models);
    ColonyLoot(models);
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("rare screen checks passed\n");
    return failures ? 1 : 0;
}
