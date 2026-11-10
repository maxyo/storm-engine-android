// Commerce: store, contraband, cannons, items trade, storage and chests / bodies — one
// buy / sell / transfer layout (pack screen <s-trade>, mockups V2-Trade-*). Goods are one
// C-TradeList; the row whose quantity window the game has open expands into C-Quantity,
// which shows the amount on our side after the deal. Money deals are confirmed by the
// primary button with the price; free transfers apply as soon as the amount settles.
//
// All six interfaces share the game's nodes (TABLE_LIST, QTY_*); they differ in the
// columns of the table, described per interface below.
#include "controls.h"

#include <algorithm>
#include <cstdlib>

namespace storm::ui::screens
{

namespace
{

struct Layout
{
    const char *title;   // string key of the title when the game sends no caption
    int own;             // amount on our side (hold, character)
    int other;           // amount on the other side (store, storage, chest)
    int name;            // name and picture
    int buy = -1;        // what one pack costs us; -1 = no prices: a free transfer
    int sell = -1;       // what we get for one pack
    int pack = -1;       // pack size
    const char *owners;  // node of the "whose" selector
    bool small = false;  // single items: steps of 1 and 10
    int details[3] = {-1, -1, -1}; // extra columns shown under the name (cannons: range, damage, reload)
};

constexpr Layout kStore{StringKey("trade.store"), 0, 5, 3, 2, 4, 6, "SHIPS_SCROLL"};
constexpr Layout kContraband{StringKey("trade.contraband"), 0, 5, 3, 2, 4, 6, "SHIPS_SCROLL"};
constexpr Layout kCannons{StringKey("trade.cannons"), 0, 5, 3, 2, 4, -1, "SHIPS_SCROLL", true, {7, 8, 9}};
constexpr Layout kItemsTrade{StringKey("trade.items"), 0, 5, 3, 2, 4, -1, "CHARACTERS_SCROLL", true};
constexpr Layout kStorage{StringKey("trade.storage"), 0, 3, 2, -1, -1, 4, "SHIPS_SCROLL"};
constexpr Layout kItemsBox{StringKey("trade.chest"), 0, 4, 2, -1, -1, -1, "CHARACTERS_SCROLL", true};

constexpr NodeRule kNodes[] = {
    {"TABLE_LIST", "choice", true},
    {"EXIT_BTN", "button"},
    {"STORECAPTION1", "text"},
    {"TITLE", "heading|text"},
    {"INFO_TEXT", "text"},
    {"STORAGE_INFO", "text"},
    {"SHIPS_SCROLL", "choice"},
    {"CHARACTERS_SCROLL", "choice"},
    {"LEFTSCROLLBUTTON", "button"},
    {"RIGHTSCROLLBUTTON", "button"},
    {"OUR_GOLD", "text"},
    {"CAPACITY", "text"},
    {"STORE_CAPACITY", "text"},
    {"FOOD_SHIP", "text"},
    {"TABBTN_*", "image|button"},
    {"TABSTR_*", "text"},
    {"GETALL_BUTTON", "button"},
    {"DELBODY_BUTTON", "button"},
    {"SKULL_BUTTON", "button"},
    {"QTY_EDIT", "edit"},
    {"QTY_EDITBOX", "edit"},
    {"QTY_OK_BUTTON", "button"},
    {"QTY_CANCEL_BUTTON", "button"},
    {"QTY_ADD_BUTTON", "button"},
    {"QTY_REMOVE_BUTTON", "button"},
    {"QTY_ADD_ALL_BUTTON", "button"},
    {"QTY_REMOVE_ALL_BUTTON", "button"},
    {"QTY_*", "text|heading|image"},
    {"MAIN_CHARACTER_PICTURE", "image"},
    {"OTHER_PICTURE", "image"},
};

int Number(const std::string &text)
{
    std::string digits;
    for (const char c : text)
        if ((c >= '0' && c <= '9') || (c == '-' && digits.empty()))
            digits += c;
    return digits.empty() || digits == "-" ? 0 : std::atoi(digits.c_str());
}

// The first whole number in a text ("Деньги 500, вес 10 ц" → 500); 0 when there is none.
int FirstNumber(const std::string &text)
{
    const size_t first = text.find_first_of("0123456789");
    if (first == std::string::npos)
        return 0;
    const size_t end = text.find_first_not_of("0123456789", first);
    return std::atoi(text.substr(first, end == std::string::npos ? std::string::npos : end - first).c_str());
}

std::string OneLine(std::string text)
{
    std::replace(text.begin(), text.end(), '\n', ' ');
    return text;
}

// The trade type, which the game tells only by the colour of the goods name: the tone of
// the row's second line and the string key of its word.
struct TradeType
{
    const char *tone = "";
    const char *word = nullptr;
};
TradeType TradeTypeOf(uint32_t argb)
{
    switch (argb & 0xffffff)
    {
    case 0xc4ffc4:
        return {"ok", StringKey("trade.export")};
    case 0xc4c4ff:
        return {"gold", StringKey("trade.import")};
    case 0xffc462:
        return {"gold", StringKey("trade.demand")};
    case 0xffc4c4:
        return {"danger", StringKey("trade.contraband_goods")};
    default:
        return {};
    }
}

// 1500 → "1 500".
std::string Spaced(int value)
{
    std::string digits = std::to_string(std::abs(value)), out;
    for (size_t i = 0; i < digits.size(); ++i)
    {
        if (i && (digits.size() - i) % 3 == 0)
            out += ' ';
        out += digits[i];
    }
    return value < 0 ? "-" + out : out;
}

// "Трюм:\n189 / 199" or "Дедвейт: 189 / 700" → {label, value}; a text without a
// separated value ("Магазин") is no figure.
Json Figure(const std::string &text)
{
    size_t line = text.find('\n');
    if (line == std::string::npos)
    {
        line = text.find(':');
        if (line == std::string::npos || text.find_first_of("0123456789", line) == std::string::npos)
            return Json();
    }
    std::string label = text.substr(0, line);
    while (!label.empty() && (label.back() == ':' || label.back() == ' '))
        label.pop_back();
    const size_t value = text.find_first_not_of(' ', line + 1);
    return Json{{"label", label}, {"value", OneLine(text.substr(value == std::string::npos ? text.size() : value))}};
}

class CommerceScreen final : public Screen
{
  public:
    explicit CommerceScreen(const Layout &layout) : layout_(layout)
    {
    }
    const char *Component() const override
    {
        return "s-trade";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const Layout &columns = layout_;
        const bool money = columns.buy >= 0;
        const std::vector<const Widget *> goods = admitted.All("TABLE_LIST");
        const Widget *ok = admitted.One("QTY_OK_BUTTON");
        const Widget *cancel = admitted.One("QTY_CANCEL_BUTTON");
        const Widget *edit = admitted.One("QTY_EDIT") ? admitted.One("QTY_EDIT") : admitted.One("QTY_EDITBOX");
        const Widget *selected = nullptr;
        for (const Widget *row : goods)
            if (row->value == "1")
                selected = row;
        const bool deal = selected && edit && (ok || cancel); // the game's quantity window is open
        const Widget *first = goods.empty() ? nullptr : goods.front();
        const auto header = [first](int column) {
            return first && column >= 0 && static_cast<size_t>(column) < first->column_headers.size()
                       ? OneLine(first->column_headers[static_cast<size_t>(column)])
                       : std::string();
        };

        // The two amount columns: the game's captions, unless it names both sides alike.
        std::string own_caption = header(columns.own), other_caption = header(columns.other);
        if (own_caption.empty() || own_caption == other_caption)
        {
            own_caption = bindings.Text("trade.yours");
            other_caption = bindings.Text("trade.theirs");
        }

        // The quantity control shows the amount on our side after the deal.
        const int ours = selected ? Number(selected->CellText(columns.own)) : 0;
        const int theirs = selected ? Number(selected->CellText(columns.other)) : 0;
        const int model_target = deal ? Number(admitted.Label("QTY_INFO_SHIP_QTY")) : ours;
        const bool recent = deal && selected->row_id == local_row_ && bindings.Now() - local_at_ < 1.0;
        const int target = recent ? local_target_ : model_target;
        if (recent) // then the game's own amount, also when it answered with another one (a limit)
            bindings.RebuildAfter(local_at_ + 1.0 - bindings.Now());

        Json rows = Json::array();
        for (size_t i = 0; i < goods.size(); ++i)
        {
            const Widget *row = goods[i];
            const int own = Number(row->CellText(columns.own));
            if (filter_ == 1 && own == 0)
                continue;
            const Cell *name = nullptr;
            for (const Cell &cell : row->cells)
                if (cell.column == columns.name)
                    name = &cell;
            const TradeType type = name ? TradeTypeOf(name->color_argb) : TradeType();
            const char *tone = type.tone;
            if (filter_ == 2 && (money ? std::string(tone) != "ok" : Number(row->CellText(columns.other)) == 0))
                continue;
            std::string meta = type.word ? bindings.Text(type.word) : std::string();
            const std::string pack = columns.pack >= 0 ? row->CellText(columns.pack) : std::string();
            if (!pack.empty() && pack != "1")
                meta += (meta.empty() ? "" : " · ") + header(columns.pack) + " " + pack;
            for (const int detail : columns.details)
                if (detail >= 0 && !row->CellText(detail).empty())
                    meta += (meta.empty() ? "" : " · ") + header(detail) + " " + row->CellText(detail);
            Json cells = Json::array({row->CellText(columns.own), row->CellText(columns.other)});
            if (money)
                cells.push_back(Json{{"text", row->CellText(columns.buy)},
                                     {"bold", true},
                                     {"sub", header(columns.sell) + " " + row->CellText(columns.sell)}});
            Json view{{"id", std::to_string(i)},
                      {"name", row->CellText(columns.name)},
                      {"image", PictureOf(name && !name->images.empty() ? &name->images.front() : row->FirstImage())},
                      {"meta", meta},
                      {"metaTone", tone},
                      {"cells", cells},
                      {"selected", row == selected},
                      {"disabled", !row->Offers("activate") && !row->Offers("select")},
                      {"reason", row->unavailable_reason}};
            if (row == selected && deal)
            {
                const bool can_take = !money || row->CellText(columns.buy) != "-";
                const bool can_give = !money || row->CellText(columns.sell) != "-";
                const std::string result = OneLine(admitted.Label("QTY_Result"));
                Json quantity{{"value", target},
                              {"min", can_give ? 0 : ours},
                              {"max", can_take ? ours + theirs : ours},
                              {"label", own_caption},
                              {"allLabel", bindings.Text("common.all")},
                              {"summary", result.empty() ? "-" : result},
                              {"disabled", !edit->Offers("edit")}};
                if (columns.small)
                    quantity["steps"] = Json::array({1, 10});
                view["q"] = std::move(quantity);
            }
            rows.push_back(std::move(view));
        }

        bindings.On("goods", "row", [this, &bindings, goods, selected, deal, cancel](const Event &event) {
            const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
            if (index >= goods.size())
                return;
            const Widget *row = goods[index];
            if (deal && cancel && cancel->Offers("press"))
                bindings.Send(*cancel, "press", {}, selected->row_id);
            if (deal && row == selected)
                return; // a tap on the open row closes it
            local_row_.clear();
            if (row->Offers("activate"))
                bindings.Send(*row, "activate", row->row_id);
            else if (row->Offers("select"))
                bindings.Send(*row, "select", row->row_id);
        });
        if (deal)
        {
            // The game's edit box holds the amount to take; a negative number is the
            // amount to give.
            const auto set = [this, &bindings, edit, ok, selected, ours, money](const Event &event) {
                const int wanted = std::atoi(event.value.c_str());
                const int delta = wanted - ours;
                local_row_ = selected->row_id;
                local_target_ = wanted;
                local_at_ = bindings.Now();
                // Once the game has turned a negative amount into giving it reads every
                // later number as an amount to give, and the model may not show that yet
                // (a second step sent before the first one is answered). Zero always
                // leaves giving, so it goes first and the signed amount after it.
                bindings.Send(*edit, "edit", "0", selected->row_id);
                const std::string amount = std::to_string(delta);
                if (money)
                {
                    bindings.SendLatest(*edit, "edit", amount, selected->row_id);
                    return;
                }
                // A free transfer applies at once: when a drag ends, or shortly after the
                // last step, the amount is entered and confirmed.
                const double wait = event.verb == "commit" ? 0.0 : 0.4;
                bindings.SendAfter(wait, *edit, "edit", amount, selected->row_id);
                if (ok && delta != 0)
                    bindings.SendAfter(wait, *ok, "press", {}, selected->row_id);
            };
            // The quantity control sits inside the list and reports under its id.
            bindings.On("goods", "change", set);
            bindings.On("goods", "commit", set);
        }

        Json view = Json::object();
        const std::string caption = OneLine(admitted.Label("STORECAPTION1"));
        view["title"] = !admitted.Label("TITLE").empty() ? admitted.Label("TITLE") : caption.empty() ? bindings.Text(columns.title) : caption;

        // Whose side: the ship or character selector.
        const std::vector<const Widget *> owners = admitted.All(columns.owners);
        if (owners.size() > 1)
        {
            size_t current = 0;
            for (size_t i = 0; i < owners.size(); ++i)
                if (owners[i]->value == "1")
                    current = i;
            view["subject"] = Json{{"label", owners[current]->label},
                                   {"sub", std::to_string(current + 1) + " " + bindings.Text("common.of") + " " + std::to_string(owners.size())},
                                   {"prev", current > 0 && !deal},
                                   {"next", current + 1 < owners.size() && !deal},
                                   {"list", false}};
            const auto choose = [&bindings, owners, deal](size_t index) {
                if (!deal && index < owners.size() && owners[index]->Offers("select"))
                    bindings.Send(*owners[index], "select", owners[index]->row_id);
            };
            bindings.On("header", "subject-prev", [choose, current](const Event &) { choose(current - 1); });
            bindings.On("header", "subject-next", [choose, current](const Event &) { choose(current + 1); });
            bindings.Ignore("header", "subject-list");
        }
        else
            view["subject"] = Json();

        // Header figures. While a deal is being set they show what it changes: the game
        // already counts the deal into the hold figure, so the figure from before is kept;
        // the balance changes only on confirmation, so its outcome is computed from the
        // game's own result line.
        const int amount_now = std::abs(model_target - ours);
        Json stats = Json::array();
        for (const char *node : {"CAPACITY", "STORE_CAPACITY"})
        {
            Json figure = Figure(admitted.Label(node));
            if (figure.is_null() || stats.size() == 2)
                continue;
            if (std::string(node) == "CAPACITY")
            {
                const std::string now = figure["value"];
                if (amount_now == 0)
                    hold_before_ = now;
                else if (!hold_before_.empty() && hold_before_ != now)
                {
                    figure["value"] = hold_before_.substr(0, hold_before_.find(" /"));
                    figure["preview"] = now.substr(0, now.find(" /"));
                    figure["previewTone"] = "neutral";
                }
            }
            stats.push_back(std::move(figure));
        }
        const Widget *balance = admitted.One("OUR_GOLD");
        const std::string gold = balance ? balance->label : std::string();
        if (const size_t digit = gold.find_first_of("0123456789"); digit != std::string::npos)
        {
            const size_t end = gold.find_first_not_of("0123456789 ", digit);
            std::string amount = gold.substr(digit, end == std::string::npos ? std::string::npos : end - digit);
            while (!amount.empty() && amount.back() == ' ')
                amount.pop_back();
            // Some merchants trade for doubloons; the engine marks the balance then.
            const auto currency = balance->context.find("currency");
            Json figure = currency != balance->context.end() && currency->second == "doubloons"
                              ? Json{{"label", bindings.Text("trade.doubloons")}, {"value", amount}, {"tone", "gold"}}
                              : Json{{"icon", "money"}, {"value", amount}, {"tone", "gold"}};
            const std::string result = admitted.Label("QTY_Result");
            if (deal && money && amount_now > 0 && FirstNumber(result) > 0)
            {
                const int price = FirstNumber(result);
                figure["preview"] = Spaced(Number(amount) + (model_target > ours ? -price : price));
            }
            if (stats.size() == 2)
                stats.erase(1);
            stats.push_back(std::move(figure));
        }
        view["stats"] = stats;

        Json cols = Json::array({Json{{"label", own_caption}}, Json{{"label", other_caption}}});
        if (money)
            cols.push_back(Json{{"label", header(columns.buy)}});
        view["list"] = Json{{"label", header(columns.name)}, {"cols", cols}, {"rows", rows}};

        // The game's own category tabs (chests) replace our filters.
        Json tabs = Json::array();
        std::vector<const Widget *> tab_buttons;
        for (int i = 1; i <= 8; ++i)
        {
            const Widget *button = admitted.One("TABBTN_" + std::to_string(i));
            const std::string label = admitted.Label("TABSTR_" + std::to_string(i));
            if (!button || label.empty())
                continue;
            tabs.push_back(Json{{"label", label}, {"selected", button->value == "1"}, {"disabled", !button->Offers("press")}});
            tab_buttons.push_back(button);
        }
        if (!tabs.empty())
        {
            view["filters"] = tabs;
            bindings.On("filters", "select", [&bindings, tab_buttons](const Event &event) {
                const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
                if (index < tab_buttons.size() && tab_buttons[index]->Offers("press"))
                    bindings.Send(*tab_buttons[index], "press");
            });
        }
        else
        {
            Json filters = Json::array({Json{{"label", bindings.Text("common.all")}, {"selected", filter_ == 0}}, Json{{"label", own_caption}, {"selected", filter_ == 1}}});
            if (!columns.small)
                filters.push_back(Json{{"label", money ? bindings.Text("trade.profitable") : other_caption}, {"selected", filter_ == 2}});
            view["filters"] = filters;
            bindings.On("filters", "select", [this, &bindings](const Event &event) {
                filter_ = std::clamp(std::atoi(event.value.c_str()), 0, 2);
                bindings.Rebuild();
            });
        }

        // The description of the open deal, else the place's own text.
        std::string info = deal ? admitted.Label("QTY_GOODS_INFO") : admitted.Label("INFO_TEXT");
        if (!deal && !admitted.Label("STORAGE_INFO").empty())
            info += (info.empty() ? "" : "\n") + admitted.Label("STORAGE_INFO");
        view["info"] = !info.empty();
        view["infoLabel"] = bindings.Text("common.description");
        const std::string info_title = deal ? admitted.Label("QTY_CAPTION") : std::string();
        bindings.On("info", "", [&bindings, info, info_title](const Event &) { bindings.Read(info_title, info); });

        // Bar buttons: the deal of a money trade, else what the place offers (take all, …).
        Json actions = Json::array();
        view["primary"] = Json();
        if (deal && ok && money)
        {
            const std::string operation = admitted.Label("QTY_TypeOperation");
            const int amount = std::abs(model_target - ours);
            view["primary"] = Json{{"label", operation.empty() ? ok->label : operation + " " + std::to_string(amount)},
                                   {"sub", OneLine(admitted.Label("QTY_Result"))},
                                   {"disabled", amount == 0 || !ok->Offers("press")},
                                   {"reason", ok->unavailable_reason},
                                   {"busy", bindings.Pending("primary")}};
            bindings.On("primary", "", [&bindings, ok, selected](const Event &) { bindings.Send(*ok, "press", {}, selected->row_id); });
        }
        else if (!deal)
        {
            for (const char *node : {"DELBODY_BUTTON", "SKULL_BUTTON"})
                if (const Widget *button = admitted.One(node))
                    actions.push_back(ButtonView(bindings, *button, "action-" + std::to_string(actions.size()), "secondary"));
            if (const Widget *all = admitted.One("GETALL_BUTTON"))
            {
                view["primary"] = Json{{"label", all->label},
                                       {"sub", ""},
                                       {"disabled", !all->Offers("press")},
                                       {"reason", all->unavailable_reason},
                                       {"busy", bindings.Pending("primary")}};
                bindings.Forward("primary", *all, "press");
            }
        }
        view["actions"] = actions;

        const Widget *exit = admitted.One("EXIT_BTN");
        bindings.On("header-back", "", [&bindings, exit, cancel, deal, selected](const Event &) {
            if (deal && cancel && cancel->Offers("press"))
                bindings.Send(*cancel, "press", {}, selected->row_id);
            else
                bindings.Back(exit);
        });
        return view;
    }

  private:
    const Layout &layout_;
    std::string hold_before_; // the hold figure before the amount of the open deal was set
    int filter_ = 0; // 0 all, 1 on our side, 2 profitable (export goods) / on the other side
    // The amount the player set last: shown until the game's own number catches up.
    std::string local_row_;
    int local_target_ = 0;
    double local_at_ = 0.0;
};

} // namespace

std::unique_ptr<Screen> CreateStoreScreen()
{
    return std::make_unique<CommerceScreen>(kStore);
}
std::unique_ptr<Screen> CreateContrabandScreen()
{
    return std::make_unique<CommerceScreen>(kContraband);
}
std::unique_ptr<Screen> CreateCannonsScreen()
{
    return std::make_unique<CommerceScreen>(kCannons);
}
std::unique_ptr<Screen> CreateItemsTradeScreen()
{
    return std::make_unique<CommerceScreen>(kItemsTrade);
}
std::unique_ptr<Screen> CreateStorageScreen()
{
    return std::make_unique<CommerceScreen>(kStorage);
}
std::unique_ptr<Screen> CreateItemsBoxScreen()
{
    return std::make_unique<CommerceScreen>(kItemsBox);
}

} // namespace storm::ui::screens
