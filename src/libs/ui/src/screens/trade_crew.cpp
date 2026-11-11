// Crew hire (interface "hirecrew", mockup V2-Trade-CrewHire): the trade layout with one
// row, the crew, whose quantity is always open; under it the experience and morale of the
// ship's crew and of the men in the tavern. Fills the pack screen <s-crew>.
#include "controls.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

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

        // The game moves the men of the open deal at once: CREW_QTY / CREW_QTY2 are the crews
        // after it, QTY_EDIT the men it moves (hire or fire, by QTY_TypeOperation). The crews
        // before the deal are noted while no deal is open, per ship. The game publishes its
        // edit box a revision before the labels that follow from it, so a figure is taken
        // from a revision only when it is whole (the ship's crew moved by exactly the edit).
        const int edit_amount = std::abs(std::atoi(edit->value.c_str()));
        const std::string operation = admitted.Label("QTY_TypeOperation");
        const std::string result = OneLine(admitted.Label("QTY_Result"));
        const int model_target = Number(admitted.Label("CREW_QTY"));
        const int model_tavern = Number(admitted.Label("CREW_QTY2"));
        if (owner != base_owner_)
        {
            base_owner_.clear();
            limit_min_ = std::numeric_limits<int>::min();
            limit_max_ = std::numeric_limits<int>::max();
        }
        if (edit_amount == 0 && operation.empty())
        {
            base_owner_ = owner;
            base_crew_ = model_target;
            base_tavern_ = model_tavern;
        }
        int crew = base_crew_, tavern = base_tavern_;
        if (base_owner_ != owner) // opened with a deal already set: the game's figures minus it
        {
            const bool firing = edit_amount > 0 && !operation.empty() && result.empty();
            const int moved = operation.empty() ? 0 : firing ? -edit_amount : edit_amount;
            crew = model_target - moved;
            tavern = model_tavern + moved;
        }

        // One amount for the control, its caption, the deal button and the header preview: the
        // player's pending amount until the game answers it, then the game's (as the store).
        const uint64_t revision = admitted.model->revision;
        const bool mine = owner == local_owner_;
        const bool waiting = mine && bindings.Waiting(*edit, "edit");
        if (waiting)
            asked_revision_ = revision;
        const bool zero_echo = mine && local_target_ != local_ours_ && edit_amount == 0 && model_target == crew;
        const bool whole = std::abs(model_target - crew) == edit_amount;
        const bool answered = mine && !waiting && !zero_echo && whole && revision > asked_revision_;
        const bool recent = mine && (waiting || (!answered && bindings.Now() - local_at_ < 2.0));
        const int target = recent ? local_target_ : model_target;
        if (recent && !waiting)
            bindings.RebuildAfter(local_at_ + 2.0 - bindings.Now());
        else if (answered && crew == local_ours_ && model_target != local_target_)
        {
            // The game's clamp (tavern, the ship's room, money): its limit in that direction.
            if (local_target_ > crew && model_target >= crew && model_target < local_target_)
                limit_max_ = model_target;
            if (local_target_ < crew && model_target <= crew && model_target > local_target_)
                limit_min_ = model_target;
            local_target_ = model_target;
        }
        const int low = std::max(0, std::min(limit_min_, crew));
        const int high = std::min(crew + tavern, std::max(limit_max_, crew));
        const int shown = std::clamp(target, low, std::max(low, high));
        const bool settled = shown == model_target;

        Json row{{"id", "0"},
                 {"name", bindings.Text("crew.sailors")},
                 {"image", {{"kind", "icon"}, {"icon", "crew"}}},
                 {"meta", OneLine(admitted.Label("INFO_SHIP"))},
                 {"cells", Json::array({std::to_string(crew), std::to_string(tavern),
                                        Json{{"text", std::to_string(Number(admitted.Label("TAVERN_PRICE")))}, {"bold", true}}})},
                 {"selected", true},
                 // One slider for both directions, as the store: the crew on the ship is the
                 // origin, firing to its left, hiring to its right; the cost is on the button.
                 {"q", Json{{"value", shown},
                            {"min", low},
                            {"max", high},
                            {"zones", true},
                            {"origin", crew},
                            {"lessLabel", bindings.Text("crew.fire")},
                            {"moreLabel", bindings.Text("crew.hire")},
                            {"label", bindings.Text("crew.on_ship")},
                            {"allLabel", bindings.Text("common.all")},
                            {"summary", "-"},
                            {"steps", Json::array({1, 10})},
                            {"disabled", !edit->Offers("edit")}}}};
        const auto set = [this, &bindings, edit, owner, crew, asked = revision](const Event &event) {
            const int wanted = std::atoi(event.value.c_str());
            const int delta = wanted - crew;
            local_owner_ = owner;
            local_target_ = wanted;
            local_ours_ = crew;
            asked_revision_ = asked; // a revision after this one answers the request
            local_at_ = bindings.Now();
            // As in the trade screens: a negative number fires; once firing the game reads
            // every number as men to fire, so zero (which leaves firing) goes right before
            // the amount, in the same flush (on its own it reset the deal in between).
            bindings.SendLatest(*edit, "edit", std::to_string(delta), owner, std::string("0"));
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
        if (shown != crew)
            men["preview"] = std::to_string(shown);
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
            // Named in our words from the shown amount; it waits until the game holds it.
            const std::string word = bindings.Text(shown > crew ? StringKey("crew.hire") : StringKey("crew.fire"));
            view["primary"] = DealView(bindings, *ok, word, std::abs(shown - crew), settled ? result : std::string(), !settled);
            if (!view["primary"].is_null())
                bindings.On("primary", "", [&bindings, ok, owner, settled](const Event &) {
                    if (settled) // a press would confirm the game's previous amount
                        bindings.Send(*ok, "press", {}, owner);
                });
        }
        const Widget *exit = admitted.One("EXIT_BTN");
        bindings.On("header-back", "", [&bindings, exit](const Event &) { bindings.Back(exit); });
        return view;
    }

  private:
    // The amount the player set last: shown until the game has answered it.
    std::string local_owner_;
    int local_target_ = 0;
    int local_ours_ = 0;          // the crew when it was set
    uint64_t asked_revision_ = 0; // a later revision answers it
    double local_at_ = 0.0;
    // The crews before the open deal, noted while none is open, for this ship.
    std::string base_owner_;
    int base_crew_ = 0, base_tavern_ = 0;
    int limit_min_ = std::numeric_limits<int>::min(), limit_max_ = std::numeric_limits<int>::max(); // the game's clamps
};

} // namespace

std::unique_ptr<Screen> CreateCrewScreen()
{
    return std::make_unique<CrewScreen>();
}

} // namespace storm::ui::screens
