// Trade book (interface "tradebook", mockup V2-Extra-TradeBook): the colonies whose prices
// the hero has seen, on the left (flag, town, island, date of the prices), and the price list
// of the chosen one on the right (goods, the trade type as a tag, sell, buy and stock). The
// header carries the hero sections. Fills the pack screen <s-tradebook>.
//
// TABLE_CITY columns: nation picture, town, island, date ("??.??.????" when unknown).
// TABLE_GOODS columns: goods picture and name, trade type picture (TRADE_TYPE ico_<type>),
// sell, buy, stock, pack, pack weight; the last two are not shown.
#include "controls.h"

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"TABLE_CITY", "choice", true},
    {"TABLE_GOODS", "choice"},
    {"EXIT_BTN", "button"},
    {"I_*", "button"},
    {"TITLE", "heading|text"},
    {"STR_BUTTONS", "text"},
};

// The game's trade type picture "ico_<type>" (store.h T_TYPE_*) as the tag of c-table.
const char *TagOf(const Cell *cell)
{
    if (!cell || cell->images.empty())
        return nullptr;
    const std::string &name = cell->images.front().image;
    if (name == "ico_3")
        return "export";
    if (name == "ico_4")
        return "import";
    if (name == "ico_7")
        return "contraband";
    if (name == "ico_8")
        return "demand";
    return nullptr;
}

const Cell *CellAt(const Widget &row, int column)
{
    for (const Cell &cell : row.cells)
        if (cell.column == column)
            return &cell;
    return nullptr;
}

class TradeBookScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-tradebook";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const std::vector<const Widget *> towns = admitted.All("TABLE_CITY");
        const std::vector<const Widget *> goods = admitted.All("TABLE_GOODS");

        Json cities = Json::array();
        std::string chosen_town;
        for (const Widget *town : towns)
        {
            const Cell *flag = CellAt(*town, 0);
            std::string date = town->CellText(3);
            const bool unknown = date.empty() || date.find('?') != std::string::npos;
            const bool selected = town->value == "1";
            if (selected)
                chosen_town = town->CellText(1);
            Json name{{"text", town->CellText(1)}, {"sub", town->CellText(2)}};
            const std::string nation = NationOf(flag && !flag->images.empty() ? &flag->images.front() : nullptr);
            if (!nation.empty())
                name.update(Json{{"icon", "nation-" + nation}, {"iconTone", "color"}, {"iconSize", 30}});
            cities.push_back(Json{{"cells", Json::array({name, Json{{"text", unknown ? bindings.Text("tradebook.no_data") : date},
                                                                    {"tone", unknown ? "dim" : ""}}})},
                                  {"selected", selected},
                                  {"disabled", !town->enabled}});
        }
        bindings.On("towns", "row", [&bindings, towns](const Event &event) {
            const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
            if (index < towns.size())
                TapChoice(bindings, *towns[index]);
        });

        Json prices = Json::array();
        for (const Widget *row : goods)
        {
            const Cell *name = CellAt(*row, 0);
            Json lead{{"text", row->CellText(0)}};
            if (name && !name->images.empty())
                lead["image"] = PictureOf(&name->images.front());
            if (const char *tag = TagOf(CellAt(*row, 1)))
                lead["tag"] = tag;
            prices.push_back(Json{{"cells", Json::array({lead, row->CellText(2), row->CellText(3), row->CellText(4)})}});
        }
        const Widget *first = goods.empty() ? nullptr : goods.front();
        auto header = [first](int column) {
            if (!first || column >= static_cast<int>(first->column_headers.size()))
                return std::string();
            return first->column_headers[column];
        };

        Json view = Json::object();
        view["title"] = admitted.Label("TITLE");
        view["towns"] = cities;
        view["townColumns"] = Json::array({Json{{"w", "1"}}, Json{{"w", "130px"}, {"align", "right"}}});
        view["columns"] = Json::array({Json{{"label", chosen_town}, {"w", "1"}},
                                       Json{{"label", header(2)}, {"w", "80px"}, {"align", "right"}},
                                       Json{{"label", header(3)}, {"w", "80px"}, {"align", "right"}},
                                       Json{{"label", header(4)}, {"w", "80px"}, {"align", "right"}}});
        view["prices"] = prices;

        // Hero sections in the header; the exit button is ←.
        std::vector<const Widget *> sections;
        for (const Widget *widget : admitted.known)
            if (widget->type == "button" && !SectionOf(*widget).empty())
                sections.push_back(widget);
        view["sections"] = sections.empty() ? "" : "tradebook";
        bindings.On("header", "section", [&bindings, sections](const Event &event) {
            for (const Widget *button : sections)
                if (SectionOf(*button) == event.value && button->Offers("press"))
                {
                    bindings.Send(*button, "press");
                    return;
                }
        });
        const Widget *exit = admitted.One("EXIT_BTN");
        bindings.On("header-back", "", [&bindings, exit](const Event &) { bindings.Back(exit); });
        return view;
    }
};

} // namespace

std::unique_ptr<Screen> CreateTradeBookScreen()
{
    return std::make_unique<TradeBookScreen>();
}

} // namespace storm::ui::screens
