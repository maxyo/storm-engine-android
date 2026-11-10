// Crew hire (interface "hirecrew", mockup V2-Trade-CrewHire): the trade layout with one
// row, the crew, whose quantity is always open; under it the experience and morale of the
// ship's crew and of the men in the tavern. Fills the pack screen <s-crew>.
#include "controls.h"

#include <algorithm>
#include <cstdlib>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"SHIPS_SCROLL", "choice", true},
    {"QTY_EDIT", "edit", true},
    {"CREW_QTY", "text", true},
    {"CREW_QTY2", "text"},
    {"EXIT_BTN", "button"},
    {"TITLE", "heading|text"},
    {"STORECAPTION1", "text"},
    {"LEFTSCROLLBUTTON", "button"},
    {"RIGHTSCROLLBUTTON", "button"},
    {"OUR_GOLD", "text"},
    {"CAPACITY", "text"},
    {"STORE_CAPACITY", "text"},
    {"FOOD_SHIP", "text"},
    {"MONEY_SHIP", "text"},
    {"INFO_SHIP", "text"},
    {"TAVERN_PRICE", "text"},
    {"CREW_MORALE_TEXT", "text"},
    {"CREW_MORALE_TEXT2", "text"},
    {"TABLE_CREW", "choice|text"},
    {"TABLE_CREW2", "choice|text"},
    {"QTY_OK_BUTTON", "button"},
    {"QTY_CANCEL_BUTTON", "button"},
    {"QTY_ADD_BUTTON", "button"},
    {"QTY_REMOVE_BUTTON", "button"},
    {"QTY_ADD_ALL_BUTTON", "button"},
    {"QTY_REMOVE_ALL_BUTTON", "button"},
    {"QTY_*", "text"},
    {"CREW_PICTURE*", "image"},
    {"CREW_MORALE_PIC*", "image"},
    {"MAIN_CHARACTER_PICTURE", "image"},
    {"OTHER_PICTURE", "image"},
};

int Number(const std::string &text)
{
    std::string digits;
    for (const char c : text)
    {
        if (c >= '0' && c <= '9')
            digits += c;
        else if (!digits.empty() && c != ' ')
            break;
    }
    return digits.empty() ? 0 : std::atoi(digits.c_str());
}

std::string OneLine(std::string text)
{
    std::replace(text.begin(), text.end(), '\n', ' ');
    return text;
}

// Skill rows of a crew table (picture, name, word) as label / value pairs.
Json Facts(const Admission &admitted, const char *table, const char *morale)
{
    Json items = Json::array();
    const std::string text = admitted.Label(morale); // "Мораль: хорошая"
    if (const size_t colon = text.find(':'); colon != std::string::npos)
        items.push_back(Json{{"k", text.substr(0, colon)}, {"v", text.substr(text.find_first_not_of(' ', colon + 1))}});
    for (const Widget *row : admitted.All(table))
        if (!row->CellText(1).empty())
            items.push_back(Json{{"k", row->CellText(1)}, {"v", row->CellText(2)}});
    return items;
}

class CrewScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-crew";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const Widget *edit = admitted.One("QTY_EDIT");
        const Widget *ok = admitted.One("QTY_OK_BUTTON");
        const std::vector<const Widget *> ships = admitted.All("SHIPS_SCROLL");
        size_t current = 0;
        for (size_t i = 0; i < ships.size(); ++i)
            if (ships[i]->value == "1")
                current = i;
        const std::string owner = ships[current]->row_id; // the ship every quantity action is bound to

        const int amount = std::abs(std::atoi(edit->value.c_str()));
        // The game names the operation in its own words; only hiring has a cost line.
        const std::string operation = admitted.Label("QTY_TypeOperation");
        const std::string result = OneLine(admitted.Label("QTY_Result"));
        const bool firing = amount > 0 && !operation.empty() && result.empty();
        // The game moves the men of the open deal at once: its figures are those after the
        // deal. The row and the header show the crew before it, the control the outcome.
        const int moved = operation.empty() ? 0 : firing ? -amount : amount;
        const int model_target = Number(admitted.Label("CREW_QTY"));
        const int crew = model_target - moved;
        const int tavern = Number(admitted.Label("CREW_QTY2")) + moved;
        const bool recent = owner == local_owner_ && bindings.Now() - local_at_ < 1.0;
        const int target = recent ? local_target_ : model_target;
        if (recent) // then the game's own amount, also when it answered with another one (a limit)
            bindings.RebuildAfter(local_at_ + 1.0 - bindings.Now());

        Json row{{"id", "0"},
                 {"name", bindings.Text("crew.sailors")},
                 {"image", {{"kind", "icon"}, {"icon", "crew"}}},
                 {"meta", OneLine(admitted.Label("INFO_SHIP"))},
                 {"cells", Json::array({std::to_string(crew), std::to_string(tavern),
                                        Json{{"text", std::to_string(Number(admitted.Label("TAVERN_PRICE")))}, {"bold", true}}})},
                 {"selected", true},
                 {"q", Json{{"value", target},
                            {"min", 0},
                            {"max", crew + tavern},
                            {"label", bindings.Text("crew.on_ship")},
                            {"allLabel", bindings.Text("common.all")},
                            {"summary", result.empty() ? "-" : result},
                            {"steps", Json::array({1, 10})},
                            {"disabled", !edit->Offers("edit")}}}};
        const auto set = [this, &bindings, edit, owner, crew](const Event &event) {
            const int wanted = std::atoi(event.value.c_str());
            const int delta = wanted - crew;
            local_owner_ = owner;
            local_target_ = wanted;
            local_at_ = bindings.Now();
            // As in the trade screens: a negative number fires; once firing the game reads
            // every number as men to fire, so zero (which leaves firing) goes first.
            bindings.Send(*edit, "edit", "0", owner);
            bindings.SendLatest(*edit, "edit", std::to_string(delta), owner);
        };
        bindings.On("crew", "change", set);
        bindings.On("crew", "commit", set);
        bindings.Ignore("crew", "row");

        Json view = Json::object();
        const std::string caption = OneLine(admitted.Label("STORECAPTION1"));
        view["title"] = !admitted.Label("TITLE").empty() ? admitted.Label("TITLE") : caption.empty() ? bindings.Text("crew.title") : caption;
        view["subject"] = Json();
        if (ships.size() > 1)
        {
            view["subject"] = Json{{"label", ships[current]->label},
                                   {"sub", std::to_string(current + 1) + " " + bindings.Text("common.of") + " " + std::to_string(ships.size())},
                                   {"prev", current > 0},
                                   {"next", current + 1 < ships.size()},
                                   {"list", false}};
            const auto choose = [&bindings, ships](size_t index) {
                if (index < ships.size() && ships[index]->Offers("select"))
                    bindings.Send(*ships[index], "select", ships[index]->row_id);
            };
            bindings.On("header", "subject-prev", [choose, current](const Event &) { choose(current - 1); });
            bindings.On("header", "subject-next", [choose, current](const Event &) { choose(current + 1); });
            bindings.Ignore("header", "subject-list");
        }
        Json stats = Json::array();
        Json men{{"icon", "crew"}, {"value", std::to_string(crew)}};
        if (model_target != crew)
            men["preview"] = std::to_string(model_target);
        stats.push_back(std::move(men));
        const std::string gold = admitted.Label("OUR_GOLD");
        if (const size_t digit = gold.find_first_of("0123456789"); digit != std::string::npos)
        {
            const size_t end = gold.find_first_not_of("0123456789 ", digit);
            std::string money = gold.substr(digit, end == std::string::npos ? std::string::npos : end - digit);
            while (!money.empty() && money.back() == ' ')
                money.pop_back();
            stats.push_back(Json{{"icon", "money"}, {"value", money}, {"tone", "gold"}});
        }
        view["stats"] = stats;
        view["list"] = Json{{"label", ""},
                            {"cols", Json::array({Json{{"label", bindings.Text("crew.on_ship")}}, Json{{"label", bindings.Text("crew.in_tavern")}},
                                                  Json{{"label", bindings.Text("trade.price")}}})},
                            {"rows", Json::array({row})}};
        view["shipLabel"] = bindings.Text("crew.on_ship");
        view["tavernLabel"] = bindings.Text("crew.in_tavern");
        view["ship"] = Facts(admitted, "TABLE_CREW", "CREW_MORALE_TEXT");
        view["tavern"] = Facts(admitted, "TABLE_CREW2", "CREW_MORALE_TEXT2");
        std::string notes;
        for (const char *node : {"MONEY_SHIP", "FOOD_SHIP"})
            if (!admitted.Label(node).empty())
                notes += (notes.empty() ? "" : " · ") + OneLine(admitted.Label(node));
        view["notes"] = notes;

        view["primary"] = Json();
        if (ok)
        {
            view["primary"] = Json{{"label", operation.empty() ? ok->label : operation + " " + std::to_string(amount)},
                                   {"sub", result},
                                   {"disabled", amount == 0 || !ok->Offers("press")},
                                   {"reason", ok->unavailable_reason},
                                   {"busy", bindings.Pending("primary")}};
            bindings.On("primary", "", [&bindings, ok, owner](const Event &) { bindings.Send(*ok, "press", {}, owner); });
        }
        const Widget *exit = admitted.One("EXIT_BTN");
        bindings.On("header-back", "", [&bindings, exit](const Event &) { bindings.Back(exit); });
        return view;
    }

  private:
    // The amount the player set last: shown until the game's own number catches up.
    std::string local_owner_;
    int local_target_ = 0;
    double local_at_ = 0.0;
};

} // namespace

std::unique_ptr<Screen> CreateCrewScreen()
{
    return std::make_unique<CrewScreen>();
}

} // namespace storm::ui::screens
