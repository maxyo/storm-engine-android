// Hero section «Корабль» (interface ship; mockups V2-Hero-Ship, -ShipCrew, -ShipCargo,
// -ShipMorale, -ShipShares). Fills the pack screen <s-ship>: three tabs of our own over the one
// stock screen. «Обзор»: the ship card (name with the rename quill, type and class, captain,
// hull / crew / sails bars, «Обмен» on a companion's ship) and the game's TABLE_OTHER figures
// on parchment; the cannons row opens the game's CANNONS_WINDOW as a sheet. «Команда»: crew
// size, the experience of the three crew groups and the morale as bars, food and upkeep,
// «Доли команды» (PARTITION_WINDOW as a parchment dialog) and «Поднять мораль» (the game's
// confirmation window as our dialog). «Трюм»: the goods; the open row is the game's
// drop-overboard window (QTY_WINDOW) as the quantity control, «За борт N» held to confirm.
//
// The header's subject is SHIPS_SCROLL, the ships of the squadron. Facts the stock screen only
// draws in status bars or windows (crew experience, morale, prices, the shares, guns per side)
// come from the interface adapter android_hero.c (ANDROID_SHIP, ANDROID_SHARES). Actions that
// take several engine steps (the exchange question, guns moved one arrow press at a time) are
// intents walked one step per model revision, as in hero_character.cpp.
#include "hero.h"

#include <algorithm>
#include <cstdlib>
#include <map>

namespace storm::ui::screens
{

using namespace hero;

namespace
{

constexpr NodeRule kNodes[] = {
    {"EDIT_BOX", "edit", true},
    {"HERO_NAME", "text", true},
    {"ANDROID_SHIP", "fact"}, // the adapter's facts: CheckAdapter reports them missing
    {"ANDROID_SHARES", "fact"},
    {"TITLE", "heading"},
    {"MAIN_BACK", "image"},
    {"EXIT_BTN", "button"},
    {"I_*", "button"},       // the section buttons: the header icons
    {"STR_BUTTONS", "text"}, // their captions
    {"SHIPS_SCROLL", "choice"},
    {"UP_BUTTON", "button"},
    {"DOWN_BUTTON", "button"},
    {"SHIP_BIG_PICTURE", "image"},
    {"MAIN_CHARACTER_PICTURE", "image"}, // the captain is named on the card
    {"HERO_RANK", "text"},
    {"SHIP_RANK", "text"},
    {"TABLE_OTHER", "choice"},
    {"TABLE_LIST", "choice"},
    {"TABLE_CREW", "choice"},
    {"CREW_PICTURE", "image"},
    {"CREW_QTY", "text"}, // TABLE_OTHER's crew row carries it with the limits
    {"CREW_MORALE_TEXT", "text"},
    {"CREW_MORALE_PIC", "image"},
    {"CREW_MORALE_BUTTON", "button"},
    {"CREW_PARTITION", "button"},
    {"FOOD", "text"},
    {"FOOD_SHIP", "text"},
    {"MONEY_SHIP", "text"},
    {"Money_PIC", "image"},
    {"Money_TEXT", "text"},
    // The game's windows: drawn as our dialogs and sheets.
    {"QTY_*", "text|button|edit|image"},
    {"CANNONS_*", "text|button|image|choice"},
    {"PARTITION_*", "text|button"},
    {"REMOVE_*", "text|button"},
    {"OFFICER*", "text"}, // RPG_WINDOW (right click on the portrait): never opened from here
    {"TABLE_SMALLSKILL", "choice"},
    {"TABLE_SMALLOTHER", "choice"},
};

constexpr double kStepTimeout = 3.0; // an intent whose step got no answer from the engine is dropped

// The guns of each side in the game's cannons window.
struct Side
{
    const char *id;    // c-quantity id, ANDROID_SHIP field with the most guns
    char letter;       // CANNONS_QTY_<letter>, CANNONS_LEFT_<letter>, CANNONS_RIGHT_<letter>
    const char *label; // string key
};
constexpr Side kSides[] = {
    {"bow", 'F', StringKey("yard.bow")},
    {"port", 'L', StringKey("yard.port")},
    {"starboard", 'R', StringKey("yard.starboard")},
    {"stern", 'B', StringKey("yard.stern")},
};

// "13.07 / 15.20" → {13, 15}; the second is -1 without a slash.
std::pair<int, int> Pair(const std::string &text)
{
    const size_t slash = text.find('/');
    return {Number(text), slash == std::string::npos ? -1 : Number(text.substr(slash + 1))};
}

int Percent(int value, int maximum)
{
    return maximum > 0 ? std::clamp(value * 100 / maximum, 0, 100) : 0;
}

enum class Tab
{
    Overview,
    Crew,
    Cargo
};

class ShipScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-ship";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        // Handlers run after Build returns: keep the admission (pointers into the router's model).
        admission_ = admitted;
        a_ = &admission_;
        revision_ = admitted.model->revision;
        CheckAdapter(admitted, bindings, "ANDROID_SHIP", checked_opening_);
        ship_ = admitted.One("ANDROID_SHIP");
        const Window before = window_;
        window_ = admitted.One("QTY_OK_BUTTON") || admitted.One("QTY_CANCEL_BUTTON")                   ? Window::Quantity
                  : admitted.One("CANNONS_OK") || admitted.One("CANNONS_CANCEL")                       ? Window::Cannons
                  : admitted.One("PARTITION_OK") || admitted.One("PARTITION_CANCEL")                   ? Window::Shares
                  : admitted.One("REMOVE_ACCEPT_OFFICER") || admitted.One("REMOVE_CANCEL_OFFICER")     ? Window::Question
                                                                                                       : Window::None;
        if (before == Window::Question && window_ != Window::Question)
            question_ = Question::None; // answered: the next question is asked by whoever opens it
        if (window_ != Window::Quantity)
            open_.name.clear();
        if (window_ == Window::None && !reopen_.empty())
        {
            for (const Widget *row : admitted.All("TABLE_LIST"))
                if (row->row_id == reopen_ && row->Offers("activate"))
                    bindings.Send(*row, "activate", row->row_id);
            reopen_.clear();
        }
        Walk(bindings);
        if (window_ == Window::None && intent_.kind == Intent::None)
            OpenOnSubject(bindings);

        Json view = Json::object();
        view["title"] = Trim(admitted.Label("TITLE"));
        Json tabs = Json::array();
        const std::pair<Tab, const char *> tab_keys[] = {
            {Tab::Overview, "ship.tab_overview"}, {Tab::Crew, "ship.tab_crew"}, {Tab::Cargo, "ship.tab_cargo"}};
        for (const auto &[tab, key] : tab_keys)
            tabs.push_back(Json{{"id", TabId(tab)}, {"label", bindings.Text(key)}, {"selected", tab == tab_}});
        view["tabs"] = tabs;
        view["tab"] = TabId(tab_);
        bindings.On("header-tabs", "select", [this, &bindings](const Event &event) {
            const Tab next = event.value == "crew" ? Tab::Crew : event.value == "cargo" ? Tab::Cargo : Tab::Overview;
            if (next == tab_)
                return;
            tab_ = next;
            sent_revision_ = 0; // a player action: its first step may go at once
            if (window_ != Window::None)
                CloseWindow(bindings);
            bindings.Rebuild();
        });
        view["subject"] = SubjectView(bindings);
        BindSections(bindings);

        if (tab_ == Tab::Overview)
            OverviewView(view, bindings);
        else if (tab_ == Tab::Crew)
            CrewView(view, bindings);
        else
            CargoView(view, bindings);
        view["dialog"] = DialogView(bindings);
        view["sheet"] = window_ == Window::Cannons ? CannonsView(bindings) : Json(nullptr);

        bindings.On("header-back", "", [this, &bindings](const Event &) {
            sent_revision_ = 0;
            if (window_ != Window::None)
                CloseWindow(bindings);
            else
                bindings.Back(a_->One("EXIT_BTN"));
        });
        return view;
    }

    // Android back closes the game's window first (its dialogs are modals; the quantity row and
    // the exchange question are not).
    bool OwnsBack() const override
    {
        return window_ != Window::None;
    }
    void Back(Bindings &bindings) override
    {
        sent_revision_ = 0;
        CloseWindow(bindings);
    }

  private:
    enum class Window
    {
        None,
        Quantity, // QTY_WINDOW: drop goods overboard
        Cannons,  // CANNONS_WINDOW
        Shares,   // PARTITION_WINDOW
        Question  // REMOVE_OFFICER_WINDOW: the morale price or the exchange question
    };
    // Who opened the question window (it serves both).
    enum class Question
    {
        None,
        Morale,
        Exchange
    };
    struct Intent
    {
        enum Kind
        {
            None,
            Exchange, // activate the current ship, answer the question window
            Guns,     // row = side id, target = guns wanted on it
            Section,  // row = section
        } kind = None;
        std::string row;
        int target = 0;
        int last = -1; // the guns the last arrow press started from
        double since = 0.0;
    };
    // The good whose drop window is open: the game takes the amount out of the hold while it
    // is being set, so the row may leave TABLE_LIST (everything set to go) and its figures
    // change; the row keeps what it had when the window opened.
    struct Open
    {
        std::string name;
        int index = 0;      // position in the list
        std::string weight; // weight cell
        std::string meta;
        Json image;
        std::string capacity; // TABLE_OTHER Capacity before any amount was set
    };

    Admission admission_;
    const Admission *a_ = nullptr;
    const Widget *ship_ = nullptr;
    uint64_t revision_ = 0;
    uint64_t sent_revision_ = 0;
    uint64_t checked_opening_ = 0;
    SubjectSwitch subjects_;
    Tab tab_ = Tab::Overview;
    Window window_ = Window::None;
    Question question_ = Question::None;
    Intent intent_;
    Open open_;
    std::string reopen_; // the good to open once the drop window of another one has closed
    // The amount the player set is preferred over the game's for a moment (the drag runs ahead).
    int local_keep_ = -1;
    double local_at_ = 0.0;

    static const char *TabId(Tab tab)
    {
        return tab == Tab::Crew ? "crew" : tab == Tab::Cargo ? "cargo" : "overview";
    }

    bool Step(Bindings &bindings, const Widget *widget, const std::string &verb, const std::string &value = {})
    {
        if (!widget || sent_revision_ == revision_)
            return false;
        sent_revision_ = revision_;
        bindings.Send(*widget, verb, value);
        return true;
    }

    const Widget *Other(const char *id) const
    {
        for (const Widget *row : a_->All("TABLE_OTHER"))
            if (row->row_id == id)
                return row;
        return nullptr;
    }
    std::string OtherValue(const char *id) const
    {
        const Widget *row = Other(id);
        return row ? Trim(row->CellText(2)) : std::string();
    }
    std::string OtherLabel(const char *id) const
    {
        const Widget *row = Other(id);
        return row ? Trim(row->CellText(1)) : std::string();
    }
    const Widget *CurrentShip() const
    {
        for (const Widget *row : a_->All("SHIPS_SCROLL"))
            if (row->value == "1")
                return row;
        return nullptr;
    }
    bool Pressable(const char *node) const
    {
        const Widget *widget = a_->One(node);
        return widget && widget->Offers("press");
    }

    void Start(Bindings &bindings, Intent intent)
    {
        sent_revision_ = 0; // a player action: its first step may go at once
        intent.since = bindings.Now();
        intent_ = std::move(intent);
        Walk(bindings);
        bindings.RebuildAfter(kStepTimeout);
    }

    void Drop(Bindings &bindings, const std::string &why)
    {
        if (!why.empty())
            bindings.Error("ship: " + why);
        intent_ = {};
    }

    void CloseWindow(Bindings &bindings)
    {
        const char *const cancels[] = {window_ == Window::Quantity  ? "QTY_CANCEL_BUTTON"
                                       : window_ == Window::Cannons ? "CANNONS_CANCEL"
                                       : window_ == Window::Shares  ? "PARTITION_CANCEL"
                                                                    : "REMOVE_CANCEL_OFFICER",
                                       window_ == Window::Cannons  ? "CANNONS_EXIT_BTN"
                                       : window_ == Window::Shares ? "PARTITION_EXIT_BTN"
                                                                   : "REMOVE_EXIT_BTN"};
        for (const char *node : cancels)
            if (const Widget *button = a_->One(node); button && button->Offers("press"))
            {
                Step(bindings, button, "press");
                return;
            }
    }

    // The next engine step of the intent, from the state of this model.
    void Walk(Bindings &bindings)
    {
        if (intent_.kind == Intent::None)
            return;
        if (bindings.Now() - intent_.since > kStepTimeout)
            return Drop(bindings, "the engine did not answer a step (" + intent_.row + ")");
        switch (intent_.kind)
        {
        case Intent::Exchange: {
            if (window_ == Window::Question)
            {
                // The game asks; the player already chose «Обмен». Where the game refuses, its
                // reason is the toast and the window closes.
                const Widget *accept = a_->One("REMOVE_ACCEPT_OFFICER");
                if (accept && accept->Offers("press"))
                {
                    if (Step(bindings, accept, "press"))
                        intent_ = {};
                    return;
                }
                const std::string reason = OneLine(a_->Label("REMOVE_WINDOW_TEXT"));
                if (Pressable("REMOVE_CANCEL_OFFICER") && Step(bindings, a_->One("REMOVE_CANCEL_OFFICER"), "press"))
                {
                    bindings.Toast(reason);
                    intent_ = {};
                }
                return;
            }
            if (window_ != Window::None)
                return CloseWindow(bindings);
            const Widget *row = CurrentShip();
            if (!row || !row->Offers("activate"))
                return Drop(bindings, "the current ship offers no exchange");
            question_ = Question::Exchange;
            Step(bindings, row, "activate", row->row_id);
            return;
        }
        case Intent::Guns: {
            if (window_ != Window::Cannons)
                return Drop(bindings, {});
            const Side *side = nullptr;
            for (const Side &s : kSides)
                if (intent_.row == s.id)
                    side = &s;
            if (!side)
                return Drop(bindings, "no cannon side " + intent_.row);
            const int now = Number(a_->Label(std::string("CANNONS_QTY_") + side->letter));
            if (now == intent_.target || now == intent_.last)
                return Drop(bindings, {}); // there, or the game took no more (limit, no guns in the hold)
            const std::string arrow = std::string(now < intent_.target ? "CANNONS_RIGHT_" : "CANNONS_LEFT_") + side->letter;
            const Widget *button = a_->One(arrow);
            if (!button || !button->Offers("press"))
                return Drop(bindings, {});
            if (Step(bindings, button, "press"))
                intent_.last = now;
            return;
        }
        case Intent::Section: {
            if (window_ != Window::None)
                return CloseWindow(bindings);
            for (const Widget *widget : a_->known)
                if (widget->type == "button" && SectionOf(*widget) == intent_.row && widget->Offers("press"))
                {
                    if (Step(bindings, widget, "press"))
                        intent_ = {};
                    return;
                }
            return Drop(bindings, "section " + intent_.row + " cannot be opened");
        }
        case Intent::None:
            return;
        }
    }

    void BindSections(Bindings &bindings)
    {
        bindings.On("header", "section", [this, &bindings](const Event &event) {
            if (event.value == "ship")
                return;
            Intent intent;
            intent.kind = Intent::Section;
            intent.row = event.value;
            Start(bindings, intent);
        });
    }

    // A tap on a world-map squadron ship opened this screen for that ship (hud world_map.cpp):
    // the squadron switch selects it, as the player's choice does. SHIPS_SCROLL lists the
    // squadron in its companion order, flagship first, as the map's ship signs do.
    void OpenOnSubject(Bindings &bindings)
    {
        const auto request = bindings.TakeSubject("ship");
        if (!request)
            return;
        const std::vector<const Widget *> rows = a_->All("SHIPS_SCROLL");
        if (request->position < 0 || static_cast<size_t>(request->position) >= std::max<size_t>(rows.size(), 1))
            return bindings.Error("ship: the HUD's tap names squadron ship " + std::to_string(request->position) + " of " +
                                  std::to_string(rows.size()) + "; the screen's own ship stays");
        if (rows.empty())
            return; // the flagship alone: the screen shows it
        const Widget *row = rows[static_cast<size_t>(request->position)];
        if (row->value != "1" && row->Offers("select"))
        {
            sent_revision_ = 0;
            Step(bindings, row, "select", row->row_id);
        }
    }

    // The subject switch over SHIPS_SCROLL, the ships of the squadron, with their list; null with a
    // single ship.
    Json SubjectView(Bindings &bindings)
    {
        const std::vector<const Widget *> rows = a_->All("SHIPS_SCROLL");
        size_t current = 0;
        std::vector<SubjectSwitch::Choice> choices;
        for (size_t i = 0; rows.size() > 1 && i < rows.size(); ++i)
        {
            if (rows[i]->value == "1")
                current = i;
            choices.push_back({rows[i]->label, {}, nullptr, "ship"});
        }
        return subjects_.View(bindings, choices, current, bindings.Text("ship.squadron"), [this, &bindings, rows](size_t index) {
            if (!rows[index]->Offers("select"))
                return;
            sent_revision_ = 0;
            if (window_ != Window::None)
                return CloseWindow(bindings);
            Step(bindings, rows[index], "select", rows[index]->row_id);
        });
    }

    // ---- «Обзор» ----

    void OverviewView(Json &view, Bindings &bindings)
    {
        const bool has_ship = !Field(ship_, "type").empty();
        Json card = Json::object();
        const Widget *name = a_->One("EDIT_BOX");
        card["title"] = name ? Trim(name->value) : std::string();
        const std::string rank = Trim(a_->Label("SHIP_RANK"));
        card["sub"] = Field(ship_, "type") + (rank.empty() ? std::string() : " \xC2\xB7 " + bindings.Text("yard.class") + " " + rank);
        card["captain"] = bindings.Text("ship.captain") + " " + Trim(a_->Label("HERO_NAME")) +
                          (Field(ship_, "hero") == "1" ? std::string() : " \xC2\xB7 " + bindings.Text("ship.companion"));
        const Widget *picture = a_->One("SHIP_BIG_PICTURE");
        card["image"] = has_ship ? PictureOf(picture ? picture->FirstImage() : nullptr) : Json(nullptr);
        if (has_ship)
        {
            const auto [hull, hull_max] = Pair(OtherValue("Hull"));
            const auto [sails, sails_max] = Pair(OtherValue("Sails"));
            card["hull"] = Json{{"value", Percent(hull, hull_max)}, {"text", std::to_string(Percent(hull, hull_max)) + "%"}};
            card["sails"] = Json{{"value", Percent(sails, sails_max)}, {"text", std::to_string(Percent(sails, sails_max)) + "%"}};
            const Crew crew = CrewOf();
            card["crew"] = Json{{"value", Percent(crew.count, crew.full)}, {"text", std::to_string(crew.count)}, {"mark", Percent(crew.least, crew.full)}};
        }
        // The quill takes the new name where the game lets the field take focus (CheckForRename).
        card["rename"] = has_ship && Field(ship_, "rename") == "1" && name && name->Offers("edit");
        if (name)
        {
            const Widget *field = name;
            bindings.On("ship-rename", "", [this, &bindings, field](const Event &event) {
                bindings.AskText(event, Trim(field->value), bindings.Text("ship.rename"), false, [this, &bindings, field](const std::string &text) {
                    const std::string trimmed = Trim(text);
                    if (!trimmed.empty() && trimmed != Trim(field->value))
                    {
                        sent_revision_ = 0;
                        Step(bindings, field, "edit", trimmed);
                    }
                });
            });
        }
        // «Обмен»: a companion's ship while the hero has one (ShipChange); the game's refusal
        // comes back as a toast from the question window.
        if (Field(ship_, "exchange") == "1")
        {
            card["exchange"] = Json{{"id", "ship-exchange"}, {"label", bindings.Text("ship.exchange")}, {"busy", intent_.kind == Intent::Exchange}};
            bindings.On("ship-exchange", "", [this, &bindings](const Event &) {
                Intent intent;
                intent.kind = Intent::Exchange;
                Start(bindings, intent);
            });
        }
        else
            card["exchange"] = nullptr;
        view["card"] = card;

        // The game's figures in the mockup's words: rounded, current of best.
        Json left = Json::array(), right = Json::array();
        const std::string of = " " + bindings.Text("common.of") + " ";
        const auto row = [](const char *icon, const std::string &label, const std::string &value, const std::string &sub = {}) {
            Json first{{"text", label}, {"icon", icon}, {"lines", 2}};
            if (!sub.empty())
                first["sub"] = sub;
            return Json{{"cells", Json::array({first, Json{{"text", value}, {"bold", true}}})}};
        };
        if (has_ship)
        {
            const auto speed = Pair(OtherValue("Speed"));
            const std::string knots = " " + bindings.Text("common.knots_short");
            left.push_back(row("speed", OtherLabel("Speed"), std::to_string(speed.first) + knots + (speed.second < 0 ? "" : of + std::to_string(speed.second))));
            const auto turn = Pair(OtherValue("Maneuver"));
            left.push_back(row("maneuver", OtherLabel("Maneuver"), std::to_string(turn.first) + (turn.second < 0 ? "" : of + std::to_string(turn.second))));
            left.push_back(row("upwind", OtherLabel("AgainstWind"), std::to_string(Number(OtherValue("AgainstWind"))) + "\xC2\xB0"));
            const auto load = Pair(OtherValue("Capacity"));
            left.push_back(row("capacity", OtherLabel("Capacity"), std::to_string(load.first) + of + std::to_string(load.second) + " " + bindings.Text("transfer.cwt")));
            const Crew crew = CrewOf();
            left.push_back(row("crew", bindings.Text("ship.min_crew"), std::to_string(crew.least) + " \xC2\xB7 " + bindings.Text("ship.full_crew") + " " + std::to_string(crew.full)));
            // Guns: "6 фунт / 12" (calibre / count) of the fitted type; the row opens the window.
            const std::string guns = OtherValue("CannonType");
            const size_t slash = guns.find('/');
            const std::string guns_text = slash == std::string::npos ? guns
                                                                      : Trim(guns.substr(slash + 1)) + " \xC3\x97 " + Trim(guns.substr(0, slash));
            const Widget *guns_row = Other("CannonType");
            right.push_back(row("cannon", OtherLabel("CannonType"), guns_text + (guns_row && guns_row->Offers("activate") ? "  \xE2\x80\xBA" : ""), bindings.Text("ship.by_sides")));
            const std::string calibre = OtherValue("sCannons");
            right.push_back(row("caliber", bindings.Text("ship.caliber"), bindings.Text("ship.up_to") + " " + Trim(calibre.substr(0, calibre.find('/')))));
            const std::string upkeep = AfterColon(OneLine(a_->Label("MONEY_SHIP")));
            if (!upkeep.empty())
                right.push_back(row("money", bindings.Text("ship.upkeep"), Thousands(upkeep), bindings.Text("hero.per_month")));
        }
        view["left"] = left;
        view["right"] = right;
        bindings.Ignore("stats-left", "row");
        bindings.On("stats-right", "row", [this, &bindings](const Event &event) {
            if (event.value != "0")
                return;
            const Widget *guns = Other("CannonType");
            if (!guns || !guns->Offers("activate"))
                return;
            sent_revision_ = 0;
            if (window_ != Window::None)
                return CloseWindow(bindings);
            Step(bindings, guns, "activate", guns->row_id);
        });
        view["empty"] = has_ship ? "" : bindings.Text("ship.no_ship");
    }

    struct Crew
    {
        int count = 0, least = 0, full = 0;
    };
    // TABLE_OTHER's crew row: "40 : 9 / 40" (crew : least / full).
    Crew CrewOf() const
    {
        const std::string text = OtherValue("Crew");
        Crew crew;
        crew.count = Number(text);
        const size_t colon = text.find(':');
        if (colon != std::string::npos)
        {
            const auto [least, full] = Pair(text.substr(colon + 1));
            crew.least = least;
            crew.full = std::max(full, 0);
        }
        return crew;
    }

    // ---- «Команда» ----

    void CrewView(Json &view, Bindings &bindings)
    {
        const bool has_ship = !Field(ship_, "type").empty();
        const Crew crew = CrewOf();
        Json c = Json::object();
        c["count"] = has_ship ? std::to_string(crew.count) : "";
        c["limits"] = bindings.Text("common.of") + " " + std::to_string(crew.full) + " \xC2\xB7 " + bindings.Text("ship.min_short") + " " + std::to_string(crew.least);
        c["value"] = Percent(crew.count, crew.full);
        c["mark"] = Percent(crew.least, crew.full);
        c.update(CrewQuality(*a_, bindings, "TABLE_CREW", "CREW_MORALE_TEXT", ship_));
        view["crew"] = has_ship ? c : Json(nullptr);

        // Food and upkeep: the game's lines.
        Json facts = Json::array();
        const std::string food_ship = OneLine(a_->Label("FOOD_SHIP"));
        if (!food_ship.empty())
            facts.push_back(Json{{"icon", "food"}, {"title", food_ship}, {"meta", OneLine(a_->Label("FOOD"))}});
        const std::string money = OneLine(a_->Label("MONEY_SHIP"));
        if (!money.empty())
        {
            const size_t colon = money.find(':');
            facts.push_back(Json{{"icon", "money"},
                                 {"title", colon == std::string::npos ? money : AfterColon(money)},
                                 {"meta", colon == std::string::npos ? std::string() : Trim(money.substr(0, colon))}});
        }
        view["facts"] = facts;

        // «Доли команды» (the hero's ship only: the game shows the button there) and «Поднять мораль».
        Json actions = Json::array();
        if (const Widget *shares = a_->One("CREW_PARTITION"))
        {
            actions.push_back(Json{{"id", "crew-shares"}, {"label", bindings.Text("ship.shares")}, {"variant", "secondary"},
                                   {"disabled", !shares->Offers("press")}, {"busy", bindings.Pending("crew-shares")}});
            bindings.Forward("crew-shares", *shares, "press");
        }
        if (const Widget *morale = a_->One("CREW_MORALE_BUTTON"); morale && has_ship)
        {
            const bool ready = morale->Offers("press");
            const std::string reason = ready                                    ? std::string()
                                       : Number(Field(ship_, "morale")) >= 100 ? bindings.Text("ship.morale_full")
                                                                               : bindings.Text("ship.no_crew");
            actions.push_back(Json{{"id", "crew-morale"}, {"label", bindings.Text("ship.raise_morale")}, {"variant", "primary"},
                                   {"sub", ready ? Thousands(Field(ship_, "moraleprice")) + " " + bindings.Text("ship.money") : std::string()},
                                   {"disabled", !ready}, {"reason", reason}, {"busy", bindings.Pending("crew-morale")}});
            const Widget *button = morale;
            bindings.On("crew-morale", "", [this, &bindings, button](const Event &) {
                sent_revision_ = 0;
                question_ = Question::Morale;
                Step(bindings, button, "press");
            });
        }
        view["crewActions"] = actions;
    }

    // ---- «Трюм» ----

    void CargoView(Json &view, Bindings &bindings)
    {
        const std::vector<const Widget *> goods = a_->All("TABLE_LIST");
        const Widget *first = goods.empty() ? nullptr : goods.front();
        const auto header = [first](int column) {
            return first && static_cast<size_t>(column) < first->column_headers.size() ? OneLine(first->column_headers[static_cast<size_t>(column)])
                                                                                        : std::string();
        };
        const bool quantity = window_ == Window::Quantity;
        const std::string caption = quantity ? OneLine(a_->Label("QTY_CAPTION")) : std::string();
        const Widget *edit = a_->One("QTY_EDIT");
        const int dropping = quantity ? std::max(Number(edit ? edit->value : std::string()), 0) : 0;
        const int left = quantity ? Number(a_->Label("QTY_INFO_SHIP_QTY")) : 0;
        const int total = left + dropping;

        // Rows; TABLE_LIST: name, quantity, weight, pack, pack weight.
        Json rows = Json::array();
        std::vector<const Widget *> shown; // TABLE_LIST row of each list row (null: the open good that left the table)
        const auto meta = [&header](const Widget &row) {
            const std::string pack = Trim(row.CellText(3));
            return pack.empty() || pack == "1" ? std::string() : header(3) + " " + pack + " \xC2\xB7 " + header(4) + " " + Trim(row.CellText(4));
        };
        bool open_listed = false;
        for (const Widget *row : goods)
        {
            const std::string name = Trim(row->CellText(0));
            const bool open = quantity && name == caption;
            if (quantity && !open_.name.empty() && !open_listed && !open && static_cast<int>(rows.size()) == open_.index && open_.name == caption)
            {
                rows.push_back(OpenRow(bindings, total, left, dropping));
                shown.push_back(nullptr);
                open_listed = true;
            }
            if (open && open_.name != caption)
            {
                // The window just opened: keep what the row shows now.
                open_.name = caption;
                open_.index = static_cast<int>(rows.size());
                open_.weight = Trim(row->CellText(2));
                open_.meta = meta(*row);
                open_.image = PictureOf(row->cells.empty() || row->cells[0].images.empty() ? row->FirstImage() : &row->cells[0].images[0]);
                open_.capacity = OtherValue("Capacity");
            }
            if (open)
            {
                rows.push_back(OpenRow(bindings, total, left, dropping));
                open_listed = true;
            }
            else
                rows.push_back(Json{{"id", std::to_string(rows.size())},
                                    {"name", name},
                                    {"meta", meta(*row)},
                                    {"image", PictureOf(row->cells.empty() || row->cells[0].images.empty() ? row->FirstImage() : &row->cells[0].images[0])},
                                    {"cells", Json::array({Trim(row->CellText(1)), Trim(row->CellText(2))})},
                                    {"disabled", !quantity && !row->Offers("activate")},
                                    {"reason", row->unavailable_reason}});
            shown.push_back(open ? nullptr : row);
        }
        if (quantity && !open_listed && open_.name == caption && !caption.empty())
        {
            rows.push_back(OpenRow(bindings, total, left, dropping));
            shown.push_back(nullptr);
        }
        view["list"] = Json{{"label", bindings.Text("ship.goods")},
                            {"cols", Json::array({Json{{"label", header(1)}, {"icon", "capacity"}, {"w", 88}}, Json{{"label", header(2)}, {"icon", "weight"}, {"w", 80}}})},
                            {"rows", rows}};
        view["empty"] = goods.empty() && !quantity ? bindings.Text("ship.no_cargo") : "";

        // A tap on a row opens its drop window; on the open row it closes it.
        bindings.On("goods", "row", [this, &bindings, shown](const Event &event) {
            const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
            if (index >= shown.size())
                return;
            const Widget *row = shown[index];
            sent_revision_ = 0;
            local_keep_ = -1;
            if (window_ == Window::Quantity)
            {
                // The window locks the table: the other good opens once the game closed it.
                if (const Widget *cancel = a_->One("QTY_CANCEL_BUTTON"); cancel && cancel->Offers("press"))
                    bindings.Send(*cancel, "press");
                reopen_ = row ? row->row_id : std::string();
                return;
            }
            else if (window_ != Window::None)
                return CloseWindow(bindings);
            if (row && row->Offers("activate"))
                bindings.Send(*row, "activate", row->row_id);
        });

        // The amount: the quantity control shows what stays in the hold; the game's edit box
        // takes the amount to drop.
        if (quantity && edit)
        {
            const auto set = [this, &bindings, edit, total](const Event &event) {
                const int keep = std::clamp(std::atoi(event.value.c_str()), 0, total);
                local_keep_ = keep;
                local_at_ = bindings.Now();
                bindings.SendLatest(*edit, "edit", std::to_string(total - keep));
            };
            bindings.On("goods", "change", set);
            bindings.On("goods", "commit", set);
        }

        // The deadweight with what the drop takes off, and «За борт N».
        const std::string now = OtherValue("Capacity");
        const auto load = Pair(now);
        Json footer{{"value", Percent(load.first, load.second)}};
        std::string text = std::to_string(load.first) + " " + bindings.Text("common.of") + " " + std::to_string(load.second) + " " + bindings.Text("transfer.cwt");
        if (quantity && dropping > 0 && !open_.capacity.empty())
        {
            const auto before = Pair(open_.capacity);
            footer["value"] = Percent(before.first, before.second);
            footer["after"] = Percent(load.first, load.second);
            text = std::to_string(before.first) + " \xE2\x80\xBA " + text;
        }
        footer["text"] = load.second > 0 ? text : "";
        view["load"] = footer;
        Json actions = Json::array();
        if (quantity)
        {
            if (const Widget *mine = a_->One("QTY_BOMB_BUTTON"))
            {
                actions.push_back(Json{{"id", "goods-mine"}, {"label", Trim(mine->label)}, {"variant", "secondary"},
                                       {"disabled", !mine->Offers("press")}, {"reason", mine->unavailable_reason}, {"busy", bindings.Pending("goods-mine")}});
                bindings.Forward("goods-mine", *mine, "press");
            }
            const Widget *ok = a_->One("QTY_OK_BUTTON");
            const bool ready = ok && ok->Offers("press") && dropping > 0;
            actions.push_back(Json{{"id", "goods-drop"},
                                   {"label", bindings.Text("ship.overboard") + (dropping > 0 ? " " + std::to_string(dropping) : std::string())},
                                   {"variant", "danger"},
                                   {"hold", true},
                                   {"disabled", !ready},
                                   {"reason", ok && !ok->Offers("press") ? ok->unavailable_reason : std::string()},
                                   {"busy", bindings.Pending("goods-drop")}});
            if (ok)
                bindings.Forward("goods-drop", *ok, "press");
        }
        view["cargoActions"] = actions;
    }

    Json OpenRow(Bindings &bindings, int total, int left, int dropping)
    {
        const bool recent = local_keep_ >= 0 && bindings.Now() - local_at_ < 1.0;
        if (recent) // then the game's own amount, also when it answered with another one
            bindings.RebuildAfter(local_at_ + 1.0 - bindings.Now());
        const Widget *edit = a_->One("QTY_EDIT");
        const int keep = recent ? local_keep_ : left;
        Json q{{"value", keep},
               {"min", 0},
               {"max", total},
               {"label", bindings.Text("ship.in_hold")},
               {"summary", dropping > 0 ? "\xE2\x88\x92" + std::to_string(dropping) : std::string()},
               {"all", false},
               {"disabled", !edit || !edit->Offers("edit")}};
        return Json{{"id", "open"},
                    {"name", open_.name},
                    {"meta", open_.meta},
                    {"image", open_.image},
                    {"cells", Json::array({std::to_string(total), open_.weight})},
                    {"selected", true},
                    {"q", q}};
    }

    // ---- the game's windows ----

    Json DialogView(Bindings &bindings)
    {
        const auto close = [this, &bindings](const Event &) {
            sent_revision_ = 0;
            CloseWindow(bindings);
        };
        bindings.On("dialog-close", "", close);
        if (window_ != Window::Shares && (window_ != Window::Question || question_ == Question::Exchange))
            return nullptr; // the exchange question is answered by the intent
        bindings.Modal("dialog", close);
        if (window_ == Window::Shares)
            return SharesView(bindings);
        const Widget *accept = a_->One("REMOVE_ACCEPT_OFFICER");
        Json actions = Json::array();
        actions.push_back(Json{{"id", "dialog-close"}, {"label", bindings.Text("common.cancel")}, {"variant", "quiet"}});
        if (accept)
        {
            const bool morale = question_ == Question::Morale;
            const std::string reason = accept->Offers("press") ? std::string() : morale ? bindings.Text("ship.no_money") : accept->unavailable_reason;
            actions.push_back(Json{{"id", "dialog-accept"}, {"label", morale ? bindings.Text("ship.raise") : Trim(accept->label)}, {"variant", "primary"},
                                   {"disabled", !accept->Offers("press")}, {"reason", reason}, {"busy", bindings.Pending("dialog-accept")}});
            bindings.Forward("dialog-accept", *accept, "press");
        }
        // The morale question in the mockup's words: «Поднять мораль за 340 песо?».
        const std::string title = question_ == Question::Morale
                                      ? bindings.Text("ship.raise_morale_for") + " " + Thousands(Field(ship_, "moraleprice")) + " " + bindings.Text("ship.money") + "?"
                                      : OneLine(a_->Label("REMOVE_WINDOW_CAPTION"));
        return Json{{"kind", "question"},
                    {"title", title},
                    {"text", question_ == Question::Morale ? std::string() : OneLine(a_->Label("REMOVE_WINDOW_TEXT"))},
                    {"actions", actions}};
    }

    Json SharesView(Bindings &bindings)
    {
        const Widget *shares = a_->One("ANDROID_SHARES");
        const auto amount = [&](const char *field) {
            const int value = Number(Field(shares, field));
            return value > 0 ? Thousands(std::to_string(value)) : std::string("\xE2\x80\x94");
        };
        Json part = Json::array({Json{{"k", bindings.Text("ship.share_captain")}, {"v", amount("captain")}},
                                 Json{{"k", bindings.Text("ship.share_officers")}, {"v", amount("officers")}},
                                 Json{{"k", bindings.Text("ship.share_crew")}, {"v", amount("crew")}}});
        Json debts = Json::array({Json{{"k", bindings.Text("ship.debt_crew")}, {"v", amount("month")}},
                                  Json{{"k", bindings.Text("ship.debt_state")}, {"v", amount("state")}}});
        if (Number(Field(shares, "last")) > 0)
            debts.push_back(Json{{"k", bindings.Text("ship.debt_last")}, {"v", amount("last")}});
        const Widget *pay = a_->One("PARTITION_OK");
        Json actions = Json::array();
        if (pay)
        {
            const int sum = Number(Field(shares, "pay"));
            const bool owed = Number(Field(shares, "month")) > 0 || Number(Field(shares, "last")) > 0;
            const std::string reason = pay->Offers("press") ? std::string() : owed ? bindings.Text("ship.no_money") : bindings.Text("ship.nothing_owed");
            actions.push_back(Json{{"id", "shares-pay"},
                                   {"label", bindings.Text("ship.pay") + (sum > 0 ? " " + Thousands(std::to_string(sum)) : std::string())},
                                   {"variant", "primary"},
                                   {"disabled", !pay->Offers("press")},
                                   {"reason", reason},
                                   {"busy", bindings.Pending("shares-pay")}});
            bindings.Forward("shares-pay", *pay, "press");
        }
        const std::string paid = Trim(Field(shares, "paid"));
        return Json{{"kind", "shares"},
                    {"title", bindings.Text("ship.shares")}, // the button's name, not the game's «debt» caption
                    {"subtitle", paid.empty() ? std::string() : bindings.Text("ship.last_payment") + " " + paid},
                    {"shares", part},
                    {"debts", debts},
                    {"actions", actions}};
    }

    // CANNONS_WINDOW: guns per side as quantity controls (one arrow press per step), the guns in
    // the hold to fit, «remove all», «fit all» and the game's OK.
    Json CannonsView(Bindings &bindings)
    {
        const auto close = [this, &bindings](const Event &) {
            sent_revision_ = 0;
            CloseWindow(bindings);
        };
        bindings.Modal("sheet", close);
        bindings.On("sheet-close", "", close);
        // Guns of the fitted type still in the hold: what a side may take beyond its own.
        const std::string fitted = OneLine(a_->Label("CANNONS_TEXT"));
        int spare = 0;
        Json stock = Json::array();
        const std::vector<const Widget *> rows = a_->All("CANNONS_TABLE");
        for (const Widget *row : rows)
        {
            const std::string name = Trim(row->CellText(0));
            if (name == fitted)
                spare = Number(row->CellText(1));
            stock.push_back(Json{{"cells", Json::array({Json{{"text", name}, {"image", PictureOf(row->cells.empty() || row->cells[0].images.empty() ? row->FirstImage() : &row->cells[0].images[0])}},
                                                        Trim(row->CellText(1))})},
                                 {"selected", row->value == "1"}});
        }
        bindings.On("cannons-stock", "row", [this, &bindings, rows](const Event &event) {
            const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
            if (index < rows.size() && rows[index]->Offers("select"))
            {
                sent_revision_ = 0;
                Step(bindings, rows[index], "select", rows[index]->row_id);
            }
        });
        Json sides = Json::array();
        for (const Side &side : kSides)
        {
            const int now = Number(a_->Label(std::string("CANNONS_QTY_") + side.letter));
            const int most = Number(Field(ship_, side.id));
            const bool walking = intent_.kind == Intent::Guns && intent_.row == side.id;
            const bool arrows = Pressable((std::string("CANNONS_LEFT_") + side.letter).c_str()) || Pressable((std::string("CANNONS_RIGHT_") + side.letter).c_str());
            sides.push_back(Json{{"id", std::string("guns-") + side.id},
                                 {"q", Json{{"value", walking ? intent_.target : now},
                                            {"min", 0},
                                            {"max", std::max(now, std::min(most, now + spare))},
                                            {"label", bindings.Text(side.label)},
                                            {"steps", Json::array({1})},
                                            {"all", false},
                                            {"dense", true},
                                            {"disabled", most <= 0 || !arrows}}}});
            const std::string id = side.id;
            const auto set = [this, &bindings, id](const Event &event) {
                Intent intent;
                intent.kind = Intent::Guns;
                intent.row = id;
                intent.target = std::max(std::atoi(event.value.c_str()), 0);
                Start(bindings, intent);
            };
            bindings.On(std::string("guns-") + side.id, "change", set);
            bindings.On(std::string("guns-") + side.id, "commit", set);
        }
        Json actions = Json::array();
        for (const auto &[node, id, variant] : {std::tuple{"CANNONS_REMOVE_ALL", "cannons-remove", "secondary"},
                                                std::tuple{"CANNONS_SET_ALL", "cannons-fit", "secondary"},
                                                std::tuple{"CANNONS_OK", "cannons-ok", "primary"}})
            if (const Widget *button = a_->One(node))
                actions.push_back(ButtonView(bindings, *button, id, variant));
        const std::string caption = OneLine(a_->Label("CANNONS_CAPTION"));
        const Widget *first = rows.empty() ? nullptr : rows.front();
        return Json{{"title", caption},
                    {"subtitle", fitted},
                    {"image", PictureOf(a_->One("CANNONS_PIC") ? a_->One("CANNONS_PIC")->FirstImage() : nullptr)},
                    {"sides", sides},
                    {"stockLabel", first && !first->column_headers.empty() ? OneLine(first->column_headers[0]) : std::string()},
                    {"stock", stock},
                    {"actions", actions}};
    }
};

} // namespace

std::unique_ptr<Screen> CreateShipScreen()
{
    return std::make_unique<ShipScreen>();
}

} // namespace storm::ui::screens
