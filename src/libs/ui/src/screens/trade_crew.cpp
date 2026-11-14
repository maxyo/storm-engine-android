// Crew hire (interface "hirecrew", mockup V2-Trade-CrewHire): the trade layout with one
// row, the crew, whose quantity is always open; under it the experience and morale of the
// ship's crew and of the men in the tavern, drawn as the ship screen draws a crew (bars with the
// game's words, hero::CrewQuality; the numbers are the adapter's ANDROID_CREWHIRE). Fills the
// pack screen <s-crew>.
//
// As in the trade screens the amount is the player's until it is confirmed: nothing goes to the game while
// it is chosen. The limits and the cost are hirecrew.c ChangeQTY_EDIT's: the men in the tavern, the ship's
// room for crew and the room of its hold (facts of the adapter android_hero.c, ANDROID_CREWHIRE), the purse
// (makeint(money / price)); the cost is makeint(price * men). «Нанять / Уволить N» sends the signed amount
// and the game's OK in one flush.
#include "controls.h"
#include "hero.h"

#include <algorithm>
#include <cstdlib>
#include <map>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"SHIPS_SCROLL", "choice", true},
    {"QTY_EDIT", "edit", true},
    {"ANDROID_CREWHIRE", "fact"}, // the adapter's limits of a hire
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

        // The crews as the game has them (it holds no deal of its own: the amount is ours until confirmed).
        const int crew = Number(admitted.Label("CREW_QTY"));
        const int tavern = Number(admitted.Label("CREW_QTY2"));
        if (owner != deal_owner_ || crew != deal_crew_ || tavern != deal_tavern_)
        {
            deal_owner_ = owner;
            deal_crew_ = crew;
            deal_tavern_ = tavern;
            target_ = crew; // a new ship, or a deal went through: the knob is on the pivot
        }
        // hirecrew.c ChangeQTY_EDIT, hiring: the tavern's men, the ship's room for crew, the purse, the hold's room.
        const int price = Number(admitted.Label("TAVERN_PRICE"));
        const std::string gold = admitted.Label("OUR_GOLD");
        const int money = gold.find_first_of("0123456789") == std::string::npos ? -1 : Number(gold.substr(gold.find_first_of("0123456789")));
        const Widget *limits = admitted.One("ANDROID_CREWHIRE");
        if (!limits && reported_opening_ != admitted.model->opening)
        {
            reported_opening_ = admitted.model->opening;
            bindings.Error("hirecrew: the interface adapter sent no ANDROID_CREWHIRE (the ship's crew and hold limits are unknown: the game may hire fewer)");
        }
        const auto fact = [limits](const char *key) {
            const auto found = limits ? limits->context.find(key) : std::map<std::string, std::string>::const_iterator();
            return limits && found != limits->context.end() ? std::atoi(found->second.c_str()) : -1;
        };
        int hire = tavern;
        std::string more_reason = tavern > 0 ? std::string() : bindings.Text("crew.nobody");
        const auto limit = [&](int most, const char *reason) {
            if (most >= 0 && most < hire)
            {
                hire = std::max(0, most);
                more_reason = bindings.Text(reason);
            }
        };
        limit(fact("max") < 0 ? -1 : fact("max") - crew, StringKey("crew.full"));
        if (price > 0 && money >= 0)
            limit(money / price, StringKey("trade.no_money"));
        limit(fact("space"), StringKey("trade.no_room")); // QtyMax: no room in the hold, nobody is hired
        const int low = 0, high = crew + hire;
        const int shown = std::clamp(target_, low, high);
        const int amount = shown - crew; // > 0 hire, < 0 fire
        const int cost = amount > 0 ? price * amount : 0;

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
                            {"moreReason", more_reason},
                            {"disabled", !edit->Offers("edit")}}}};
        const auto set = [this, &bindings, low, high, more_reason](const Event &event) {
            const int wanted = std::atoi(event.value.c_str());
            if (wanted > high && !more_reason.empty() && bindings.Now() - reason_at_ > 1.5)
            {
                reason_at_ = bindings.Now();
                bindings.Toast(more_reason);
            }
            target_ = std::clamp(wanted, low, high);
            bindings.Rebuild();
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
        if (const size_t digit = gold.find_first_of("0123456789"); digit != std::string::npos)
        {
            const size_t end = gold.find_first_not_of("0123456789 ", digit);
            std::string purse_text = gold.substr(digit, end == std::string::npos ? std::string::npos : end - digit);
            while (!purse_text.empty() && purse_text.back() == ' ')
                purse_text.pop_back();
            Json purse{{"icon", "money"}, {"value", purse_text}, {"tone", "gold"}};
            if (cost > 0)
                purse["preview"] = std::to_string(money - cost);
            stats.push_back(std::move(purse));
        }
        view["stats"] = stats;
        view["list"] = Json{{"label", ""},
                            {"cols", Json::array({Json{{"label", bindings.Text("crew.on_ship")}}, Json{{"label", bindings.Text("crew.in_tavern")}},
                                                  Json{{"label", bindings.Text("trade.price")}}})},
                            {"rows", Json::array({row})}};
        view["shipLabel"] = bindings.Text("crew.on_ship");
        view["tavernLabel"] = bindings.Text("crew.in_tavern");
        // The adapter's crew numbers (android_hero.c AndroidCrewHire): the ship's as the ship screen's, the tavern's prefixed "town_".
        if (limits && limits->context.find("morale") == limits->context.end() && reported_quality_ != admitted.model->opening)
        {
            reported_quality_ = admitted.model->opening;
            bindings.Error("hirecrew: the interface adapter's ANDROID_CREWHIRE has no crew quality (morale, experience): the bars show 0");
        }
        view["ship"] = hero::CrewQuality(admitted, bindings, "TABLE_CREW", "CREW_MORALE_TEXT", limits);
        view["tavern"] = hero::CrewQuality(admitted, bindings, "TABLE_CREW2", "CREW_MORALE_TEXT2", limits, "town_");
        std::string notes;
        for (const char *node : {"MONEY_SHIP", "FOOD_SHIP"})
            if (!admitted.Label(node).empty())
                notes += (notes.empty() ? "" : " · ") + OneLine(admitted.Label(node));
        view["notes"] = notes;

        view["primary"] = Json();
        if (ok && amount != 0)
        {
            view["primary"] = Json{{"label", bindings.Text(amount > 0 ? StringKey("crew.hire") : StringKey("crew.fire")) + " " + std::to_string(std::abs(amount))},
                                   {"sub", amount > 0 ? std::to_string(cost) : std::string()},
                                   {"disabled", !ok->Offers("press") || !edit->Offers("edit")},
                                   {"reason", ok->unavailable_reason},
                                   {"busy", bindings.Pending("primary")}};
            // A negative number fires; zero first leaves a previous «fire» of the game's window.
            bindings.On("primary", "", [&bindings, edit, ok, owner, amount](const Event &) {
                bindings.SendAfter(0.0, *edit, "edit", std::to_string(amount), owner, std::string("0"));
                bindings.SendAfter(0.0, *ok, "press", {}, owner);
            });
        }
        const Widget *exit = admitted.One("EXIT_BTN");
        bindings.On("header-back", "", [&bindings, exit](const Event &) { bindings.Back(exit); });
        return view;
    }

  private:
    // The open deal: the ship and the crews it started from (the control's pivot), the crew the player has set.
    std::string deal_owner_;
    int deal_crew_ = -1, deal_tavern_ = -1;
    int target_ = 0;
    double reason_at_ = -10.0;       // when the "cannot hire more" reason was last said
    uint64_t reported_opening_ = 0;  // the opening the missing adapter facts were reported for
    uint64_t reported_quality_ = 0;  // the opening the missing crew quality was reported for
};

} // namespace

std::unique_ptr<Screen> CreateCrewScreen()
{
    return std::make_unique<CrewScreen>();
}

} // namespace storm::ui::screens
