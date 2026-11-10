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
    const char *title;   // when the game sends no caption
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

constexpr Layout kStore{"Магазин", 0, 5, 3, 2, 4, 6, "SHIPS_SCROLL"};
constexpr Layout kContraband{"Контрабанда", 0, 5, 3, 2, 4, 6, "SHIPS_SCROLL"};
constexpr Layout kCannons{"Орудия", 0, 5, 3, 2, 4, -1, "SHIPS_SCROLL", true, {7, 8, 9}};
constexpr Layout kItemsTrade{"Торговля", 0, 5, 3, 2, 4, -1, "CHARACTERS_SCROLL", true};
constexpr Layout kStorage{"Склад", 0, 3, 2, -1, -1, 4, "SHIPS_SCROLL"};
constexpr Layout kItemsBox{"Предметы", 0, 4, 2, -1, -1, -1, "CHARACTERS_SCROLL", true};

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

std::string OneLine(std::string text)
{
    std::replace(text.begin(), text.end(), '\n', ' ');
    return text;
}

// The trade type as the game colours the goods name.
const char *MetaTone(uint32_t argb)
{
    switch (argb & 0xffffff)
    {
    case 0xc4ffc4:
        return "ok"; // export
    case 0xc4c4ff:
    case 0xffc462:
        return "gold"; // import, high demand
    case 0xffc4c4:
        return "danger"; // contraband
    default:
        return "";
    }
}

// "Трюм:\n189 / 199" → {label, value}; a one-line text is no figure.
Json Figure(const std::string &text)
{
    const size_t line = text.find('\n');
    if (line == std::string::npos)
        return Json();
    std::string label = text.substr(0, line);
    while (!label.empty() && (label.back() == ':' || label.back() == ' '))
        label.pop_back();
    return Json{{"label", label}, {"value", OneLine(text.substr(line + 1))}};
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

        // The quantity control shows the amount on our side after the deal.
        const int ours = selected ? Number(selected->CellText(columns.own)) : 0;
        const int theirs = selected ? Number(selected->CellText(columns.other)) : 0;
        const int model_target = deal ? Number(admitted.Label("QTY_INFO_SHIP_QTY")) : ours;
        const bool recent = deal && selected->row_id == local_row_ && bindings.Now() - local_at_ < 1.0;
        const int target = recent ? local_target_ : model_target;

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
            const char *tone = name ? MetaTone(name->color_argb) : "";
            if (filter_ == 2 && (money ? std::string(tone) != "ok" : Number(row->CellText(columns.other)) == 0))
                continue;
            std::string meta;
            const std::string pack = columns.pack >= 0 ? row->CellText(columns.pack) : std::string();
            if (!pack.empty() && pack != "1")
                meta = header(columns.pack) + " " + pack;
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
                              {"label", header(columns.own)},
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
            // The game's edit box holds the amount to take; a negative number (or any
            // number once it is giving) is the amount to give. Zero leaves giving.
            const auto set = [this, &bindings, edit, ok, selected, ours, model_target, money](const Event &event) {
                const int wanted = std::atoi(event.value.c_str());
                const int delta = wanted - ours;
                const bool giving = model_target < ours;
                local_row_ = selected->row_id;
                local_target_ = wanted;
                local_at_ = bindings.Now();
                if (delta > 0 && giving)
                    bindings.Send(*edit, "edit", "0", selected->row_id);
                const std::string amount = std::to_string(delta < 0 && giving ? -delta : delta);
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
        view["title"] = !admitted.Label("TITLE").empty() ? admitted.Label("TITLE") : caption.empty() ? columns.title : caption;

        // Whose side: the ship or character selector.
        const std::vector<const Widget *> owners = admitted.All(columns.owners);
        if (owners.size() > 1)
        {
            size_t current = 0;
            for (size_t i = 0; i < owners.size(); ++i)
                if (owners[i]->value == "1")
                    current = i;
            view["subject"] = Json{{"label", owners[current]->label},
                                   {"sub", std::to_string(current + 1) + " из " + std::to_string(owners.size())},
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

        Json stats = Json::array();
        for (const char *node : {"CAPACITY", "STORE_CAPACITY"})
            if (Json figure = Figure(admitted.Label(node)); !figure.is_null() && stats.size() < 2)
                stats.push_back(std::move(figure));
        const std::string gold = admitted.Label("OUR_GOLD");
        if (const size_t digit = gold.find_first_of("0123456789"); digit != std::string::npos)
        {
            const size_t end = gold.find_first_not_of("0123456789 ", digit);
            std::string amount = gold.substr(digit, end == std::string::npos ? std::string::npos : end - digit);
            while (!amount.empty() && amount.back() == ' ')
                amount.pop_back();
            if (stats.size() == 2)
                stats.erase(1);
            stats.push_back(Json{{"icon", "money"}, {"value", amount}, {"tone", "gold"}});
        }
        view["stats"] = stats;

        // Numeric columns are as wide as their caption needs (13 dp text), within limits.
        const auto width = [](const std::string &caption, int least) {
            return std::clamp(static_cast<int>(Length(caption)) * 8 + 12, least, 176);
        };
        Json cols = Json::array({Json{{"label", header(columns.own)}, {"w", width(header(columns.own), 72)}},
                                 Json{{"label", header(columns.other)}, {"w", width(header(columns.other), 88)}}});
        if (money)
            cols.push_back(Json{{"label", header(columns.buy)}, {"w", width(header(columns.sell) + " 0000", 104)}});
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
            Json filters = Json::array({Json{{"label", "Все"}, {"selected", filter_ == 0}}, Json{{"label", header(columns.own)}, {"selected", filter_ == 1}}});
            if (!columns.small)
                filters.push_back(Json{{"label", money ? "Выгодно" : header(columns.other)}, {"selected", filter_ == 2}});
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
