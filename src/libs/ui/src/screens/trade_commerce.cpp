// Commerce: store, contraband, cannons, items trade, storage, colony loot and chests / bodies — one
// buy / sell / transfer layout (pack screen <s-trade>). Goods are one C-TradeList that never
// moves: filter chips and sortable columns in its head, 44 dp rows; the row whose quantity
// window the game has open shows in the dock beside the list (object, description, C-Quantity
// with the amount on our side after the deal). While the player sets an amount nothing goes
// to the game: the amount, its limits (stock, money, hold), the total and the button's text are
// computed here from what the open row already says, with the game's own formulas (DealMath
// below mirrors store.c / cannons.c / contraband.c / itemstrade.c ChangeQTY_EDIT), so the
// control answers in the same frame. Only the confirmation goes out: the amount and the game's
// OK in one flush for a money deal («Купить / Продать N»), or as soon as the amount settles for
// a free transfer; the hand at a row's end takes the whole stack in one tap.
//
// All six interfaces share the game's nodes (TABLE_LIST, QTY_*); they differ in the
// columns of the table, described per interface below.
#include "controls.h"

#include <algorithm>
#include <cstdio>
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
    int details[3] = {-1, -1, -1}; // extra columns named in the dock (cannons: range, damage, reload)
    bool quick = false;            // the hand at a row's end takes the whole stack (free transfers by the game's amount window)
    // Our short captions of the game's category tabs, by position; a tab past them keeps the game's caption.
    const char *tabs[4] = {nullptr, nullptr, nullptr, nullptr};
    int weight = -1;               // what one pack (goods) or one piece (items) weighs; -1 = the deal has no weight
    bool pieces = false;           // itemstrade.c: single pieces with fractional weights (else goods in packs, whole cwt)
};

// The arithmetic of a money deal exactly as the game's scripts do it (ChangeQTY_EDIT of store.c, cannons.c,
// contraband.c for goods in packs; itemstrade.c for pieces), so that what the player sees while choosing is
// what the game will charge. makeint() truncates; all values here are non-negative.
struct DealMath
{
    bool pieces = false; // itemstrade.c
    int units = 1;       // pack size (Goods[].Units); 1 for pieces and cannons
    double weight = 0.0; // of one pack / one piece
    double load = 0.0, capacity = -1.0; // what we carry of what we can; capacity < 0 = unknown: no weight limit applied
    int money = -1;      // ours; < 0 = unknown: no money limit applied

    // goods.h GetGoodWeightByType: whole packs, rounded at 0.05; pieces: fWeight * qty.
    double Weight(int amount) const
    {
        if (pieces)
            return weight * amount;
        return static_cast<double>(static_cast<int>(((amount + units - 1) / units) * weight + 0.05));
    }
    // makeint(price * qty / iUnits + 0.5); pieces have no units.
    int Total(int amount, int price) const
    {
        return static_cast<int>(static_cast<double>(price) * amount / (pieces ? 1 : units) + 0.5);
    }
    // The most that can be bought of `stock`: the largest amount the game takes as it is. Its script lets a request
    // through while what we carry with it does not exceed the capacity and its total does not exceed the money
    // (past that it cuts the request down, and by one pack more than needed); both grow with the amount, so the
    // largest request that passes is found by halving.
    int BuyLimit(int stock, int price) const
    {
        const auto passes = [&](int amount) {
            if (capacity >= 0.0 && weight > 0.0 && load + Weight(amount) > capacity)
                return false;
            return !(money >= 0 && price > 0 && Total(amount, price) > money);
        };
        int low = 0, high = std::max(0, stock);
        while (low < high)
        {
            const int middle = low + (high - low + 1) / 2;
            if (passes(middle))
                low = middle;
            else
                high = middle - 1;
        }
        return low;
    }
};

constexpr Layout kStore{StringKey("trade.store"), 0, 5, 3, 2, 4, 6, "SHIPS_SCROLL", false, {-1, -1, -1}, false, {nullptr, nullptr, nullptr, nullptr}, 7};
constexpr Layout kContraband{StringKey("trade.contraband"), 0, 5, 3, 2, 4, 6, "SHIPS_SCROLL", false, {-1, -1, -1}, false, {nullptr, nullptr, nullptr, nullptr}, 7};
constexpr Layout kCannons{StringKey("trade.cannons"), 0, 5, 3, 2, 4, -1, "SHIPS_SCROLL", true, {7, 8, 9}, false, {nullptr, nullptr, nullptr, nullptr}, 6};
constexpr Layout kItemsTrade{StringKey("trade.items"), 0, 5, 3, 2, 4, -1, "CHARACTERS_SCROLL", true, {-1, -1, -1}, false, {nullptr, nullptr, nullptr, nullptr}, 6, true};
constexpr Layout kStorage{StringKey("trade.storage"), 0, 3, 2, -1, -1, 4, "SHIPS_SCROLL", false, {-1, -1, -1}, true};
// Looting a captured colony (ColonyCapture.c): the hold, the town's stores, a free transfer.
constexpr Layout kColonyCapture{StringKey("trade.loot"), 0, 3, 2, -1, -1, 4, "SHIPS_SCROLL", false, {-1, -1, -1}, true};
constexpr Layout kItemsBox{StringKey("trade.chest"), 0, 4, 2, -1, -1, -1, "CHARACTERS_SCROLL", true, {-1, -1, -1}, true,
                           {StringKey("common.all"), StringKey("trade.tab_mine"), StringKey("trade.tab_here"), StringKey("trade.tab_weapons")}};
// The goods of the loot / exchange screen (transfer_main): ours, weight, name, theirs, weight.
constexpr Layout kTransfer{StringKey("transfer.title"), 0, 3, 2, -1, -1, -1, "", false};

constexpr NodeRule kNodes[] = {
    {"TABLE_LIST", "choice|table", true},
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
        const std::vector<const Widget *> goods = admitted.Rows("TABLE_LIST");
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
        else if (other_caption.rfind(own_caption, 0) == 0) // "Предметов" / "Предметов торговца"
            own_caption = bindings.Text("trade.yours");

        // The open deal is the player's until it is confirmed: what we hold and what the other side has are
        // taken once, when the game opens the row's amount window (the pivot of the control and its two ends
        // never move after that), and the amount starts at the pivot. Nothing the game publishes while the
        // amount is being chosen moves it; a row whose stock the game changed (a transfer went through) starts anew.
        const int ours = selected ? Number(selected->CellText(columns.own)) : 0;
        const int theirs = selected ? Number(selected->CellText(columns.other)) : 0;
        if (!deal)
            deal_row_.clear();
        else if (selected->row_id != deal_row_ || ((ours != deal_ours_ || theirs != deal_theirs_) && !bindings.Waiting(*edit, "edit")))
        {
            deal_row_ = selected->row_id;
            deal_ours_ = ours;
            deal_theirs_ = theirs;
            target_ = ours;
        }
        const int buy_price = selected && money ? Number(selected->CellText(columns.buy)) : 0;
        const int sell_price = selected && money ? Number(selected->CellText(columns.sell)) : 0;
        const bool can_take = !selected || !money || selected->CellText(columns.buy) != "-";
        const bool can_give = !selected || !money || selected->CellText(columns.sell) != "-";
        // The scale's left end: everything we hold given away; what we hold is the pivot.
        const int low = can_give ? 0 : deal_ours_;
        const int high = can_take ? deal_ours_ + deal_theirs_ : deal_ours_;

        // The game's arithmetic for this row; the limits stop the knob where the game would.
        DealMath math;
        math.pieces = columns.pieces;
        if (selected)
        {
            if (columns.pack >= 0)
                math.units = std::max(1, Number(selected->CellText(columns.pack)));
            if (columns.weight >= 0)
                math.weight = std::atof(selected->CellText(columns.weight).c_str());
        }
        if (const Json carried = Figure(admitted.Label("CAPACITY")); carried.is_object())
        {
            const std::string value = carried["value"];
            if (const size_t slash = value.find('/'); slash != std::string::npos)
            {
                math.load = std::atof(value.c_str());
                math.capacity = std::atof(value.c_str() + slash + 1);
            }
        }
        const Widget *balance = admitted.One("OUR_GOLD");
        if (balance && balance->label.find_first_of("0123456789") != std::string::npos)
            math.money = Number(balance->label);
        const int most = money ? std::min(high, deal_ours_ + math.BuyLimit(deal_theirs_, buy_price)) : high;
        const int shown = deal ? std::clamp(target_, low, std::max(low, most)) : ours;
        const int amount = shown - deal_ours_; // > 0 we take (buy), < 0 we give (sell)
        const int total = money ? math.Total(std::abs(amount), amount > 0 ? buy_price : sell_price) : 0;
        const double weight = math.Weight(std::abs(amount));
        const auto weight_text = [&](double value) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), columns.pieces ? "%.1f" : "%.0f", value);
            return std::string(buffer);
        };

        // The quick take waits for the game to open the row's amount window, then takes all of it.
        if (!quick_row_.empty())
        {
            if (deal && selected->row_id == quick_row_ && edit->Offers("edit") && ok)
            {
                bindings.SendAfter(0.0, *edit, "edit", std::to_string(theirs), selected->row_id, std::string("0"));
                bindings.SendAfter(0.0, *ok, "press", {}, selected->row_id);
                quick_row_.clear();
            }
            else if (bindings.Now() - quick_at_ > 3.0)
            {
                bindings.Error(std::string(columns.title) + ": the game did not open the amount window of " + quick_row_ + "; nothing was taken");
                quick_row_.clear();
                take_all_ = false;
            }
            else
                bindings.RebuildAfter(quick_at_ + 3.0 - bindings.Now());
        }

        // «Взять всё» of a chest (the game has none there): the hand's quick take, one stack after
        // another, while no amount window is open. A stack still there after its take was cut
        // by the weight the hero can carry (the game's amount window caps it): the take stops.
        if (take_all_ && quick_row_.empty() && !deal)
        {
            const Widget *next = nullptr;
            for (const Widget *row : goods)
                if (Number(row->CellText(columns.other)) > 0 && row->Offers("activate"))
                {
                    next = row;
                    break;
                }
            if (!next)
                take_all_ = false;
            else if (next->row_id == take_all_row_)
            {
                take_all_ = false;
                bindings.Toast(bindings.Text("trade.too_heavy"));
            }
            else
            {
                take_all_row_ = next->row_id;
                quick_row_ = next->row_id;
                quick_at_ = bindings.Now();
                bindings.Send(*next, "activate", next->row_id);
            }
        }

        // The order of the rows: the game's, or by the column whose caption the player tapped.
        std::vector<size_t> order(goods.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        const int sort_columns[4] = {columns.own, columns.other, columns.buy, columns.sell};
        if (sort_ >= 0 && sort_ < 4 && sort_columns[sort_] >= 0)
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                const int left = Number(goods[a]->CellText(sort_columns[sort_])), right = Number(goods[b]->CellText(sort_columns[sort_]));
                return sort_descending_ ? left > right : left < right;
            });

        Json rows = Json::array();
        for (const size_t i : order)
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
            // Beside the name: the trade type only (a 44 dp row keeps the name whole); the dock's second
            // line adds the pack and the details (cannons: range, damage, reload).
            const std::string meta = type.word ? bindings.Text(type.word) : std::string();
            std::string sub = meta;
            const std::string pack = columns.pack >= 0 ? row->CellText(columns.pack) : std::string();
            if (!pack.empty() && pack != "1")
                sub += (sub.empty() ? "" : " · ") + header(columns.pack) + " " + pack;
            for (const int detail : columns.details)
                if (detail >= 0 && !row->CellText(detail).empty())
                    sub += (sub.empty() ? "" : " · ") + header(detail) + " " + row->CellText(detail);
            Json cells = Json::array({row->CellText(columns.own), row->CellText(columns.other)});
            if (money)
            {
                cells.push_back(Json{{"text", row->CellText(columns.buy)}, {"bold", true}});
                cells.push_back(Json{{"text", row->CellText(columns.sell)}, {"bold", true}});
            }
            Json view{{"id", std::to_string(i)},
                      {"name", row->CellText(columns.name)},
                      {"image", PictureOf(name && !name->images.empty() ? &name->images.front() : row->FirstImage())},
                      {"meta", meta},
                      {"sub", sub},
                      {"quick", columns.quick && Number(row->CellText(columns.other)) > 0 && row->Offers("activate")},
                      {"metaTone", tone},
                      {"cells", cells},
                      {"selected", row == selected},
                      {"disabled", !row->Offers("activate") && !row->Offers("select")},
                      {"reason", row->unavailable_reason}};
            if (row == selected && deal)
            {
                // One slider for both directions: what we hold is the origin, the zone to its
                // left gives (sells), the zone to its right takes (buys), each in its colour.
                Json quantity{{"value", shown},
                              {"min", low},
                              // The right end is the most this deal can take (stock, hold, purse): fixed while the
                              // deal is open, so the whole rail is usable and the pivot stays where it is.
                              {"max", std::max(low, most)},
                              {"zones", true},
                              {"origin", deal_ours_},
                              {"lessLabel", bindings.Text(money ? StringKey("trade.sell") : StringKey("trade.give"))},
                              {"moreLabel", bindings.Text(money ? StringKey("trade.buy") : StringKey("trade.take"))},
                              {"label", own_caption},
                              {"allLabel", bindings.Text("common.all")},
                              {"summary", "-"}, // a money deal's price is on its button (one place)
                              {"disabled", !edit->Offers("edit")}};
                if (columns.small)
                    quantity["steps"] = Json::array({1, 10});
                view["q"] = std::move(quantity);
                view["text"] = admitted.Label("QTY_GOODS_INFO");
            }
            rows.push_back(std::move(view));
        }

        // After a free transfer the row stays open, as the quantity control was: the game
        // closed its window, so it is opened again once.
        if (!deal && !reopen_row_.empty())
        {
            for (const Widget *row : goods)
                if (row->row_id == reopen_row_ && row->Offers("activate"))
                    bindings.Send(*row, "activate", row->row_id);
            reopen_row_.clear();
        }

        bindings.On("goods", "sort", [this, &bindings](const Event &event) {
            // A caption's taps: largest first, smallest first, the game's order again.
            const int column = std::atoi(event.value.c_str());
            if (sort_ != column)
                sort_ = column, sort_descending_ = true;
            else if (sort_descending_)
                sort_descending_ = false;
            else
                sort_ = -1;
            bindings.Rebuild();
        });
        if (columns.quick)
            bindings.On("goods", "quick", [this, &bindings, goods, selected, deal, cancel, edit, ok, theirs](const Event &event) {
                const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
                if (index >= goods.size())
                    return;
                const Widget *row = goods[index];
                reopen_row_.clear();
                if (deal && row == selected)
                {
                    if (!edit->Offers("edit") || !ok)
                        return;
                    bindings.SendAfter(0.0, *edit, "edit", std::to_string(theirs), row->row_id, std::string("0"));
                    bindings.SendAfter(0.0, *ok, "press", {}, row->row_id);
                    return;
                }
                if (deal && cancel && cancel->Offers("press"))
                    bindings.Send(*cancel, "press", {}, selected->row_id);
                if (!row->Offers("activate"))
                    return;
                quick_row_ = row->row_id;
                quick_at_ = bindings.Now();
                bindings.Send(*row, "activate", row->row_id);
            });
        bindings.On("goods", "row", [this, &bindings, goods, selected, deal, cancel](const Event &event) {
            const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
            if (index >= goods.size())
                return;
            const Widget *row = goods[index];
            if (deal && cancel && cancel->Offers("press"))
                bindings.Send(*cancel, "press", {}, selected->row_id);
            reopen_row_.clear();
            quick_row_.clear();
            take_all_ = false; // the player's own step ends a running «Взять всё»
            if (deal && row == selected)
                return; // a tap on the open row closes it
            if (row->Offers("activate"))
                bindings.Send(*row, "activate", row->row_id);
            else if (row->Offers("select"))
                bindings.Send(*row, "select", row->row_id);
        });
        if (deal)
        {
            // The amount is the player's: every sample of a drag and every step only moves it here (the next
            // frame shows it, its total and its button). A free transfer has no button: it goes to the game
            // when a drag ends, or shortly after the last step.
            const auto set = [this, &bindings, edit, ok, selected, money, low, most](const Event &event) {
                target_ = std::clamp(std::atoi(event.value.c_str()), low, std::max(low, most));
                bindings.Rebuild();
                if (money)
                    return;
                // The game's edit box holds the amount to take; a negative number is the amount to give. Once
                // the game has turned a negative amount into giving it reads every later number as an amount to
                // give; zero always leaves giving, so it goes right before the signed amount, in the same flush.
                const int delta = target_ - deal_ours_;
                const double wait = event.verb == "commit" ? 0.0 : 0.4;
                bindings.SendAfter(wait, *edit, "edit", std::to_string(delta), selected->row_id, std::string("0"));
                if (ok && delta != 0)
                {
                    bindings.SendAfter(wait, *ok, "press", {}, selected->row_id);
                    reopen_row_ = selected->row_id; // the game closes its window on a transfer
                }
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

        // Header figures. While a deal is being set they show what it changes, computed here like the deal.
        Json stats = Json::array();
        for (const char *node : {"CAPACITY", "STORE_CAPACITY"})
        {
            Json figure = Figure(admitted.Label(node));
            if (figure.is_null() || stats.size() == 2)
                continue;
            // (the preview is there from the moment the deal opens, so the header does not reflow at the first step)
            if (std::string(node) == "CAPACITY" && deal && money && math.capacity >= 0.0 && math.weight > 0.0)
            {
                const std::string now = figure["value"];
                figure["value"] = now.substr(0, now.find(" /"));
                figure["preview"] = weight_text(math.load + (amount > 0 ? weight : -weight));
                figure["previewTone"] = "neutral";
            }
            stats.push_back(std::move(figure));
        }
        // The storage writes its load as a sentence ("Общая загруженность склада : 0 / 50000 ц.")
        // and names the place in STORE_CAPACITY ("Склад").
        if (Json load = Figure(admitted.Label("STORAGE_INFO")); !load.is_null() && stats.size() < 2)
        {
            const std::string place = OneLine(admitted.Label("STORE_CAPACITY"));
            if (!place.empty() && Figure(place).is_null())
                load["label"] = place;
            stats.push_back(std::move(load));
        }
        const std::string gold = balance ? balance->label : std::string();
        if (const size_t digit = gold.find_first_of("0123456789"); digit != std::string::npos)
        {
            const size_t end = gold.find_first_not_of("0123456789 ", digit);
            std::string purse = gold.substr(digit, end == std::string::npos ? std::string::npos : end - digit);
            while (!purse.empty() && purse.back() == ' ')
                purse.pop_back();
            // Some merchants trade for doubloons; the engine marks the balance then.
            const auto currency = balance->context.find("currency");
            Json figure = currency != balance->context.end() && currency->second == "doubloons"
                              ? Json{{"label", bindings.Text("trade.doubloons")}, {"value", purse}, {"tone", "gold"}}
                              : Json{{"icon", "money"}, {"value", purse}, {"tone", "gold"}};
            if (deal && money)
                figure["preview"] = Spaced(Number(gold) + (amount > 0 ? -total : total));
            if (stats.size() == 2)
                stats.erase(1);
            stats.push_back(std::move(figure));
        }
        view["stats"] = stats;

        // The captions of a money deal's prices name the operation («Купить», «Продать»): the column is
        // what one pack costs in that direction.
        std::vector<std::string> captions{own_caption, other_caption};
        if (money)
        {
            captions.push_back(bindings.Text("trade.buy"));
            captions.push_back(bindings.Text("trade.sell"));
        }
        Json cols = Json::array();
        for (size_t i = 0; i < captions.size(); ++i)
            cols.push_back(Json{{"label", captions[i]}, {"sortable", true},
                                {"sort", sort_ == static_cast<int>(i) ? (sort_descending_ ? "desc" : "asc") : ""}});
        view["list"] = Json{{"label", header(columns.name)}, {"cols", cols}, {"rows", rows}, {"quick", columns.quick ? "hand" : ""}};

        // The game's own category tabs (chests) replace our filters.
        Json tabs = Json::array();
        std::vector<const Widget *> tab_buttons;
        for (int i = 1; i <= 8; ++i)
        {
            const Widget *button = admitted.One("TABBTN_" + std::to_string(i));
            const Widget *caption = admitted.One("TABSTR_" + std::to_string(i));
            if (!button || !caption || caption->label.empty())
                continue;
            // The script reads the click on the caption over the picture where it has one.
            const Widget *target = caption->Offers("press") ? caption : button;
            // Our short caption by position; a tab past the known ones (a mod's) keeps the game's.
            const char *known = i <= 4 ? columns.tabs[i - 1] : nullptr;
            tabs.push_back(Json{{"label", known ? bindings.Text(known) : caption->label}, {"selected", button->value == "1"}, {"disabled", !target->Offers("press")}});
            tab_buttons.push_back(target);
        }
        if (!tabs.empty())
        {
            view["list"]["filters"] = tabs;
            bindings.On("goods", "filter", [&bindings, tab_buttons](const Event &event) {
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
            view["list"]["filters"] = filters;
            bindings.On("goods", "filter", [this, &bindings](const Event &event) {
                filter_ = std::clamp(std::atoi(event.value.c_str()), 0, 2);
                bindings.Rebuild();
            });
        }

        // The description of the open deal is in the dock (a tap reads it whole); without a deal the
        // dock offers the place's own text.
        if (deal)
        {
            view["info"] = false;
            bindings.On("goods", "info", [&bindings, text = admitted.Label("QTY_GOODS_INFO"), title = admitted.Label("QTY_CAPTION")](const Event &) {
                bindings.Read(title, text);
            });
        }
        else
        {
            std::string info = admitted.Label("INFO_TEXT");
            if (!admitted.Label("STORAGE_INFO").empty())
                info += (info.empty() ? "" : "\n") + admitted.Label("STORAGE_INFO");
            view["info"] = !info.empty();
            bindings.On("info", "", [&bindings, info](const Event &) { bindings.Read(std::string(), info); });
        }
        view["infoLabel"] = bindings.Text("common.description");
        view["dealing"] = deal; // the dock is the deal's: the place's note waits

        // Bar buttons: the deal of a money trade, else what the place offers (take all, …).
        Json actions = Json::array();
        view["primary"] = Json();
        if (deal && ok && money && amount != 0)
        {
            // The button names the deal in our words with its price (and the weight it moves): the amount and
            // the game's OK go out together when it is pressed, nothing before.
            std::string sub = Spaced(total);
            if (math.weight > 0.0)
                sub += " \xC2\xB7 " + weight_text(weight) + (columns.pieces ? std::string() : " " + bindings.Text("transfer.cwt"));
            view["primary"] = Json{{"label", bindings.Text(amount > 0 ? StringKey("trade.buy") : StringKey("trade.sell")) + " " + std::to_string(std::abs(amount))},
                                   {"sub", sub},
                                   {"disabled", !ok->Offers("press") || !edit->Offers("edit")},
                                   {"reason", ok->unavailable_reason},
                                   {"busy", bindings.Pending("primary")}};
            bindings.On("primary", "", [&bindings, edit, ok, selected, amount](const Event &) {
                bindings.SendAfter(0.0, *edit, "edit", std::to_string(amount), selected->row_id, std::string("0"));
                bindings.SendAfter(0.0, *ok, "press", {}, selected->row_id);
            });
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
            else if (columns.quick && std::any_of(goods.begin(), goods.end(), [&columns](const Widget *row) {
                         return Number(row->CellText(columns.other)) > 0 && row->Offers("activate");
                     }))
            {
                // A chest: our «Взять всё», the hand on every stack in turn (see Build).
                view["primary"] = Json{{"label", bindings.Text("trade.take_all")}, {"sub", ""}, {"disabled", false}, {"busy", take_all_}};
                bindings.On("primary", "", [this, &bindings](const Event &) {
                    if (take_all_)
                        return;
                    take_all_ = true;
                    take_all_row_.clear();
                    bindings.Rebuild();
                });
            }
        }
        view["actions"] = actions;

        const Widget *exit = admitted.One("EXIT_BTN");
        bindings.On("header-back", "", [this, &bindings, exit, cancel, deal, selected](const Event &) {
            reopen_row_.clear();
            quick_row_.clear();
            take_all_ = false; // the player's own step ends a running «Взять всё»
            if (deal && cancel && cancel->Offers("press"))
                bindings.Send(*cancel, "press", {}, selected->row_id);
            else
                bindings.Back(exit);
        });
        return view;
    }

  private:
    const Layout &layout_;
    std::string reopen_row_;  // the row whose window the game closed after a free transfer
    int filter_ = 0; // 0 all, 1 on our side, 2 profitable (export goods) / on the other side
    int sort_ = -1;  // the column the rows are ordered by (own, other, buy, sell); -1 = the game's order
    bool sort_descending_ = true;
    std::string quick_row_; // the row whose whole stack is taken once the game opens its amount window
    bool take_all_ = false;    // «Взять всё» of a chest runs: the quick take of one stack after another
    std::string take_all_row_; // the stack it took last (still there next time: too heavy to carry)
    double quick_at_ = 0.0;
    // The open deal: its row, what both sides held when the game opened it (the control's pivot and ends)
    // and the amount on our side the player has set.
    std::string deal_row_;
    int deal_ours_ = 0, deal_theirs_ = 0;
    int target_ = 0;
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
// Used inside the loot / exchange screen (extra_transfer.cpp), which adds its windows.
std::unique_ptr<Screen> CreateTransferGoods()
{
    return std::make_unique<CommerceScreen>(kTransfer);
}
std::unique_ptr<Screen> CreateColonyCaptureScreen()
{
    return std::make_unique<CommerceScreen>(kColonyCapture);
}
std::unique_ptr<Screen> CreateItemsBoxScreen()
{
    return std::make_unique<CommerceScreen>(kItemsBox);
}

} // namespace storm::ui::screens
