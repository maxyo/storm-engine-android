// Loot and exchange with another ship (interface "transfer_main", mockups V2-Extra-Transfer*,
// V2-Land-Boarding-Decide / -Crew / -ShipFate). The cargo is the commerce layout (free
// transfer, trade_commerce.cpp); on top of it: the cargo / crew tabs, the ship actions and
// the game's windows — the defectors (hire), the fate of the surrendered crew and captain,
// the one question window (sink, let go, remove the captain, plain messages) as a modal,
// the captain choice as a side sheet. Dangerous answers are held, never asked twice.
// Fills the pack screen <s-transfer>.
//
// Which window is open shows by its nodes (a hidden window exports none); the question
// window's meaning comes from the script mode the engine exports (ANDROID_TRANSFER).
#include "controls.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace storm::ui::screens
{

std::unique_ptr<Screen> CreateTransferGoods();

namespace
{

constexpr NodeRule kNodes[] = {
    {"TABLE_LIST", "choice", true},
    {"EXIT_BTN", "button", true},
    {"TITLE", "heading|text"},
    {"ANDROID_TRANSFER", "transfer"},
    {"TAKE_GOODS", "button"},
    {"CAPTAN_BUTTON", "button"},
    {"SWAP_BUTTON", "button"},
    {"CREW_BUTTON", "button"},
    {"B_LEFT", "button"},
    {"B_RIGHT", "button"},
    {"SHIP_NAME", "text"},
    {"SHIP_NAME2", "text"},
    {"SHIP_TYPE", "text"},
    {"SHIP_TYPE2", "text"},
    {"SHIP_RANK", "text"},
    {"SHIP_RANK2", "text"},
    {"TABLE_OTHER", "choice"},
    {"TABLE_OTHER2", "choice"},
    {"FOOD", "text"},
    {"FOOD_SHIP", "text"},
    {"MONEY_SHIP", "text"},
    {"Money_TEXT", "text"},
    // the goods deal (trade_commerce.cpp)
    {"QTY_EDIT", "edit"},
    {"QTY_OK_BUTTON", "button"},
    {"QTY_CANCEL_BUTTON", "button"},
    {"QTY_ADD_BUTTON", "button"},
    {"QTY_REMOVE_BUTTON", "button"},
    {"QTY_ADD_ALL_BUTTON", "button"},
    {"QTY_REMOVE_ALL_BUTTON", "button"},
    {"QTY_EXIT_BTN", "button"},
    {"QTY_*", "text|heading|image"},
    // crew window
    {"CREW_EXIT_BTN", "button"},
    {"CREW_LEFT", "button"},
    {"CREW_RIGHT", "button"},
    {"CREW_QTY", "text"},
    {"CREW_QTY2", "text"},
    {"CREW_CAPTION", "text"},
    {"CREW_Win_fix", "text"},
    {"CREW_MORALE_TEXT", "text"},
    {"CREW_MORALE_TEXT2", "text"},
    {"TABLE_CREW", "choice|text"},
    {"TABLE_CREW2", "choice|text"},
    {"BAR_*", "image|slider|text"},
    // the question window
    {"REMOVE_WINDOW_CAPTION", "text"},
    {"REMOVE_WINDOW_TEXT", "text"},
    {"REMOVE_OK", "button"},
    {"REMOVE_ACCEPT_OFFICER", "button"},
    {"REMOVE_CANCEL_OFFICER", "button"},
    {"REMOVE_EXIT_BTN", "button"},
    // captain and crew capture
    {"CAPTURE_CAPTION", "text"},
    {"CAPTURE_TEXT", "text"},
    {"CAPTURE_CAPTAIN_KILL", "button"},
    {"CAPTURE_CAPTAIN_PRISON", "button"},
    {"CAPTURE_CANCEL", "button"},
    {"CAPTURE_EXIT_BTN", "button"},
    {"CAPTURE_CREW_CAPTION", "text"},
    {"CAPTURE_TEXT_CREW", "text"},
    {"CAPTURE_CREW_KILL", "button"},
    {"CAPTURE_CREW_PRISON", "button"},
    {"CAPTURE_CREW_FREE", "button"},
    // defectors
    {"HIRECAPTION1", "text"},
    {"HIRE_EXIT_BTN", "button"},
    {"HIRE_QTY_TypeOperation", "text"},
    {"HIRED_CREW", "text"},
    {"HIRE_CREW_QTY", "text"},
    {"HIRE_CREW_QTY2", "text"},
    {"HIRE_QTY_OK_BUTTON", "button"},
    {"HIRE_QTY_CANCEL_BUTTON", "button"},
    {"HIRE_QTY_ADD_BUTTON", "button"},
    {"HIRE_QTY_REMOVE_BUTTON", "button"},
    {"HIRE_QTY_ADD_ALL_BUTTON", "button"},
    {"HIRE_QTY_REMOVE_ALL_BUTTON", "button"},
    {"HIRE_*", "text|choice|image"},
    {"CAPACITY", "text"},
    {"MAIN_HERO_NAME", "text"},
    // captain choice
    {"OFFICERS_WINDOW_CAPTION", "text"},
    {"PASSENGERSLIST", "choice"},
    {"LEFTSCROLLBUTTON", "button"},
    {"RIGHTSCROLLBUTTON", "button"},
    {"OFFICER_NAME", "text"},
    {"ACCEPT_ADD_OFFICER", "button"},
    {"CANCEL_ADD_OFFICER", "button"},
    {"OFFICER_EXIT_BTN", "button"},
    {"TABLE_SMALL*", "choice|text"},
    {"MAIN_CHARACTER_PICTURE*", "image"},
    {"SHIP_BIG_PICTURE*", "image"},
};

std::string Trim(const std::string &text)
{
    const size_t first = text.find_first_not_of(" \r\n\t");
    if (first == std::string::npos)
        return {};
    std::string out = text.substr(first, text.find_last_not_of(" \r\n\t") - first + 1);
    std::replace(out.begin(), out.end(), '\r', ' ');
    std::replace(out.begin(), out.end(), '\n', ' ');
    return out;
}

int Number(const std::string &text)
{
    std::string digits;
    for (const char c : text)
        if (c >= '0' && c <= '9')
            digits += c;
        else if (!digits.empty() && c != ' ')
            break;
    return digits.empty() ? 0 : std::atoi(digits.c_str());
}

// "317 / 700" in the ship table row `stat` → 700 − 317; -1 when the row is missing.
int FreeSpace(const Admission &admitted, const char *table)
{
    for (const Widget *row : admitted.All(table))
        if (row->row_id == "Capacity")
        {
            const std::string text = row->CellText(2);
            const size_t slash = text.find('/');
            if (slash != std::string::npos)
                return Number(text.substr(slash + 1)) - Number(text.substr(0, slash));
        }
    return -1;
}

std::string Context(const Widget *marker, const char *key)
{
    if (!marker)
        return {};
    const auto found = marker->context.find(key);
    return found == marker->context.end() ? std::string() : found->second;
}

class TransferScreen final : public Screen
{
  public:
    TransferScreen() : goods_(CreateTransferGoods())
    {
    }
    const char *Component() const override
    {
        return "s-transfer";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        Json view = goods_->Build(admitted, bindings);
        view.erase("filters");
        view.erase("info");
        view.erase("infoLabel");
        const std::string title = Trim(admitted.Label("TITLE"));
        view["title"] = title.empty() ? bindings.Text("transfer.title") : title;

        const Widget *marker = admitted.One("ANDROID_TRANSFER");
        if (!marker)
            bindings.Error("transfer_main: the engine sent no ANDROID_TRANSFER state");
        else if (!marker->unavailable_reason.empty())
            bindings.Error("transfer_main: " + marker->unavailable_reason);
        const std::string mode = Context(marker, "mode");
        const bool captain_alive = Context(marker, "captainAlive") == "1";
        const std::string ours = admitted.Label("SHIP_NAME"), theirs = admitted.Label("SHIP_NAME2");
        if (!ours.empty() && !theirs.empty() && view["list"].contains("cols"))
            view["list"]["cols"] = Json::array({Json{{"label", ours}}, Json{{"label", theirs}}});

        // Header: the two ships, their free space (or the vacant captain), the next ship.
        Json stats = Json::array();
        const int free_ours = FreeSpace(admitted, "TABLE_OTHER"), free_theirs = FreeSpace(admitted, "TABLE_OTHER2");
        if (free_ours >= 0)
            stats.push_back(Json{{"label", ours}, {"value", std::to_string(free_ours) + " " + bindings.Text("transfer.free")}});
        if (!captain_alive && marker && Context(marker, "exchange") == "0")
            stats.push_back(Json{{"label", theirs}, {"value", bindings.Text("transfer.no_captain")}, {"tone", "danger"}});
        else if (free_theirs >= 0)
            stats.push_back(Json{{"label", theirs}, {"value", std::to_string(free_theirs) + " " + bindings.Text("transfer.free")}});
        view["stats"] = stats;
        const Widget *left = admitted.One("B_LEFT"), *right = admitted.One("B_RIGHT");
        view["subject"] = Json();
        if ((left && left->Offers("press")) || (right && right->Offers("press")))
        {
            view["subject"] = Json{{"label", theirs}, {"sub", admitted.Label("SHIP_TYPE2")}, {"prev", left && left->Offers("press")},
                                   {"next", right && right->Offers("press")}, {"list", false}};
            bindings.On("header", "subject-prev", [&bindings, left](const Event &) { if (left) bindings.Send(*left, "press"); });
            bindings.On("header", "subject-next", [&bindings, right](const Event &) { if (right) bindings.Send(*right, "press"); });
            bindings.Ignore("header", "subject-list");
        }

        // Tabs: the crew is the game's crew window.
        const Widget *crew_button = admitted.One("CREW_BUTTON");
        const Widget *crew_exit = admitted.One("CREW_EXIT_BTN");
        const bool crew_open = admitted.One("CREW_QTY") != nullptr;
        view["tabs"] = Json::array();
        view["crew"] = Json();
        if (crew_button)
        {
            view["tabs"] = Json::array({Json{{"label", bindings.Text("transfer.cargo")}, {"selected", !crew_open}},
                                        Json{{"label", bindings.Text("transfer.crew")}, {"selected", crew_open},
                                             {"disabled", !crew_open && !crew_button->Offers("press")}, {"reason", crew_button->unavailable_reason}}});
            bindings.On("header", "tab", [&bindings, crew_open, crew_button, crew_exit](const Event &event) {
                const bool want_crew = event.value == "1";
                if (want_crew && !crew_open && crew_button->Offers("press"))
                    bindings.Send(*crew_button, "press");
                else if (!want_crew && crew_open)
                    bindings.Send(crew_exit ? *crew_exit : *crew_button, "press");
            });
        }
        if (crew_open)
            view["crew"] = CrewView(admitted, bindings, ours, theirs);

        // The bar: captain, swap, take everything.
        Json actions = Json::array();
        for (const char *node : {"CAPTAN_BUTTON", "SWAP_BUTTON"})
            if (const Widget *button = admitted.One(node))
                actions.push_back(ButtonView(bindings, *button, std::string("action-") + node, "secondary"));
        view["actions"] = actions;
        view["primary"] = Json();
        if (const Widget *take = admitted.One("TAKE_GOODS"); take && !crew_open)
        {
            int weight = 0;
            for (const Widget *row : admitted.All("TABLE_LIST"))
                weight += Number(row->CellText(4));
            view["primary"] = Json{{"label", bindings.Text("transfer.take_all")},
                                   {"sub", weight > 0 ? std::to_string(weight) + " " + bindings.Text("transfer.cwt") : std::string()},
                                   {"disabled", !take->Offers("press") || weight == 0},
                                   {"reason", take->unavailable_reason},
                                   {"busy", bindings.Pending("primary")}};
            bindings.Forward("primary", *take, "press");
        }

        // The game's windows. Each binds the platform back to its own way out.
        const Widget *way_out = nullptr;
        view["modal"] = Json();
        view["sheet"] = Json();
        if (const Widget *ok = admitted.One("HIRE_QTY_OK_BUTTON"))
            way_out = Hire(admitted, bindings, view, ok);
        else if (const Widget *kill = admitted.One("CAPTURE_CREW_KILL"))
        {
            Json acts = Json::array();
            acts.push_back(Action(bindings, *kill, "fate-kill", kill->label, "danger", true));
            if (const Widget *prison = admitted.One("CAPTURE_CREW_PRISON"))
                acts.push_back(Action(bindings, *prison, "fate-prison", prison->label, "secondary"));
            const Widget *free = admitted.One("CAPTURE_CREW_FREE");
            if (free)
                acts.push_back(Action(bindings, *free, "fate-free", free->label, "primary"));
            view["modal"] = Json{{"step", hire_seen_ ? "2/2" : ""},
                                 {"title", Trim(admitted.Label("CAPTURE_TEXT_CREW"))},
                                 {"text", ""},
                                 {"actions", acts}};
            way_out = free;
        }
        else if (const Widget *kill_captain = admitted.One("CAPTURE_CAPTAIN_KILL"))
        {
            Json acts = Json::array();
            acts.push_back(Action(bindings, *kill_captain, "capture-kill", kill_captain->label, "danger", true));
            if (const Widget *prison = admitted.One("CAPTURE_CAPTAIN_PRISON"))
                acts.push_back(Action(bindings, *prison, "capture-prison", prison->label, "primary"));
            const Widget *cancel = admitted.One("CAPTURE_CANCEL");
            if (cancel)
                acts.push_back(Action(bindings, *cancel, "capture-cancel", cancel->label, "quiet"));
            view["modal"] = Json{{"title", Trim(admitted.Label("CAPTURE_CAPTION"))}, {"text", Trim(admitted.Label("CAPTURE_TEXT"))}, {"actions", acts}};
            way_out = cancel;
        }
        else if (const Widget *accept = admitted.One("REMOVE_ACCEPT_OFFICER"))
        {
            const Widget *cancel = admitted.One("REMOVE_CANCEL_OFFICER");
            Json acts = Json::array();
            if (mode == "ShipDeadAsk" || mode == "ShipGoFreeAsk")
            {
                // Leaving the loot: sink the ship without a captain, or let the surrendered
                // captain go; keeping it means staying here to appoint a captain.
                const bool sink = mode == "ShipDeadAsk";
                acts.push_back(Action(bindings, *accept, "fate-ship", bindings.Text(sink ? "transfer.sink" : "transfer.release"),
                                      sink ? "danger" : "secondary", sink));
                if (cancel)
                    acts.push_back(Action(bindings, *cancel, "fate-keep", bindings.Text("transfer.keep"), "primary"));
                view["modal"] = Json{{"title", "«" + theirs + "»: " + bindings.Text("transfer.fate")}, {"text", ""}, {"actions", acts}};
            }
            else
            {
                if (cancel)
                    acts.push_back(Action(bindings, *cancel, "question-no", cancel->label, "quiet"));
                acts.push_back(Action(bindings, *accept, "question-yes", accept->label, "primary"));
                view["modal"] = Json{{"title", Trim(admitted.Label("REMOVE_WINDOW_CAPTION"))}, {"text", Trim(admitted.Label("REMOVE_WINDOW_TEXT"))}, {"actions", acts}};
            }
            way_out = cancel;
        }
        else if (const Widget *ok_message = admitted.One("REMOVE_OK"))
        {
            view["modal"] = Json{{"title", Trim(admitted.Label("REMOVE_WINDOW_CAPTION"))},
                                 {"text", Trim(admitted.Label("REMOVE_WINDOW_TEXT"))},
                                 {"actions", Json::array({Action(bindings, *ok_message, "message-ok", ok_message->label, "primary")})}};
            way_out = ok_message;
        }
        else if (admitted.One("ACCEPT_ADD_OFFICER") || !admitted.All("PASSENGERSLIST").empty())
            way_out = Captains(admitted, bindings, view);

        const Widget *exit = admitted.One("EXIT_BTN");
        const bool deal = admitted.One("QTY_OK_BUTTON") && admitted.One("QTY_EDIT");
        const Widget *deal_cancel = admitted.One("QTY_CANCEL_BUTTON");
        bindings.On("header-back", "", [&bindings, way_out, crew_open, crew_exit, deal, deal_cancel, exit](const Event &) {
            if (way_out)
                bindings.Send(*way_out, "press");
            else if (crew_open && crew_exit)
                bindings.Send(*crew_exit, "press");
            else if (deal && deal_cancel)
                bindings.Send(*deal_cancel, "press");
            else
                bindings.Back(exit);
        });
        return view;
    }

  private:
    // {id, label, variant, hold} of a modal button that presses `widget`.
    static Json Action(Bindings &bindings, const Widget &widget, const std::string &id, const std::string &label, const char *variant, bool hold = false)
    {
        bindings.Forward(id, widget, "press");
        return Json{{"id", id}, {"label", label}, {"variant", variant}, {"hold", hold}, {"disabled", !widget.Offers("press")}, {"reason", widget.unavailable_reason}};
    }

    // The crew of both ships: our number moves by the game's arrows, one man per press.
    Json CrewView(const Admission &admitted, Bindings &bindings, const std::string &ours, const std::string &theirs)
    {
        const Widget *take = admitted.One("CREW_LEFT"), *give = admitted.One("CREW_RIGHT");
        const int our = Number(admitted.Label("CREW_QTY")), their = Number(admitted.Label("CREW_QTY2"));
        const bool recent = crew_.target >= 0 && (crew_.send >= 0 || bindings.Now() - crew_.send_at < 1.0);
        if (recent && crew_.send < 0)
            bindings.RebuildAfter(crew_.send_at + 1.0 - bindings.Now());
        if (crew_.send >= 0 && bindings.Now() >= crew_.send_at && take && give)
        {
            const int wanted = crew_.send;
            crew_.send = -1;
            for (int i = 0; i < std::abs(wanted - our); ++i)
                bindings.Send(wanted > our ? *take : *give, "press");
        }
        else if (crew_.send >= 0)
            bindings.RebuildAfter(crew_.send_at - bindings.Now());
        const auto set = [this, &bindings](const Event &event) {
            crew_.target = crew_.send = std::atoi(event.value.c_str());
            crew_.send_at = bindings.Now() + (event.verb == "commit" ? 0.0 : 0.4);
            bindings.Rebuild();
        };
        bindings.On("crew", "change", set);
        bindings.On("crew", "commit", set);
        bindings.Ignore("crew", "row");
        Json q{{"value", recent ? crew_.target : our},
               {"min", 0},
               {"max", our + their},
               {"label", ours},
               {"unit", bindings.Text("transfer.men")},
               {"steps", Json::array({1, 10})},
               {"disabled", !take || !give}};
        Json row{{"id", "0"}, {"name", bindings.Text("transfer.sailors")}, {"image", Json{{"kind", "icon"}, {"icon", "crew"}}},
                 {"cells", Json::array({std::to_string(our), std::to_string(their)})}, {"selected", true}, {"q", q}};
        const auto side = [&](const std::string &name, const char *table, const char *morale) {
            Json facts = Json::array();
            for (const Widget *r : admitted.All(table))
                if (!r->CellText(1).empty())
                    facts.push_back(Json{{"k", r->CellText(1)}, {"v", r->CellText(2)}});
            return Json{{"name", name}, {"morale", Trim(admitted.Label(morale))}, {"facts", facts}};
        };
        return Json{{"label", bindings.Text("transfer.crew")},
                    {"cols", Json::array({Json{{"label", ours}}, Json{{"label", theirs}}})},
                    {"rows", Json::array({row})},
                    {"sides", Json::array({side(ours, "TABLE_CREW", "CREW_MORALE_TEXT"), side(theirs, "TABLE_CREW2", "CREW_MORALE_TEXT2")})}};
    }

    // The defectors: how many of the enemy crew join us. The number moves by the game's
    // arrows; the first number the window shows is everyone willing.
    const Widget *Hire(const Admission &admitted, Bindings &bindings, Json &view, const Widget *ok)
    {
        hire_seen_ = true;
        const Widget *more = admitted.One("HIRE_QTY_ADD_BUTTON"), *less = admitted.One("HIRE_QTY_REMOVE_BUTTON");
        const int hired = Number(admitted.Label("HIRED_CREW"));
        hire_max_ = std::max(hire_max_, hired);
        const bool recent = hire_.target >= 0 && (hire_.send >= 0 || bindings.Now() - hire_.send_at < 1.0);
        if (recent && hire_.send < 0)
            bindings.RebuildAfter(hire_.send_at + 1.0 - bindings.Now());
        if (hire_.send >= 0 && bindings.Now() >= hire_.send_at && more && less)
        {
            const int wanted = hire_.send;
            hire_.send = -1;
            for (int i = 0; i < std::abs(wanted - hired); ++i)
                bindings.Send(wanted > hired ? *more : *less, "press");
        }
        else if (hire_.send >= 0)
            bindings.RebuildAfter(hire_.send_at - bindings.Now());
        const auto set = [this, &bindings](const Event &event) {
            hire_.target = hire_.send = std::atoi(event.value.c_str());
            hire_.send_at = bindings.Now() + (event.verb == "commit" ? 0.0 : 0.4);
            bindings.Rebuild();
        };
        bindings.On("hire", "change", set);
        bindings.On("hire", "commit", set);
        const int value = recent ? hire_.target : hired;
        const int crew = Number(admitted.Label("HIRE_CREW_QTY"));
        Json acts = Json::array();
        const Widget *refuse = admitted.One("HIRE_QTY_CANCEL_BUTTON");
        if (refuse)
            acts.push_back(Action(bindings, *refuse, "hire-none", bindings.Text("transfer.nobody"), "quiet"));
        Json take = Action(bindings, *ok, "hire-ok", bindings.Text("transfer.take") + " " + std::to_string(value), "primary");
        take["sub"] = bindings.Text("transfer.crew") + " " + std::to_string(crew) + " › " + std::to_string(crew + value);
        take["disabled"] = !ok->Offers("press") || value == 0;
        acts.push_back(take);
        view["modal"] = Json{{"step", "1/2"},
                             {"title", Trim(admitted.Label("HIRE_QTY_TypeOperation"))},
                             {"text", ""},
                             {"actions", acts},
                             {"q", Json{{"value", value},
                                        {"min", 0},
                                        {"max", std::max(1, hire_max_)},
                                        {"unit", bindings.Text("transfer.men")},
                                        {"steps", Json::array({1, 10})},
                                        {"all", true},
                                        {"disabled", !more || !less}}}};
        return refuse;
    }

    // The captain of the other ship: the passengers who can take it.
    const Widget *Captains(const Admission &admitted, Bindings &bindings, Json &view)
    {
        Json people = Json::array();
        size_t i = 0;
        for (const Widget *person : admitted.All("PASSENGERSLIST"))
        {
            const std::string id = "captain-" + std::to_string(i++);
            people.push_back(Json{{"id", id},
                                  {"title", person->label},
                                  {"image", PictureOf(person->FirstImage())},
                                  {"selected", person->value == "1"},
                                  {"disabled", !person->Offers("select")},
                                  {"reason", person->unavailable_reason}});
            bindings.On(id, "", [&bindings, person](const Event &) {
                if (person->value != "1")
                    TapChoice(bindings, *person);
            });
        }
        Json acts = Json::array();
        if (const Widget *accept = admitted.One("ACCEPT_ADD_OFFICER"))
            acts.push_back(Action(bindings, *accept, "sheet-ok", accept->label, "primary"));
        view["sheet"] = Json{{"title", Trim(admitted.Label("OFFICERS_WINDOW_CAPTION"))},
                             {"subtitle", Trim(admitted.Label("OFFICER_NAME"))},
                             {"people", people},
                             {"actions", acts}};
        const Widget *close = admitted.One("CANCEL_ADD_OFFICER") ? admitted.One("CANCEL_ADD_OFFICER") : admitted.One("OFFICER_EXIT_BTN");
        if (close)
            bindings.Forward("sheet-close", *close, "press");
        return close;
    }

    struct Amount
    {
        int target = -1;  // shown for a second after sending, until the game's number catches up
        int send = -1;    // waiting to be sent when the drag or the steps settle
        double send_at = 0.0;
    };
    std::unique_ptr<Screen> goods_;
    Amount crew_, hire_;
    int hire_max_ = 0;
    bool hire_seen_ = false;
};

} // namespace

std::unique_ptr<Screen> CreateTransferScreen()
{
    return std::make_unique<TransferScreen>();
}

} // namespace storm::ui::screens
