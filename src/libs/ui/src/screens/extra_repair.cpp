// Self repair (interface "repair", mockup V2-Extra-Repair): hull, masts and sails as amount
// rows with the materials each takes, the ship selector in the header, the planks and
// sailcloth in stock and the repair button with its time. Fills the pack screen <s-repair>.
//
// The game moves an amount only by its arrows (hull and sails 1 % per press, masts all at
// once) and clamps them to what the materials allow, so a new amount is sent as presses
// once it settles; its texts carry the numbers ("Состояние: 61% Текущий предел
// восстановления: 74%", "Доступно всего: 40/40 шт. Необходимо для полного ремонта: 117 шт.").
#include "controls.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"REPAIR_QTY_H", "text", true},
    {"REPAIR_LEFT_H", "button", true},
    {"REPAIR_RIGHT_H", "button", true},
    {"REPAIR_OK", "button", true},
    {"REPAIR_QTY_M", "text"},
    {"REPAIR_QTY_S", "text"},
    {"REPAIR_LEFT_M", "button"},
    {"REPAIR_RIGHT_M", "button"},
    {"REPAIR_LEFT_S", "button"},
    {"REPAIR_RIGHT_S", "button"},
    {"REPAIR_STATUS_*", "text"},
    {"REPAIR_QTY_*", "text"},
    {"REPAIR_HULL_CAPTION", "text"},
    {"REPAIR_MAST_CAPTION", "text"},
    {"REPAIR_SAIL_CAPTION", "text"},
    {"REPAIR_WINDOW_CAPTION", "text|heading"},
    {"REPAIR_SHIP_NAME", "text"},
    {"REPAIR_ALL_SHIP", "button"},
    {"REPAIR_ALL_SQUADRON", "button"},
    {"REPAIR_CANCEL", "button"},
    {"EXIT_BTN", "button"},
    {"SHIPS_SCROLL", "choice"},
    {"LEFTSCROLLBUTTON", "button"},
    {"RIGHTSCROLLBUTTON", "button"},
    {"NAME_STR", "text"},
    {"LOCATION_STR", "text"},
    {"TIME_STR", "text"},
    {"REPAIR_*", "image"},
    {"MAIN_CHARACTER_*", "image"},
    {"OTHER_*", "image"},
};

// Every whole number of a text, in order: "4/4 … 3/4" → {4, 4, 3, 4}.
std::vector<int> Numbers(const std::string &text)
{
    std::vector<int> out;
    for (size_t i = 0; i < text.size();)
    {
        if (text[i] >= '0' && text[i] <= '9')
        {
            const size_t end = text.find_first_not_of("0123456789", i);
            out.push_back(std::atoi(text.substr(i, end == std::string::npos ? std::string::npos : end - i).c_str()));
            i = end == std::string::npos ? text.size() : end;
        }
        else
            ++i;
    }
    return out;
}
int At(const std::vector<int> &numbers, size_t index)
{
    return index < numbers.size() ? numbers[index] : -1;
}

// One part of the ship as the game describes it.
struct Part
{
    char kind;          // H hull, M masts, S sails: suffix of the game's nodes
    const char *id;     // element id in the view
    const char *icon;
    bool masts;         // counted in masts ("2/3"), else in per cent
    const char *material, *missing; // what it takes, and the word when there is none
};
constexpr Part kParts[] = {
    {'H', "hull", "hull", false, StringKey("repair.planks"), StringKey("repair.no_planks")},
    {'M', "masts", "sail-furled", true, StringKey("repair.planks"), StringKey("repair.no_planks")},
    {'S', "sails", "sail-full", false, StringKey("repair.sailcloth"), StringKey("repair.no_sailcloth")},
};
constexpr const char *kStatus[] = {"REPAIR_STATUS_HULL_STR", "REPAIR_STATUS_MAST_STR", "REPAIR_STATUS_SAIL_STR"};
constexpr const char *kStock[] = {"REPAIR_QTY_HULL_STR", "REPAIR_QTY_MAST_STR", "REPAIR_QTY_SAIL_STR"};
constexpr const char *kCaption[] = {"REPAIR_HULL_CAPTION", "REPAIR_MAST_CAPTION", "REPAIR_SAIL_CAPTION"};

std::string Trim(const std::string &text)
{
    const size_t first = text.find_first_not_of(" \r\n\t");
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \r\n\t") - first + 1);
}

class RepairScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-repair";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        Json view = Json::object();
        view["title"] = bindings.Text("repair.title");

        Json parts = Json::array();
        int planks_total = -1, sailcloth_total = -1, planks_planned = 0;
        for (size_t p = 0; p < std::size(kParts); ++p)
        {
            const Part &part = kParts[p];
            const std::string suffix(1, part.kind);
            const Widget *less = admitted.One("REPAIR_LEFT_" + suffix);
            const Widget *more = admitted.One("REPAIR_RIGHT_" + suffix);
            const std::vector<int> amount = Numbers(admitted.Label("REPAIR_QTY_" + suffix)); // "61%" | "3/4"
            const std::vector<int> status = Numbers(admitted.Label(kStatus[p]));             // now, limit (masts: now/all, limit/all)
            const std::vector<int> stock = Numbers(admitted.Label(kStock[p]));               // left/total, full repair needs
            if (amount.empty() || status.size() < 2 || stock.size() < 3)
            {
                if (part.kind == 'H')
                    bindings.Error("repair: unreadable hull texts");
                continue;
            }
            const int full = part.masts ? At(amount, 1) : 100;
            const int now = At(status, 0);
            const int shown = At(amount, 0);
            // The game's "limit" is how far the materials still left reach from the part's
            // state, i.e. on top of the amount already set.
            const int top = std::min(full, shown + std::max(0, (part.masts ? At(status, 2) : At(status, 1)) - now));
            State &state = state_[p];
            const bool recent = state.target >= 0 && (state.send >= 0 || bindings.Now() - state.send_at < 1.0);
            if (recent && state.send < 0)
                bindings.RebuildAfter(state.send_at + 1.0 - bindings.Now());
            const int value = recent ? state.target : shown;
            if (part.material == kParts[0].material)
                planks_total = At(stock, 1);
            else
                sailcloth_total = At(stock, 1);

            // What the amount set takes: the game's "full repair needs" scales linearly.
            const int needs_full = At(stock, 2);
            const int needs = full > now ? static_cast<int>(std::lround(double(needs_full) * (value - now) / (full - now))) : 0;
            if (part.material == kParts[0].material)
                planks_planned += needs;
            const bool none = now < full && At(stock, 1) == 0;
            const bool idle = now >= full || top <= now;
            std::string need = none ? bindings.Text(part.missing)
                               : needs > 0 ? bindings.Text(part.material) + " " + std::to_string(needs)
                                           : std::string();
            const std::string unit = part.masts ? std::string() : "%";
            // The whole 0…full with the knob at the part's state: it cannot go below it nor past what the materials reach.
            Json q{{"value", value},
                   {"min", 0},
                   {"max", full},
                   {"floor", now},
                   {"ceiling", std::max(now, top)},
                   {"unit", unit},
                   {"summary", part.masts ? bindings.Text("common.of") + " " + std::to_string(full) : std::string("-")},
                   {"steps", Json::array({1, 10})},
                   {"all", true},
                   {"allLabel", bindings.Text("repair.max")},
                   {"disabled", idle || !less || !more}};
            parts.push_back(Json{{"id", part.id},
                                 {"icon", part.icon},
                                 {"label", admitted.Label(kCaption[p])},
                                 {"now", bindings.Text("repair.now") + " " + std::to_string(now) + (part.masts ? " " + bindings.Text("common.of") + " " + std::to_string(full) : "%")},
                                 {"q", q},
                                 {"need", need},
                                 {"short", none},
                                 {"disabled", idle}});

            // A settled new amount: the presses that take the game's amount there. Masts move
            // all at once: one press either way.
            if (state.send >= 0 && bindings.Now() >= state.send_at && less && more)
            {
                const int wanted = state.send;
                // While the game still shows the number earlier presses started from, they
                // are on their way: count from where they lead.
                const int from = shown == state.base && state.expected >= 0 ? state.expected : shown;
                state.send = -1;
                state.base = shown;
                state.expected = wanted;
                const int presses = part.masts ? (wanted != from ? 1 : 0) : std::abs(wanted - from);
                for (int i = 0; i < presses; ++i)
                    bindings.Send(wanted > from ? *more : *less, "press");
            }
            else if (state.send >= 0)
                bindings.RebuildAfter(state.send_at - bindings.Now());
            const auto set = [this, &bindings, p, low = now, high = std::max(now, top)](const Event &event) {
                State &state = state_[p];
                state.target = state.send = std::clamp(std::atoi(event.value.c_str()), low, high);
                state.send_at = bindings.Now() + (event.verb == "commit" ? 0.0 : 0.4);
                bindings.Rebuild();
            };
            bindings.On(part.id, "change", set);
            bindings.On(part.id, "commit", set);
        }
        view["parts"] = parts;

        Json stock = Json::array();
        if (planks_total >= 0)
            stock.push_back(Json{{"icon", "planks"}, {"label", bindings.Text("repair.planks")}, {"value", std::to_string(planks_total)}});
        if (sailcloth_total >= 0)
            stock.push_back(Json{{"icon", "sailcloth"}, {"label", bindings.Text("repair.sailcloth")}, {"value", std::to_string(sailcloth_total)}});
        view["stock"] = stock;

        // The ship being repaired.
        const std::vector<const Widget *> ships = admitted.All("SHIPS_SCROLL");
        view["subject"] = Json();
        if (ships.size() > 1)
        {
            size_t current = 0;
            for (size_t i = 0; i < ships.size(); ++i)
                if (ships[i]->value == "1")
                    current = i;
            view["subject"] = Json{{"label", ships[current]->label},
                                   {"sub", std::to_string(current + 1) + " " + bindings.Text("common.of") + " " + std::to_string(ships.size())},
                                   {"prev", current > 0},
                                   {"next", current + 1 < ships.size()},
                                   {"list", false}};
            const auto choose = [&bindings, ships](size_t index) {
                if (index < ships.size())
                    TapChoice(bindings, *ships[index]);
            };
            bindings.On("header", "subject-prev", [choose, current](const Event &) { choose(current - 1); });
            bindings.On("header", "subject-next", [choose, current](const Event &) { choose(current + 1); });
            bindings.Ignore("header", "subject-list");
        }

        Json actions = Json::array();
        if (const Widget *all = admitted.One("REPAIR_ALL_SQUADRON"))
        {
            Json button = ButtonView(bindings, *all, "all-ships", "quiet");
            button["label"] = bindings.Text("repair.all_ships");
            actions.push_back(button);
        }
        view["actions"] = actions;

        const Widget *ok = admitted.One("REPAIR_OK");
        const std::vector<int> hours = Numbers(admitted.Label("TIME_STR"));
        std::string sub;
        if (At(hours, 0) > 0)
            sub = std::to_string(At(hours, 0)) + " " + bindings.Text("save.hours");
        if (planks_planned > 0)
            sub += (sub.empty() ? "" : " · ") + bindings.Text("repair.planks") + " " + std::to_string(planks_planned);
        view["primary"] = Json{{"label", bindings.Text("repair.do")},
                               {"sub", sub},
                               {"disabled", !ok->Offers("press") || At(hours, 0) <= 0},
                               {"reason", ok->unavailable_reason},
                               {"busy", bindings.Pending("primary")}};
        bindings.Forward("primary", *ok, "press");

        const Widget *cancel = admitted.One("REPAIR_CANCEL");
        const Widget *exit = cancel ? cancel : admitted.One("EXIT_BTN");
        bindings.On("header-back", "", [&bindings, exit](const Event &) { bindings.Back(exit); });
        return view;
    }

  private:
    // The amount the player set last per part: shown until the game's own number catches up.
    struct State
    {
        int target = -1;    // shown for a second after sending, until the game's number catches up
        int send = -1;      // waiting to be sent when the drag or the steps settle
        double send_at = 0.0;
        int base = -1, expected = -1; // presses already sent from the game's `base` towards `expected`
    };
    State state_[3];
};

} // namespace

std::unique_ptr<Screen> CreateRepairScreen()
{
    return std::make_unique<RepairScreen>();
}

} // namespace storm::ui::screens
