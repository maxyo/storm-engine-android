// Loot and exchange with another ship (interface "transfer_main": a boarded prize, a companion,
// a ship met at sea). The cargo is the commerce layout (free transfer, trade_commerce.cpp);
// on top of it: the cargo / crew / ships tabs (the two ships' figures side by side, the other
// ship's better ones green and worse red: what the keep / swap / sink decision needs and the
// stock screen shows in two small tables), the other ship's card with the ship actions in the dock, and
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
#include <cstdio>
#include <cstdlib>

namespace storm::ui::screens
{

std::unique_ptr<Screen> CreateTransferGoods();

namespace
{

constexpr NodeRule kNodes[] = {
    {"TABLE_LIST", "choice|table", true},
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
    {"CAPTUR_PICTURE", "image"}, // the surrendered captain's portrait (the ini's own spelling)
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
        view["list"].erase("filters"); // the cargo is one short list of the two holds
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
        if (!ours.empty() && !theirs.empty() && view["list"].contains("cols") && view["list"]["cols"].size() == 2)
        {
            view["list"]["cols"][0]["label"] = ours;
            view["list"]["cols"][1]["label"] = theirs;
        }

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
        view["subject"] = Json();

        // Tabs: the crew is the game's crew window; the ships are the two figure tables it always sends.
        const Widget *crew_button = admitted.One("CREW_BUTTON");
        const Widget *crew_exit = admitted.One("CREW_EXIT_BTN");
        const bool crew_open = admitted.One("CREW_QTY") != nullptr;
        const bool has_ships = !admitted.All("TABLE_OTHER").empty() && !admitted.All("TABLE_OTHER2").empty();
        if (crew_open || !has_ships)
            ships_open_ = false;
        view["tabs"] = Json::array();
        view["crew"] = Json();
        if (crew_button || has_ships)
        {
            Json tabs = Json::array({Json{{"id", "cargo"}, {"label", bindings.Text("transfer.cargo")}, {"selected", !crew_open && !ships_open_}}});
            if (crew_button)
                tabs.push_back(Json{{"id", "crew"}, {"label", bindings.Text("transfer.crew")}, {"selected", crew_open},
                                    {"disabled", !crew_open && !crew_button->Offers("press")}, {"reason", crew_button->unavailable_reason}});
            if (has_ships)
                tabs.push_back(Json{{"id", "ships"}, {"label", bindings.Text("transfer.ships")}, {"selected", ships_open_}});
            view["tabs"] = tabs;
            // The header's own tabs report their index.
            bindings.On("header", "tab", [this, &bindings, tabs, crew_open, crew_button, crew_exit](const Event &event) {
                const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
                const std::string id = index < tabs.size() ? tabs[index].value("id", "") : std::string();
                const bool want_crew = id == "crew";
                ships_open_ = id == "ships";
                if (want_crew && !crew_open && crew_button && crew_button->Offers("press"))
                    bindings.Send(*crew_button, "press");
                else if (!want_crew && crew_open)
                    bindings.Send(crew_exit ? *crew_exit : *crew_button, "press");
                else
                    bindings.Rebuild();
            });
        }
        if (crew_open)
            view["crew"] = CrewView(admitted, bindings, ours, theirs);
        view["ships"] = ships_open_ ? ShipsView(admitted, bindings, ours, theirs) : Json();
        view["prize"] = has_ships ? PrizeView(admitted, bindings, theirs) : Json();

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
            for (const Widget *row : admitted.Rows("TABLE_LIST"))
                weight += Number(row->CellText(4));
            view["primary"] = Json{{"label", bindings.Text("transfer.take_all")},
                                   {"sub", weight > 0 ? std::to_string(weight) + " " + bindings.Text("transfer.cwt") : std::string()},
                                   {"disabled", !take->Offers("press") || weight == 0},
                                   {"reason", take->unavailable_reason},
                                   {"busy", bindings.Pending("primary")}};
            bindings.Forward("primary", *take, "press");
        }
        // The amount set for the cargo row or the crew is a draft (AmountDraft): while it differs from what the
        // game has, the bar is its «Взять / Отдать N»; another row or the amount set back drops it.
        if (crew_open)
            CrewDeal(admitted, bindings, view);
        else
            Cargo(admitted, bindings, view, ours);

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
            // The captain's face beside the question, where the game names the picture (a portrait).
            const Widget *face = admitted.One("CAPTUR_PICTURE");
            view["modal"] = Json{{"title", Trim(admitted.Label("CAPTURE_CAPTION"))}, {"text", Trim(admitted.Label("CAPTURE_TEXT"))}, {"actions", acts},
                                 {"image", PictureOf(face ? face->FirstImage() : nullptr)}};
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

        // A tap beside a window of the game answers nothing: these are questions with consequences (a miss must
        // not sink a ship or refuse the defectors). Seen on the device as an unmapped-event error.
        for (const char *window : {"modal", "amount", "sheet"})
            bindings.Ignore(window, "dismiss");
        const Widget *exit = admitted.One("EXIT_BTN");
        const bool deal = admitted.One("QTY_OK_BUTTON") && admitted.One("QTY_EDIT");
        const Widget *deal_cancel = admitted.One("QTY_CANCEL_BUTTON");
        bindings.On("header-back", "", [this, &bindings, way_out, crew_open, crew_exit, deal, deal_cancel, exit](const Event &) {
            if (!way_out && (crew_open ? crew_.Drop() : cargo_.Drop()))
                bindings.Rebuild(); // back drops the number set first
            else if (way_out)
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
    static const Widget *FigureRow(const Admission &admitted, const char *table, const std::string &row_id)
    {
        for (const Widget *row : admitted.All(table))
            if (row->row_id == row_id)
                return row;
        return nullptr;
    }

    // The last number of a figure ("610 / 610" → 610, "14.59" → 14.59, "40 : 9 / 40" → 40); -1 without one.
    static double LastNumber(const std::string &text)
    {
        const size_t end = text.find_last_of("0123456789");
        if (end == std::string::npos)
            return -1.0;
        size_t begin = end;
        while (begin > 0 && ((text[begin - 1] >= '0' && text[begin - 1] <= '9') || text[begin - 1] == '.'))
            --begin;
        return std::atof(text.substr(begin, end - begin + 1).c_str());
    }

    // The two ships side by side, row by row of the game's tables (ours TABLE_OTHER, theirs TABLE_OTHER2).
    // The other ship's figure is green where a larger last number is plainly better and it has it, red
    // where it has less (hull, speed, turn rate, deadweight, crew); the rest stays plain.
    Json ShipsView(const Admission &admitted, Bindings &bindings, const std::string &ours, const std::string &theirs)
    {
        struct Figure
        {
            const char *row;
            const char *icon;
            bool larger_is_better;
        };
        static const Figure figures[] = {{"Hull", "hull", true},         {"Sails", "sail-full", false},    {"Speed", "speed", true},
                                         {"Maneuver", "maneuver", true}, {"AgainstWind", "upwind", false}, {"Capacity", "capacity", true},
                                         {"Crew", "crew", true},         {"sCannons", "caliber", false},   {"CannonType", "cannon", false}};
        Json rows = Json::array();
        for (const Widget *row : admitted.All("TABLE_OTHER"))
        {
            if (row->CellText(1).empty())
                continue;
            const Figure *figure = nullptr;
            for (const Figure &known : figures)
                if (row->row_id == known.row)
                    figure = &known;
            const Widget *other = FigureRow(admitted, "TABLE_OTHER2", row->row_id);
            const std::string mine = row->CellText(2), their = other ? other->CellText(2) : std::string();
            Json line{{"k", row->CellText(1)}, {"a", mine}, {"b", their}, {"icon", figure ? figure->icon : "ship"}};
            const double left = LastNumber(mine), right = LastNumber(their);
            if (figure && figure->larger_is_better && left >= 0 && right >= 0 && left != right)
                line["tone"] = right > left ? "ok" : "danger";
            rows.push_back(std::move(line));
        }
        const auto side = [&](const std::string &name, const char *type, const char *rank) {
            std::string sub = Trim(admitted.Label(type));
            if (const std::string cls = Trim(admitted.Label(rank)); !cls.empty())
                sub += (sub.empty() ? "" : " \xC2\xB7 ") + bindings.Text("yard.class") + " " + cls;
            return Json{{"title", name}, {"sub", sub}};
        };
        return Json{{"ours", side(ours, "SHIP_TYPE", "SHIP_RANK")}, {"theirs", side(theirs, "SHIP_TYPE2", "SHIP_RANK2")}, {"rows", rows}};
    }

    // The other ship's card of the dock: what it is and the state it is in.
    Json PrizeView(const Admission &admitted, Bindings &bindings, const std::string &theirs)
    {
        std::string sub = Trim(admitted.Label("SHIP_TYPE2"));
        if (const std::string cls = Trim(admitted.Label("SHIP_RANK2")); !cls.empty())
            sub += (sub.empty() ? "" : " \xC2\xB7 ") + bindings.Text("yard.class") + " " + cls;
        // Hull and sails as the share of whole ("2100 / 2384" → 88 %), the crew as it stands against the full one.
        Json facts = Json::array();
        for (const char *row_id : {"Hull", "Sails", "Crew"})
        {
            const Widget *row = FigureRow(admitted, "TABLE_OTHER2", row_id);
            if (!row)
                continue;
            const std::string text = row->CellText(2);
            const double whole = LastNumber(text), now = std::atof(text.c_str());
            if (whole <= 0)
                continue;
            const bool crew = std::string(row_id) == "Crew";
            const int percent = static_cast<int>(std::clamp(std::round(now * 100.0 / whole), 0.0, 999.0));
            facts.push_back(Json{{"k", row->CellText(1)},
                                 {"v", crew ? std::to_string(static_cast<int>(now)) + " / " + std::to_string(static_cast<int>(whole)) : std::to_string(percent) + " %"},
                                 {"tone", percent < 50 ? "danger" : ""}});
        }
        const Widget *picture = admitted.One("SHIP_BIG_PICTURE2");
        return Json{{"title", theirs}, {"sub", sub}, {"image", PictureOf(picture ? picture->FirstImage() : nullptr)}, {"facts", facts}};
    }

    // {id, label, variant, hold} of a modal button that presses `widget`.
    static Json Action(Bindings &bindings, const Widget &widget, const std::string &id, const std::string &label, const char *variant, bool hold = false)
    {
        bindings.Forward(id, widget, "press");
        return Json{{"id", id}, {"label", label}, {"variant", variant}, {"hold", hold}, {"disabled", !widget.Offers("press")}, {"reason", widget.unavailable_reason}};
    }

    // How far the game's arrows can move one good, press by press as transfer_main.c TakeGoods / GiveGoods and
    // AddCharacterGoodsSimple do it: a press moves one pack (or what is left), cut to the whole packs the
    // receiving hold has room for (GetGoodQuantityByWeight of its free space); a hold whose load reaches its
    // capacity stops it. `free` < 0: the free space is not known, the stock alone limits. Returns how many pieces
    // the presses move in all.
    static int Reach(int receiving, int stock, int free, int pack, double pack_weight)
    {
        const auto weight = [pack, pack_weight](int amount) { // goods.h GetGoodWeightByType
            return static_cast<int>(((amount + pack - 1) / pack) * pack_weight + 0.05);
        };
        int moved = 0;
        while (stock > 0)
        {
            int amount = std::min(pack, stock);
            if (free >= 0 && pack_weight > 0.0)
                amount = std::min(amount, pack * static_cast<int>(free / pack_weight));
            if (amount <= 0)
                break;
            if (free >= 0)
                free -= weight(receiving + amount) - weight(receiving);
            receiving += amount, stock -= amount, moved += amount;
        }
        return moved;
    }

    // The cargo: a tap selects a row (the game's arrows act on the selected row), the selected row opens its
    // amount; the game moves one pack per arrow press (B_LEFT takes, B_RIGHT gives), sent when the player
    // confirms the amount, so the amount steps by packs. The game shows each arrow only while its way is
    // possible (transfer_main.c SetGoodsArrows: the giving side has some, the other hold has room for a pack)
    // and exports only the arrows it shows: each direction stands alone (a good the other ship no longer has
    // is still given back to it). The game's own amount window here is «throw overboard», not a transfer, and
    // is not offered.
    void Cargo(const Admission &admitted, Bindings &bindings, Json &view, const std::string &ours)
    {
        const std::vector<const Widget *> goods = admitted.Rows("TABLE_LIST");
        const Widget *take = admitted.One("B_LEFT"), *give = admitted.One("B_RIGHT");
        const Widget *selected = nullptr;
        for (const Widget *row : goods)
            if (row->value == "1")
                selected = row;
        if (!view["list"].contains("rows"))
            return;
        Json &rows = view["list"]["rows"];
        const int own = selected ? Number(selected->CellText(0)) : 0, other = selected ? Number(selected->CellText(3)) : 0;
        // "20 / 2": the pack and what it weighs
        const std::string pack_cell = selected ? selected->CellText(5) : std::string();
        const int pack = std::max(1, Number(pack_cell));
        const size_t slash = pack_cell.find('/');
        const double pack_weight = slash == std::string::npos ? 0.0 : std::atof(pack_cell.c_str() + slash + 1);
        if (selected)
            cargo_.Follow(std::to_string(selected->index), own, other, bindings); // the rows carry no identity here
        else
            cargo_.Close();
        const bool can_take = take && take->Offers("press"), can_give = give && give->Offers("press");
        const int low = own - (can_give ? Reach(other, own, FreeSpace(admitted, "TABLE_OTHER2"), pack, pack_weight) : 0);
        const int high = own + (can_take ? Reach(own, other, FreeSpace(admitted, "TABLE_OTHER"), pack, pack_weight) : 0);
        // The game moves whole packs: |delta| rounded up to packs, by its arrows.
        const auto move = [&bindings, take, give, pack](int delta) {
            const Widget *arrow = delta > 0 ? take : give;
            if (!arrow)
                return;
            const int presses = (std::abs(delta) + pack - 1) / pack;
            for (int i = 0; i < presses; ++i)
                bindings.Send(*arrow, "press");
        };
        // The hand at a row's end: once the game has selected that row, all of the other hold is taken.
        if (selected && selected->index == quick_row_)
        {
            quick_row_ = -1;
            if (can_take && high > own)
            {
                cargo_.Sent(high, bindings.Now());
                move(high - own);
            }
        }
        view["list"]["quick"] = "hand"; // the game shows its arrows for the selected row only: a row is offered by what the other hold has
        view["dealing"] = selected != nullptr;
        for (size_t i = 0; i < rows.size() && i < goods.size(); ++i)
        {
            Json &row = rows[i];
            const size_t index = static_cast<size_t>(std::atoi(row["id"].get<std::string>().c_str()));
            const bool open = index < goods.size() && goods[index] == selected;
            row["selected"] = open;
            row["quick"] = index < goods.size() && Number(goods[index]->CellText(3)) > 0 && (open ? can_take : goods[index]->Offers("select"));
            row.erase("q");
            if (!open)
                continue;
            row["q"] = Json{{"value", cargo_.Value(low, high)},
                            {"min", low},
                            {"max", high},
                            {"zones", true},
                            {"origin", own},
                            {"lessLabel", bindings.Text("trade.give")},
                            {"moreLabel", bindings.Text("trade.take")},
                            {"label", ours},
                            {"summary", "-"},
                            {"steps", Json::array({pack, pack * 10, pack * 100})},
                            {"disabled", low == high}};
            // The dock's facts: what a pack weighs (the icon, no word) and how many pieces it is.
            Json facts = Json::array();
            if (pack_weight > 0.0)
            {
                char weight[32];
                std::snprintf(weight, sizeof(weight), "%g", pack_weight);
                facts.push_back(Json{{"icon", "weight"}, {"v", std::string(weight) + " " + bindings.Text("transfer.cwt")}});
                if (pack > 1)
                    facts.push_back(Json{{"v", Fill(bindings.Text("trade.per_units"), {std::to_string(pack)})}, {"tone", "dim"}});
            }
            row["facts"] = facts;
        }
        // The game hides both arrows while its focus is off the table (ProcessFrame), as after the crew window: a tap
        // on the selected row then selects it again, which brings the focus and the arrows back.
        const bool arrowless = !take && !give;
        bindings.On("goods", "row", [this, &bindings, goods, selected, arrowless](const Event &event) {
            const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
            quick_row_ = -1;
            if (index < goods.size() && (goods[index] != selected || arrowless) && goods[index]->Offers("select"))
                bindings.Send(*goods[index], "select", goods[index]->row_id);
        });
        bindings.On("goods", "quick", [this, &bindings, goods, selected](const Event &event) {
            const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
            if (index >= goods.size())
                return;
            quick_row_ = goods[index]->index;
            if (goods[index] == selected)
                bindings.Rebuild();
            else if (goods[index]->Offers("select"))
                bindings.Send(*goods[index], "select", goods[index]->row_id);
            else
                quick_row_ = -1;
        });
        cargo_.Bind(bindings, "goods", low, high, std::string(), pack);
        if (selected && low < high)
        {
            // The button's second line: what our hold's load changes by (every started pack weighs whole).
            const auto load = [pack, pack_weight](int amount) { return static_cast<int>(((amount + pack - 1) / pack) * pack_weight + 0.05); };
            const int value = cargo_.Value(low, high);
            const int change = std::abs(load(value) - load(own));
            Json primary = cargo_.Buttons(bindings, low, high, {StringKey("trade.take"), StringKey("trade.give")},
                                          change > 0 ? std::to_string(change) + " " + bindings.Text("transfer.cwt") : std::string(), false, std::string(), move);
            if (!primary.is_null())
            {
                view["primary"] = std::move(primary);
                view["actions"] = Json::array();
            }
        }
    }

    // The crew of both ships: our number moves by the game's arrows, one man per press, sent when the player
    // confirms the number. Its ends are where the game's TakeCrew / GiveCrew stop (the giving side's men, the
    // receiving ship's MaxCrew, the receiving hold's free space); the marks on the slider are each ship's MinCrew,
    // the least crew it sails with (characterUtilite.c GetMinCrewQuantity, through ANDROID_TRANSFER).
    Json CrewView(const Admission &admitted, Bindings &bindings, const std::string &ours, const std::string &theirs)
    {
        const Widget *take = admitted.One("CREW_LEFT"), *give = admitted.One("CREW_RIGHT");
        const int our = Number(admitted.Label("CREW_QTY")), their = Number(admitted.Label("CREW_QTY2"));
        crew_limits_ = CrewLimits(admitted, bindings, our, their);
        const Limits &limits = crew_limits_;
        crew_.Follow("crew", our, their, bindings);
        crew_.Bind(bindings, "crew", limits.low, limits.high, std::string());
        bindings.Ignore("crew", "row");
        Json q{{"value", crew_.Value(limits.low, limits.high)},
               {"min", limits.low},
               {"max", limits.high},
               {"zones", true},
               {"origin", our},
               {"lessLabel", bindings.Text("trade.give")},
               {"moreLabel", bindings.Text("trade.take")},
               {"label", ours},
               {"unit", bindings.Text("transfer.men")},
               {"summary", "-"},
               {"steps", Json::array({1, 10})},
               {"marks", limits.marks},
               {"disabled", !take || !give || limits.low == limits.high}};
        // Each ship's crew with its MinCrew under it, named like the slider's mark.
        const auto cell = [&bindings](int crew, int minimum) {
            Json cell{{"text", std::to_string(crew)}};
            if (minimum > 0)
                cell["sub"] = Fill(bindings.Text("transfer.min_mark"), {std::to_string(minimum)});
            return cell;
        };
        Json row{{"id", "0"}, {"name", bindings.Text("transfer.sailors")}, {"image", Json{{"kind", "icon"}, {"icon", "crew"}}},
                 {"cells", Json::array({cell(our, limits.min_ours), cell(their, limits.min_theirs)})}, {"selected", true}, {"q", q}};
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

    struct Limits
    {
        int low = 0, high = 0;          // our crew: the least and the most the game's arrows reach
        int min_ours = 0, min_theirs = 0; // MinCrew of each ship
        Json marks = Json::array();     // [{at, label}] on our crew's scale
    };
    Limits CrewLimits(const Admission &admitted, Bindings &bindings, int our, int their)
    {
        Limits limits;
        const Widget *marker = admitted.One("ANDROID_TRANSFER");
        const std::string min_ours = Context(marker, "crewMin"), max_ours = Context(marker, "crewMax");
        const std::string min_theirs = Context(marker, "crewMin2"), max_theirs = Context(marker, "crewMax2");
        int take = their, give = our;
        if (min_ours.empty() || max_ours.empty() || min_theirs.empty() || max_theirs.empty())
        {
            // An engine without the limits: the slider spans both crews, the game cuts what it cannot take.
            if (limits_reported_ != admitted.model->opening)
                bindings.Error("transfer_main: ANDROID_TRANSFER carries no crew limits (crewMin, crewMax, crewMin2, crewMax2): the crew slider has no marks");
            limits_reported_ = admitted.model->opening;
        }
        else
        {
            limits.min_ours = std::atoi(min_ours.c_str());
            limits.min_theirs = std::atoi(min_theirs.c_str());
            take = std::min(take, std::max(0, std::atoi(max_ours.c_str()) - our));
            give = std::min(give, std::max(0, std::atoi(max_theirs.c_str()) - their));
        }
        // TakeCrew / GiveCrew also stop at the receiving hold's free space (GetCargoFreeSpace)
        if (const int free = FreeSpace(admitted, "TABLE_OTHER"); free >= 0)
            take = std::min(take, free);
        if (const int free = FreeSpace(admitted, "TABLE_OTHER2"); free >= 0)
            give = std::min(give, free);
        limits.low = our - give;
        limits.high = our + take;
        // Our MinCrew where our crew is that many; theirs where what we leave them is their MinCrew.
        const auto mark = [&](int at, int minimum) {
            if (minimum > 0 && at >= limits.low && at <= limits.high)
                limits.marks.push_back(Json{{"at", at}, {"label", Fill(bindings.Text("transfer.min_mark"), {std::to_string(minimum)})}});
        };
        mark(limits.min_ours, limits.min_ours);
        mark(our + their - limits.min_theirs, limits.min_theirs);
        return limits;
    }

    // The crew's «Взять / Отдать N» in the crew window's bar while its number is changed.
    void CrewDeal(const Admission &admitted, Bindings &bindings, Json &view)
    {
        const Widget *take = admitted.One("CREW_LEFT"), *give = admitted.One("CREW_RIGHT");
        if (!take || !give)
            return;
        const Limits &limits = crew_limits_; // CrewView's, of this build
        Json primary = crew_.Buttons(bindings, limits.low, limits.high, {StringKey("trade.take"), StringKey("trade.give")}, std::string(),
                                     !take->Offers("press") || !give->Offers("press"), std::string(), [&bindings, take, give](int delta) {
                                         for (int i = 0; i < std::abs(delta); ++i)
                                             bindings.Send(delta > 0 ? *take : *give, "press");
                                     });
        if (!primary.is_null())
        {
            view["primary"] = std::move(primary);
            view["actions"] = Json::array();
        }
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
            const int from = hire_.From(hired);
            hire_.Sent(hired);
            for (int i = 0; i < std::abs(wanted - from); ++i)
                bindings.Send(wanted > from ? *more : *less, "press");
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
                             {"empty", people.empty() ? bindings.Text("yard.no_captains") : std::string()},
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
        int base = -1, expected = -1; // presses already sent from the game's `base` towards `expected`

        // Where new presses count from: while the game still shows the number the last
        // presses started from, they are on their way and count as done.
        int From(int model) const
        {
            return model == base && expected >= 0 ? expected : model;
        }
        void Sent(int model)
        {
            base = model;
            expected = send;
            send = -1;
        }
    };
    std::unique_ptr<Screen> goods_;
    AmountDraft crew_, cargo_; // the numbers the player sets for the crew and the cargo row, until confirmed
    Limits crew_limits_;       // the crew slider's ends and marks, of the last build
    uint64_t limits_reported_ = 0; // the opening whose missing crew limits were reported
    Amount hire_;              // the defectors' number: the game's own window holds it until its OK
    bool ships_open_ = false; // the «Корабли» tab of ours
    int quick_row_ = -1;    // the cargo row (the game's index: its rows carry no identity) whose whole stock is taken once the game selects it
    int hire_max_ = 0;
    bool hire_seen_ = false;
};

} // namespace

std::unique_ptr<Screen> CreateTransferScreen()
{
    return std::make_unique<TransferScreen>();
}

} // namespace storm::ui::screens
