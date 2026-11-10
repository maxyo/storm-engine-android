// Store (interface "store", mockup V2-Trade-Store): goods as one C-TradeList; the row
// whose quantity window the game has open expands into C-Quantity; the primary button
// confirms the deal. Fills the pack screen <s-store>.
//
// The game's table columns: 0 in the hold, 1 weight, 2 price to buy, 3 name (+ picture,
// the colour tells the trade type), 4 price to sell, 5 in the store, 6 pack, 7 pack weight.
#include "controls.h"

#include <algorithm>
#include <cstdlib>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"TABLE_LIST", "choice", true},
    {"EXIT_BTN", "button"},
    {"STORECAPTION1", "text"},
    {"TITLE", "heading|text"},
    {"INFO_TEXT", "text"},
    {"SHIPS_SCROLL", "choice"},
    {"LEFTSCROLLBUTTON", "button"},
    {"RIGHTSCROLLBUTTON", "button"},
    {"OUR_GOLD", "text"},
    {"CAPACITY", "text"},
    {"STORE_CAPACITY", "text"},
    {"FOOD_SHIP", "text"},
    {"QTY_EDIT", "edit"},
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

// Our icon for the goods the game names in its GOODS picture group.
const char *GoodsIcon(const std::string &picture)
{
    static const char *const map[][2] = {{"BALLS", "ammo"},       {"GRAPES", "grape"},   {"KNIPPELS", "chain"}, {"BOMBS", "bomb"},
                                         {"SAILCLOTH", "sailcloth"}, {"PLANKS", "planks"}, {"POWDER", "powder"}, {"FOOD", "food"},
                                         {"RUM", "rum"},          {"WEAPON", "sabre"},   {"MEDICAMENT", "medicine"}};
    const std::string name = Upper(picture);
    for (const auto &pair : map)
        if (name == pair[0])
            return pair[1];
    return "chest";
}

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

class StoreScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-store";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const std::vector<const Widget *> goods = admitted.All("TABLE_LIST");
        const Widget *ok = admitted.One("QTY_OK_BUTTON");
        const Widget *cancel = admitted.One("QTY_CANCEL_BUTTON");
        const Widget *edit = admitted.One("QTY_EDIT");
        const Widget *selected = nullptr;
        for (const Widget *row : goods)
            if (row->value == "1")
                selected = row;
        const bool deal = selected && edit && (ok || cancel); // the game's quantity window is open
        const Widget *first = goods.empty() ? nullptr : goods.front();
        const auto header = [first](size_t column) {
            return first && column < first->column_headers.size() ? OneLine(first->column_headers[column]) : std::string();
        };

        // The quantity control shows the amount in the hold after the deal.
        const int in_hold = selected ? Number(selected->CellText(0)) : 0;
        const int in_store = selected ? Number(selected->CellText(5)) : 0;
        const int model_target = deal ? Number(admitted.Label("QTY_INFO_SHIP_QTY")) : in_hold;
        const bool recent = deal && selected->row_id == local_row_ && bindings.Now() - local_at_ < 1.0;
        const int target = recent ? local_target_ : model_target;

        Json rows = Json::array();
        for (size_t i = 0; i < goods.size(); ++i)
        {
            const Widget *row = goods[i];
            const int hold = Number(row->CellText(0));
            if (filter_ == 1 && hold == 0)
                continue;
            const Cell *name = nullptr;
            for (const Cell &cell : row->cells)
                if (cell.column == 3)
                    name = &cell;
            const char *tone = name ? MetaTone(name->color_argb) : "";
            if (filter_ == 2 && std::string(tone) != "ok")
                continue;
            const ImageRef *picture = name && !name->images.empty() ? &name->images.front() : row->FirstImage();
            const std::string pack = row->CellText(6);
            Json view{{"id", std::to_string(i)},
                      {"name", row->CellText(3)},
                      {"image", {{"kind", "token"}, {"icon", GoodsIcon(picture ? picture->image : std::string())}, {"enamel", "parchment"}}},
                      {"meta", pack.empty() || pack == "1" ? std::string() : header(6) + " " + pack},
                      {"metaTone", tone},
                      {"cells", Json::array({row->CellText(0), row->CellText(5),
                                             Json{{"text", row->CellText(2)}, {"bold", true}, {"sub", header(4) + " " + row->CellText(4)}}})},
                      {"selected", row == selected},
                      {"disabled", !row->Offers("activate") && !row->Offers("select")},
                      {"reason", row->unavailable_reason}};
            if (row == selected && deal)
            {
                const bool can_buy = row->CellText(2) != "-", can_sell = row->CellText(4) != "-";
                const std::string result = OneLine(admitted.Label("QTY_Result"));
                view["q"] = Json{{"value", target},
                                 {"min", can_sell ? 0 : in_hold},
                                 {"max", can_buy ? in_hold + in_store : in_hold},
                                 {"label", header(0)},
                                 {"summary", result.empty() ? "-" : result},
                                 {"disabled", !edit->Offers("edit")}};
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
            // The game's edit box holds the amount to buy; a negative number (or any
            // number once it is selling) is the amount to sell. Zero leaves selling.
            const auto set = [this, &bindings, edit, selected, in_hold, model_target](const Event &event) {
                const int wanted = std::atoi(event.value.c_str());
                const int delta = wanted - in_hold;
                const bool selling = model_target < in_hold;
                local_row_ = selected->row_id;
                local_target_ = wanted;
                local_at_ = bindings.Now();
                if (delta > 0 && selling)
                    bindings.Send(*edit, "edit", "0", selected->row_id);
                bindings.SendLatest(*edit, "edit", std::to_string(delta < 0 && selling ? -delta : delta), selected->row_id);
            };
            // The quantity control sits inside the list and reports under its id.
            bindings.On("goods", "change", set);
            bindings.On("goods", "commit", set);
        }

        Json view = Json::object();
        const std::string caption = admitted.Label("STORECAPTION1");
        view["title"] = !admitted.Label("TITLE").empty() ? admitted.Label("TITLE") : caption.empty() ? "Магазин" : caption;

        // Whose hold: the ship selector.
        const std::vector<const Widget *> ships = admitted.All("SHIPS_SCROLL");
        if (ships.size() > 1)
        {
            size_t current = 0;
            for (size_t i = 0; i < ships.size(); ++i)
                if (ships[i]->value == "1")
                    current = i;
            view["subject"] = Json{{"label", ships[current]->label},
                                   {"sub", std::to_string(current + 1) + " из " + std::to_string(ships.size())},
                                   {"prev", current > 0 && !deal},
                                   {"next", current + 1 < ships.size() && !deal},
                                   {"list", false}};
            const auto choose = [&bindings, ships, deal](size_t index) {
                if (!deal && index < ships.size() && ships[index]->Offers("select"))
                    bindings.Send(*ships[index], "select", ships[index]->row_id);
            };
            bindings.On("header", "subject-prev", [choose, current](const Event &) { choose(current - 1); });
            bindings.On("header", "subject-next", [choose, current](const Event &) { choose(current + 1); });
        }
        else
            view["subject"] = Json();

        Json stats = Json::array();
        const std::string capacity = admitted.Label("CAPACITY"); // "Трюм:\n189 / 199"
        if (const size_t line = capacity.find('\n'); line != std::string::npos)
        {
            std::string label = capacity.substr(0, line);
            while (!label.empty() && (label.back() == ':' || label.back() == ' '))
                label.pop_back();
            stats.push_back(Json{{"label", label}, {"value", capacity.substr(line + 1)}});
        }
        const std::string gold = admitted.Label("OUR_GOLD");
        size_t digit = gold.find_first_of("0123456789");
        if (digit != std::string::npos)
        {
            size_t end = gold.find_first_not_of("0123456789 ", digit);
            std::string amount = gold.substr(digit, end == std::string::npos ? std::string::npos : end - digit);
            while (!amount.empty() && amount.back() == ' ')
                amount.pop_back();
            stats.push_back(Json{{"icon", "money"}, {"value", amount}, {"tone", "gold"}});
        }
        view["stats"] = stats;

        view["list"] = Json{{"label", header(3)},
                            {"cols", Json::array({Json{{"label", header(0)}, {"w", 88}}, Json{{"label", header(5)}, {"w", 104}},
                                                  Json{{"label", header(2)}, {"w", 104}}})},
                            {"rows", rows}};
        view["filters"] = Json::array({Json{{"label", "Все"}, {"selected", filter_ == 0}}, Json{{"label", header(0)}, {"selected", filter_ == 1}},
                                       Json{{"label", "Выгодно"}, {"selected", filter_ == 2}}});
        bindings.On("filters", "select", [this, &bindings](const Event &event) {
            filter_ = std::clamp(std::atoi(event.value.c_str()), 0, 2);
            bindings.Rebuild();
        });

        // The goods description of the open deal, else the store's own text.
        const std::string info = deal ? admitted.Label("QTY_GOODS_INFO") : admitted.Label("INFO_TEXT");
        view["info"] = !info.empty();
        const std::string info_title = deal ? admitted.Label("QTY_CAPTION") : std::string();
        bindings.On("info", "", [&bindings, info, info_title](const Event &) { bindings.Read(info_title, info); });

        if (deal && ok)
        {
            const std::string operation = admitted.Label("QTY_TypeOperation");
            const int amount = std::abs(model_target - in_hold);
            const bool ready = amount > 0 && ok->Offers("press");
            view["primary"] = Json{{"label", operation.empty() ? ok->label : operation + " " + std::to_string(amount)},
                                   {"disabled", !ready},
                                   {"reason", ok->unavailable_reason},
                                   {"busy", bindings.Pending("primary")}};
            bindings.On("primary", "", [&bindings, ok, selected](const Event &) { bindings.Send(*ok, "press", {}, selected->row_id); });
        }
        else
            view["primary"] = Json();

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
    int filter_ = 0; // 0 all, 1 in the hold, 2 profitable (export goods)
    // The amount the player set last: shown until the game's own number catches up.
    std::string local_row_;
    int local_target_ = 0;
    double local_at_ = 0.0;
};

} // namespace

std::unique_ptr<Screen> CreateStoreScreen()
{
    return std::make_unique<StoreScreen>();
}

} // namespace storm::ui::screens
