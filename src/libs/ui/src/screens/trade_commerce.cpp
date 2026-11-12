// Commerce: store, contraband, cannons, items trade, storage, colony loot and chests / bodies — one
// buy / sell / transfer layout (pack screen <s-trade>). Goods are one C-TradeList that never
// moves: filter chips and sortable columns in its head, 44 dp rows; the row whose quantity
// window the game has open shows in the dock beside the list (object, description, C-Quantity
// with the amount on our side after the deal). Money deals are confirmed by the dock's primary
// button with the price; free transfers apply as soon as the amount settles, and the hand at
// a row's end takes the whole stack in one tap.
//
// All six interfaces share the game's nodes (TABLE_LIST, QTY_*); they differ in the
// columns of the table, described per interface below.
#include "controls.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

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
};

constexpr Layout kStore{StringKey("trade.store"), 0, 5, 3, 2, 4, 6, "SHIPS_SCROLL"};
constexpr Layout kContraband{StringKey("trade.contraband"), 0, 5, 3, 2, 4, 6, "SHIPS_SCROLL"};
constexpr Layout kCannons{StringKey("trade.cannons"), 0, 5, 3, 2, 4, -1, "SHIPS_SCROLL", true, {7, 8, 9}};
constexpr Layout kItemsTrade{StringKey("trade.items"), 0, 5, 3, 2, 4, -1, "CHARACTERS_SCROLL", true};
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

        // The quantity control shows the amount on our side after the deal.
        const int ours = selected ? Number(selected->CellText(columns.own)) : 0;
        const int theirs = selected ? Number(selected->CellText(columns.other)) : 0;
        const int model_target = deal ? Number(admitted.Label("QTY_INFO_SHIP_QTY")) : ours;
        if (!deal || !selected || selected->row_id != limit_row_)
        {
            limit_row_ = deal && selected ? selected->row_id : std::string();
            limit_min_ = std::numeric_limits<int>::min();
            limit_max_ = std::numeric_limits<int>::max();
        }
        // The player's own amount stands until the game has answered it: while it waits to
        // go out and until a revision made after it arrives (at most 2 s), so that steps
        // count on from it and not from a revision the game made before it saw the request.
        const uint64_t revision = admitted.model->revision;
        const bool mine = deal && selected->row_id == local_row_;
        const bool waiting = mine && bindings.Waiting(*edit, "edit");
        if (waiting)
            asked_revision_ = revision;
        // A revision answers the player's amount only when it is whole. The amount goes out as
        // "0" and then the signed amount, in one flush; the game may publish the zero on its
        // own first (QTY_EDIT 0, the hold as before), and it publishes its clamped QTY_EDIT a
        // revision before the labels that follow from it (QTY_INFO_SHIP_QTY still the old
        // figure; seen on the device). Taking either as the answer learnt a false limit at
        // what we hold. Whole = the hold figure moved by exactly the edit box's amount.
        const int edit_amount = std::abs(Number(edit ? edit->value : std::string()));
        const bool zero_echo = mine && local_target_ != local_ours_ && edit_amount == 0 && model_target == ours;
        const bool whole = std::abs(model_target - ours) == edit_amount;
        const bool answered = mine && !waiting && !zero_echo && whole && revision > asked_revision_;
        const bool recent = mine && (waiting || (!answered && bindings.Now() - local_at_ < 2.0));
        const int target = recent ? local_target_ : model_target;
        if (recent && !waiting)
            bindings.RebuildAfter(local_at_ + 2.0 - bindings.Now());
        else if (answered && ours == local_ours_ && model_target != local_target_)
        {
            // The game answered with another amount: its limit in that direction (money,
            // hold space, the merchant's purse). The control stops there instead of jumping
            // out and back on every further tap.
            if (local_target_ > ours && model_target >= ours && model_target < local_target_)
                limit_max_ = model_target;
            if (local_target_ < ours && model_target <= ours && model_target > local_target_)
                limit_min_ = model_target;
            local_target_ = model_target;
        }

        // One amount for everything the deal shows: the quantity control (its number, knob and
        // buy / sell caption), the deal button and the header previews all derive from it. It
        // is the player's pending amount until the game has answered, then the game's.
        const bool can_take = !selected || !money || selected->CellText(columns.buy) != "-";
        const bool can_give = !selected || !money || selected->CellText(columns.sell) != "-";
        const int low = std::max(can_give ? 0 : ours, std::min(limit_min_, ours));
        const int high = std::min(can_take ? ours + theirs : ours, std::max(limit_max_, ours));
        const int shown = std::clamp(target, low, std::max(low, high));
        // The game's figures (price line, hold, balance) describe its own amount: they are shown
        // once it is the one the player set.
        const bool settled = shown == model_target;

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
                const std::string result = settled ? OneLine(admitted.Label("QTY_Result")) : std::string();
                // One slider for both directions: what we hold is the origin, the zone to its
                // left gives (sells), the zone to its right takes (buys), each in its colour.
                Json quantity{{"value", shown},
                              {"min", low},
                              {"max", high},
                              {"zones", true},
                              {"origin", ours},
                              {"lessLabel", bindings.Text(money ? StringKey("trade.sell") : StringKey("trade.give"))},
                              {"moreLabel", bindings.Text(money ? StringKey("trade.buy") : StringKey("trade.take"))},
                              {"label", own_caption},
                              {"allLabel", bindings.Text("common.all")},
                              // A money deal's price is on its button (one place); the control keeps the
                              // width for the amount. A free transfer's line stays.
                              {"summary", money || result.empty() ? "-" : result},
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
                local_row_.clear();
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
            const auto set = [this, &bindings, edit, ok, selected, ours, money, asked = revision](const Event &event) {
                const int wanted = std::atoi(event.value.c_str());
                const int delta = wanted - ours;
                local_row_ = selected->row_id;
                local_target_ = wanted;
                local_ours_ = ours;
                asked_revision_ = asked; // a revision after this one answers the request
                local_at_ = bindings.Now();
                // Once the game has turned a negative amount into giving it reads every
                // later number as an amount to give. Zero always leaves giving, so it goes
                // right before the signed amount, in the same flush: sent on its own it made
                // the game reset the deal and publish that in between (the amount jumped back
                // to what we hold while the player was still stepping).
                const std::string amount = std::to_string(delta);
                if (money)
                {
                    bindings.SendLatest(*edit, "edit", amount, selected->row_id, std::string("0"));
                    return;
                }
                // A free transfer applies at once: when a drag ends, or shortly after the
                // last step, the amount is entered and confirmed.
                const double wait = event.verb == "commit" ? 0.0 : 0.4;
                bindings.SendAfter(wait, *edit, "edit", amount, selected->row_id, std::string("0"));
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

        // Header figures. While a deal is being set they show what it changes: the game
        // already counts the deal into the hold figure, so the figure from before is kept;
        // the balance changes only on confirmation, so its outcome is computed from the
        // game's own result line.
        const int amount_now = settled ? std::abs(model_target - ours) : 0;
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
        // The storage writes its load as a sentence ("Общая загруженность склада : 0 / 50000 ц.")
        // and names the place in STORE_CAPACITY ("Склад").
        if (Json load = Figure(admitted.Label("STORAGE_INFO")); !load.is_null() && stats.size() < 2)
        {
            const std::string place = OneLine(admitted.Label("STORE_CAPACITY"));
            if (!place.empty() && Figure(place).is_null())
                load["label"] = place;
            stats.push_back(std::move(load));
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
        if (deal && ok && money)
        {
            // The button names the shown amount in our words; until the game holds that amount
            // it waits (a press would confirm the game's previous one).
            const std::string operation = bindings.Text(shown > ours ? StringKey("trade.buy") : StringKey("trade.sell"));
            view["primary"] = DealView(bindings, *ok, operation, std::abs(shown - ours), settled ? OneLine(admitted.Label("QTY_Result")) : std::string(), !settled);
            if (!view["primary"].is_null())
                bindings.On("primary", "", [&bindings, ok, selected, settled](const Event &) {
                    if (settled) // while the game does not hold the shown amount the button waits
                        bindings.Send(*ok, "press", {}, selected->row_id);
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
    std::string hold_before_; // the hold figure before the amount of the open deal was set
    std::string reopen_row_;  // the row whose window the game closed after a free transfer
    int filter_ = 0; // 0 all, 1 on our side, 2 profitable (export goods) / on the other side
    int sort_ = -1;  // the column the rows are ordered by (own, other, buy, sell); -1 = the game's order
    bool sort_descending_ = true;
    std::string quick_row_; // the row whose whole stack is taken once the game opens its amount window
    bool take_all_ = false;    // «Взять всё» of a chest runs: the quick take of one stack after another
    std::string take_all_row_; // the stack it took last (still there next time: too heavy to carry)
    double quick_at_ = 0.0;
    // The amount the player set last: shown until the game's own number catches up.
    std::string local_row_;
    int local_target_ = 0;
    int local_ours_ = 0; // what we held when it was set: a learnt limit is one of this deal only
    double local_at_ = 0.0;
    uint64_t asked_revision_ = 0; // the model revision the player's amount was asked against
    // The game's own limits for the open row, learnt from its answers.
    std::string limit_row_;
    int limit_min_ = std::numeric_limits<int>::min();
    int limit_max_ = std::numeric_limits<int>::max();
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
