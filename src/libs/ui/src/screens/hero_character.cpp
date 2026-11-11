// Hero section «Персонаж» (interface lscchar_all; mockups V2-Hero-Skills, -SkillPoints, -Perks,
// -Companions, -OfficerAssign, -OfficerActions) and the read-only card of another character
// (interface character, V2-Hero-Candidate). Fills the pack screen <s-hero>.
//
// Three tabs of our own: «Умения» (the PIRATES column, personal and ship skills, ‹ › while
// points are free), «Способности» (the perk list of the game's two groups with a detail
// panel) and «Офицеры» (posts, free posts, passengers). The stock interface keeps its rules:
// every change goes through its own controls, and actions that need several of them (a skill
// arrow sits on the selected row only, a perk is learned in its window, a post is changed in
// the post chooser) are intents the screen walks one engine step per model revision, each
// step taken only from the state the engine confirmed.
//
// Facts the stock interface does not draw come from the interface adapter android_hero.c
// (ANDROID_SUBJECT, ANDROID_POSTS, ANDROID_PASSENGERS, ANDROID_CANDIDATES, ANDROID_PERKS);
// the hidden PIRATES table is exported read-only by the engine (xinterface mobile_facts.inc).
#include "hero.h"

#include <cstdlib>
#include <map>

namespace storm::ui::screens
{

using namespace hero;

namespace
{

constexpr NodeRule kNodes[] = {
    {"HERO_NAME", "text", true},
    {"TABLE_SKILL_1", "choice", true},
    {"TABLE_SKILL_2", "choice", true},
    {"ANDROID_SUBJECT", "fact"}, // the adapter's facts: CheckAdapter reports them missing
    {"ANDROID_POSTS", "fact"},
    {"ANDROID_PASSENGERS", "fact"},
    {"ANDROID_CANDIDATES", "fact"},
    {"ANDROID_PERKS", "fact"},
    {"TITLE", "heading"},
    {"CHARACTER_BIG_PICTURE", "image"},
    {"CHARACTER_NATION_PICTURE", "image"},
    {"CHARACTER_DRUNK_PICTURE", "image"},
    {"MAIN_BACK", "image"},
    {"TABLE_SPECIAL", "choice"},
    {"TABLE_OTHER", "choice"},
    {"TABLE_PERKS", "choice"},
    {"TABBTN_1", "image"},
    {"TABBTN_2", "image"},
    {"TABSTR_1", "text"},
    {"TABSTR_2", "text"},
    {"STR_1", "text"}, // table captions: the columns carry their own headings
    {"STR_2", "text"},
    {"STR_3", "text"},
    {"STR_4", "text"},
    {"REPUTATION_STR", "text"},
    {"LOYALITY_STR", "text"},
    {"FREE_SKILL", "text"},
    {"FREE_SPECIAL", "text"},
    {"B_SKILLUP", "button"},
    {"B_SKILLDOWN", "button"},
    {"CHARACTERS_SCROLL", "choice"},
    {"UP_BUTTON", "button"},
    {"DOWN_BUTTON", "button"},
    {"EXIT_BTN", "button"},
    {"I_CHARACTER", "button"},
    {"I_SHIP", "button"},
    {"I_QUESTBOOK", "button"},
    {"I_TRADEBOOK", "button"},
    {"I_NATIONS", "button"},
    {"I_ITEMS", "button"},
    {"STR_BUTTONS", "text"}, // captions of the section buttons: the header icons replace them
    // The game's windows: the perk window is walked by the learn intent, the post chooser and
    // the remove window are drawn as our sheets, the message window as a dialog.
    {"PERK_*", "text|button|image|choice"},
    {"REMOVE_*", "text|button"},
    {"OFFICER*", "text|button"},
    {"PASSENGERSLIST", "choice"},
    {"LEFTSCROLLBUTTON", "button"},
    {"RIGHTSCROLLBUTTON", "button"},
    {"TABLE_SMALLSKILL", "choice"},
    {"TABLE_SMALLOTHER", "choice"},
    {"ACCEPT_ADD_OFFICER", "button"},
    {"CANCEL_ADD_OFFICER", "button"},
    {"SPECIAL_*", "text|button"},
    {"GAME_ACCEPT", "button"},
    {"GAME_CANCEL", "button"},
    {"MSG_*", "text|button"},
};

constexpr double kStepTimeout = 3.0; // an intent whose step got no answer from the engine is dropped

// The caption of a perk group button: "Личные 1" → {"Личные", 1}.
std::pair<std::string, int> GroupCaption(const std::string &label)
{
    const std::string text = Trim(label);
    const size_t space = text.find_last_of(' ');
    if (space == std::string::npos || text.find_first_not_of("0123456789", space + 1) != std::string::npos)
        return {text, 0};
    return {Trim(text.substr(0, space)), std::atoi(text.c_str() + space + 1)};
}

// Our bare icon of a skill (pictures.ini ICONS_SPEC rows carry the skill id as row id).
const char *SkillIcon(const std::string &id)
{
    static const char *const icons[][2] = {
        {"Leadership", "leadership"}, {"FencingL", "rapier"},     {"FencingS", "sabre"},    {"FencingH", "axe"},
        {"Pistol", "pistol"},         {"Fortune", "fortune"},     {"Sneak", "sneak"},       {"Sailing", "wheel"},
        {"Accuracy", "accuracy"},     {"Cannons", "cannon"},      {"Grappling", "grapple"}, {"Defence", "shield"},
        {"Repair", "repair"},         {"Commerce", "commerce"}};
    for (const auto &pair : icons)
        if (id == pair[0])
            return pair[1];
    return "";
}

// The icon of a free post (GetOfficerTypeByNum).
const char *PostIcon(const std::string &post, int slot)
{
    static const char *const icons[][2] = {{"navigator", "wheel"}, {"boatswain", "grapple"}, {"cannoner", "cannon"},
                                           {"doctor", "medicine"}, {"treasurer", "commerce"}, {"carpenter", "repair"}};
    for (const auto &pair : icons)
        if (post == pair[0])
            return pair[1];
    return slot % 3 == 1 ? "rapier" : slot % 3 == 2 ? "sabre" : "axe";
}

// Perks drawn with our enamel icon; the others keep the game's picture.
bool HasPerkIcon(const std::string &perk)
{
    static const char *const perks[] = {"advanceddefense", "alchemy", "basiccommerce", "basicdefense", "brander", "carpenter",
                                        "crewdamageup", "criticalhit", "energyplus", "fastreload", "grapplingprofessional",
                                        "gunman", "gunprofessional", "hardhitter", "hpplus", "hulldamageup", "instantrepair",
                                        "ironwill", "longrangeshoot", "professionalcommerce", "sailsdamageup", "shipescape",
                                        "swordplayprofessional", "tireless"};
    std::string id;
    for (const char c : perk)
        id += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    for (const char *name : perks)
        if (id == name)
            return true;
    return false;
}

std::string Lowercase(const std::string &text)
{
    std::string out;
    for (const char c : text)
        out += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return out;
}

// "45 / 140" → {value: percent, text: "45"}.
Json BarOf(const std::string &text)
{
    const size_t slash = text.find('/');
    const int current = Number(text);
    const int maximum = slash == std::string::npos ? 0 : Number(text.substr(slash + 1));
    return Json{{"value", maximum > 0 ? std::clamp(current * 100 / maximum, 0, 100) : 100}, {"text", std::to_string(current)}};
}

enum class Tab
{
    Skills,
    Perks,
    Officers
};

class HeroScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-hero";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        // Handlers run after Build returns: keep the admission (pointers into the router's model,
        // valid until the next revision rebuilds the view).
        admission_ = admitted;
        a_ = &admission_;
        revision_ = admitted.model->revision;
        CheckAdapter(admitted, bindings, "ANDROID_SUBJECT", checked_opening_);
        const bool full = admitted.model->screen == "lscchar_all";
        const Widget *subject = admitted.One("ANDROID_SUBJECT");
        if (!full && tab_ == Tab::Officers)
            tab_ = Tab::Skills;

        Json view = Json::object();
        view["title"] = Trim(admitted.Label("TITLE"));
        view["sections"] = full ? "hero" : "";

        // The game's windows that are open now.
        window_ = admitted.One("PERK_ACCEPT") || admitted.One("PERK_CANCEL")                ? Window::Perk
                  : admitted.One("REMOVE_ACCEPT_OFFICER") || admitted.One("REMOVE_CANCEL_OFFICER") ? Window::Remove
                  : admitted.One("ACCEPT_ADD_OFFICER") || admitted.One("CANCEL_ADD_OFFICER")         ? Window::Assign
                  : admitted.One("MSG_OK")                                                           ? Window::Message
                                                                                                     : Window::None;
        Walk(bindings);

        // Header: tabs, the officer switch, the sections.
        const int free_self = Number(Field(subject, "freeself"));
        const int free_ship = Number(Field(subject, "freeship"));
        const int free_skill = Number(admitted.Label("FREE_SKILL"));
        Json tabs = Json::array();
        tabs.push_back(Json{{"id", "skills"}, {"label", bindings.Text("hero.tab_skills")}, {"selected", tab_ == Tab::Skills},
                            {"badge", free_skill > 0 ? Json(free_skill) : Json("")}});
        tabs.push_back(Json{{"id", "perks"}, {"label", bindings.Text("hero.tab_perks")}, {"selected", tab_ == Tab::Perks},
                            {"badge", free_self + free_ship > 0 ? Json(free_self + free_ship) : Json("")}});
        if (full)
            tabs.push_back(Json{{"id", "officers"}, {"label", bindings.Text("hero.tab_officers")}, {"selected", tab_ == Tab::Officers}});
        view["tabs"] = tabs;
        bindings.On("header-tabs", "select", [this, &bindings](const Event &event) {
            const Tab next = event.value == "perks" ? Tab::Perks : event.value == "officers" ? Tab::Officers : Tab::Skills;
            if (next == tab_)
                return;
            tab_ = next;
            sent_revision_ = 0; // a player action: its first step may go at once
            if (window_ != Window::None && window_ != Window::Message)
                CloseWindow(bindings);
            bindings.Rebuild();
        });
        view["tab"] = tab_ == Tab::Skills ? "skills" : tab_ == Tab::Perks ? "perks" : "officers";
        view["subject"] = full ? hero::Subject(admitted, bindings, subjects_, [this, &bindings](const Widget &row) {
            sent_revision_ = 0; // a player action: its first step may go at once
            Step(bindings, &row, "select", row.row_id);
        })
                               : Json(nullptr);
        BindSections(bindings);

        view["card"] = CardView(bindings, subject, free_skill);
        if (tab_ == Tab::Skills)
            SkillsView(view, bindings, free_skill);
        else if (tab_ == Tab::Perks)
            PerksView(view, bindings, subject, full, free_self, free_ship);
        else
            OfficersView(view, bindings);
        view["dialog"] = DialogView(bindings);

        bindings.On("header-back", "", [this, &bindings](const Event &) {
            sent_revision_ = 0; // a player action: its first step may go at once
            if (window_ != Window::None)
                CloseWindow(bindings);
            else
                bindings.Back(nullptr);
        });
        return view;
    }

    // Android back closes the subject list or the game's window first.
    bool OwnsBack() const override
    {
        return subjects_.Open() || window_ != Window::None;
    }
    void Back(Bindings &bindings) override
    {
        if (subjects_.Open())
            return subjects_.Close(bindings);
        sent_revision_ = 0; // a player action: its first step may go at once
        CloseWindow(bindings);
    }

  private:
    enum class Window
    {
        None,
        Perk,
        Remove,
        Assign,
        Message
    };
    // What the player asked for that takes several engine steps.
    struct Intent
    {
        enum Kind
        {
            None,
            Arrow,   // table, row id, verb up|down
            Learn,   // perk id
            Post,    // slot: select, then activate (the post chooser or the remove window)
            Assign,  // candidate row of PASSENGERSLIST: select, then accept
            Section, // a section button once no window is open
        } kind = None;
        std::string node, row, verb;
        double since = 0.0;
    };

    Admission admission_;
    const Admission *a_ = nullptr;
    uint64_t revision_ = 0;
    uint64_t sent_revision_ = 0;
    uint64_t checked_opening_ = 0;
    SubjectSwitch subjects_;
    Tab tab_ = Tab::Skills;
    Window window_ = Window::None;
    Intent intent_;
    std::string perk_;                  // the perk in the detail panel
    std::map<std::string, int> added_;  // points the player put into a skill during this visit

    // One engine step per model revision: a step is sent only after the engine answered the last.
    bool Step(Bindings &bindings, const Widget *widget, const std::string &verb, const std::string &value = {})
    {
        if (!widget || sent_revision_ == revision_)
            return false;
        sent_revision_ = revision_;
        bindings.Send(*widget, verb, value);
        return true;
    }

    const Widget *Row(const char *node, const std::string &row_id) const
    {
        for (const Widget *row : a_->All(node))
            if (row->row_id == row_id)
                return row;
        return nullptr;
    }

    const Widget *Selected(const char *node) const
    {
        for (const Widget *row : a_->All(node))
            if (row->value == "1")
                return row;
        return nullptr;
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
            bindings.Error("lscchar_all: " + why);
        intent_ = {};
    }

    void CloseWindow(Bindings &bindings)
    {
        const char *const cancels[] = {window_ == Window::Perk     ? "PERK_CANCEL"
                                       : window_ == Window::Remove ? "REMOVE_CANCEL_OFFICER"
                                       : window_ == Window::Assign ? "CANCEL_ADD_OFFICER"
                                                                   : "MSG_OK",
                                       window_ == Window::Perk     ? "PERK_EXIT_BTN"
                                       : window_ == Window::Remove ? "REMOVE_EXIT_BTN"
                                       : window_ == Window::Assign ? "OFFICER_EXIT_BTN"
                                                                   : "MSG_EXIT_BTN"};
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
            return Drop(bindings, "the engine did not answer a step (" + intent_.node + " " + intent_.row + ")");
        switch (intent_.kind)
        {
        case Intent::Arrow: {
            const Widget *row = Row(intent_.node.c_str(), intent_.row);
            if (!row)
                return Drop(bindings, "skill row " + intent_.row + " is gone");
            if (row->value != "1")
            {
                Step(bindings, row, "select", row->row_id);
                return;
            }
            const Widget *arrow = a_->One(intent_.verb == "up" ? "B_SKILLUP" : "B_SKILLDOWN");
            if (!arrow || !arrow->Offers("press"))
                return Drop(bindings, {}); // the game offers no arrow there (limit reached)
            if (Step(bindings, arrow, "press"))
            {
                added_[intent_.row] += intent_.verb == "up" ? 1 : -1;
                intent_ = {};
            }
            return;
        }
        case Intent::Learn: {
            if (window_ == Window::Perk)
            {
                const Widget *accept = a_->One("PERK_ACCEPT");
                const Widget *selected = Selected("TABLE_PERKS");
                if (selected && selected->row_id == intent_.row && accept && accept->Offers("press"))
                {
                    if (Step(bindings, accept, "press"))
                        intent_ = {};
                    return;
                }
                CloseWindow(bindings); // another perk's window, or the game refuses: close it
                if (selected && selected->row_id == intent_.row)
                    Drop(bindings, {});
                return;
            }
            if (window_ != Window::None)
                return CloseWindow(bindings);
            const Widget *row = Row("TABLE_PERKS", intent_.row);
            if (!row)
                return Drop(bindings, "perk " + intent_.row + " is not in the list");
            Step(bindings, row, row->value == "1" ? "activate" : "select", row->row_id);
            return;
        }
        case Intent::Post: {
            if (window_ == Window::Remove || window_ == Window::Assign)
            {
                intent_ = {};
                return;
            }
            if (window_ != Window::None)
                return CloseWindow(bindings);
            const auto rows = a_->All("CHARACTERS_SCROLL");
            const int slot = std::atoi(intent_.row.c_str());
            const Widget *row = nullptr;
            for (const Widget *r : rows)
                if (r->index == slot)
                    row = r;
            if (!row)
                return Drop(bindings, "officer slot " + intent_.row + " is gone");
            if (row->value != "1")
                Step(bindings, row, "select", row->row_id);
            else if (row->Offers("activate"))
                Step(bindings, row, "activate", row->row_id);
            return;
        }
        case Intent::Assign: {
            if (window_ != Window::Assign)
                return Drop(bindings, {}); // the chooser closed: assigned, or cancelled by the game
            const int index = std::atoi(intent_.row.c_str());
            const Widget *row = nullptr;
            for (const Widget *r : a_->All("PASSENGERSLIST"))
                if (r->index == index)
                    row = r;
            if (!row)
                return Drop(bindings, "candidate " + intent_.row + " is not in the post chooser");
            if (row->value != "1")
            {
                Step(bindings, row, "select", row->row_id);
                return;
            }
            const Widget *accept = a_->One("ACCEPT_ADD_OFFICER");
            if (!accept || !accept->Offers("press"))
                return Drop(bindings, "the post chooser refuses candidate " + intent_.row);
            Step(bindings, accept, "press");
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
            if (event.value == "hero")
                return;
            Intent intent;
            intent.kind = Intent::Section;
            intent.row = event.value;
            Start(bindings, intent);
        });
    }

    Json CardView(Bindings &bindings, const Widget *subject, int free_skill)
    {
        std::map<std::string, const Widget *> other;
        for (const Widget *row : a_->All("TABLE_OTHER"))
            other[row->row_id] = row;
        const auto value = [&](const char *id) { return other.count(id) ? Trim(other[id]->CellText(2)) : std::string(); };
        const auto label = [&](const char *id) { return other.count(id) ? Trim(other[id]->CellText(1)) : std::string(); };

        Json card = Json::object();
        card["title"] = Trim(a_->Label("HERO_NAME"));
        std::string sub = label("Rank").empty() ? "" : label("Rank") + " " + value("Rank");
        const Widget *slot = Selected("CHARACTERS_SCROLL");
        for (const Widget *post : a_->All("ANDROID_POSTS"))
            if (slot && Field(post, "slot") == std::to_string(slot->index) && !Field(post, "character").empty())
                sub += (sub.empty() ? "" : " · ") + LowerText(Field(post, "role"));
        card["sub"] = sub;
        const ImageRef *face = nullptr;
        if (const Widget *picture = a_->One("CHARACTER_BIG_PICTURE"))
            face = picture->FirstImage();
        const Json portrait = PictureOf(face);
        card["portrait"] = portrait.is_object() ? portrait.value("texture", "") : std::string();
        card["health"] = BarOf(value("Life"));
        card["energy"] = BarOf(value("Energy"));
        card["free"] = tab_ == Tab::Skills && free_skill > 0 ? std::to_string(free_skill) : "";
        Json lines = Json::array();
        if (!value("Health").empty())
        {
            const std::string word = LowerText(value("Health"));
            const bool bad = other.count("Health") && (other["Health"]->cells.size() > 2) &&
                             (other["Health"]->cells[2].color_argb & 0xFFFFFF) != 0xFFFFFF && (other["Health"]->cells[2].color_argb & 0xFF00) < 0x8000;
            lines.push_back(Json{{"k", label("Health")}, {"v", word}, {"tone", bad ? "danger" : "ok"}});
        }
        // The stock LOYALITY bar: the hero's authority (1 … 100), an officer's loyalty word.
        if (!Field(subject, "authority").empty())
            lines.push_back(Json{{"k", Trim(a_->Label("LOYALITY_STR"))}, {"v", Field(subject, "authority") + " / 100"}});
        else if (!Field(subject, "loyalty").empty())
            lines.push_back(Json{{"k", Trim(a_->Label("LOYALITY_STR"))}, {"v", LowerText(Field(subject, "loyalty"))}});
        if (Field(subject, "hero") != "1" && Number(Field(subject, "salary")) > 0)
            lines.push_back(Json{{"k", bindings.Text("hero.salary")}, {"v", Thousands(Field(subject, "salary"))}, {"tone", "gold"},
                                 {"after", " " + bindings.Text("hero.per_month")}});
        card["lines"] = lines;
        // The portrait opens the rest of the game's facts (TABLE_OTHER: rank, title, money,
        // weight, experience) with the reputation, as the mockup's rail leaves them out.
        std::string facts;
        for (const Widget *row : a_->All("TABLE_OTHER"))
            if (row->row_id != "Life" && row->row_id != "Energy" && row->row_id != "Health" && !Trim(row->CellText(2)).empty())
                facts += Trim(row->CellText(1)) + ": " + Trim(row->CellText(2)) + "\n";
        if (!Trim(a_->Label("REPUTATION_STR")).empty())
            facts += bindings.Text("hero.reputation") + ": " + Trim(a_->Label("REPUTATION_STR")) + "\n";
        const std::string name = Trim(a_->Label("HERO_NAME"));
        bindings.On("card", "portrait", [&bindings, name, facts](const Event &) { bindings.Read(name, facts); });
        // The start-of-game PIRATES choice (interface character with its own OK / cancel).
        Json actions = Json::array();
        if (const Widget *cancel = a_->One("GAME_CANCEL"))
            actions.push_back(ButtonView(bindings, *cancel, "game-cancel", "secondary"));
        if (const Widget *accept = a_->One("GAME_ACCEPT"))
            actions.push_back(ButtonView(bindings, *accept, "game-accept", "primary"));
        card["actions"] = actions;
        return card;
    }

    // One skill table (PIRATES, personal, ship) for a c-table.
    Json SkillTable(Bindings &bindings, const char *node, const std::string &id, const std::string &heading, bool points,
                    bool special)
    {
        Json columns = Json::array();
        columns.push_back(Json{{"label", heading}, {"w", "1fr"}});
        if (points)
            columns.push_back(Json{{"label", ""}, {"w", "112px"}, {"align", "right"}});
        else
        {
            if (!special)
                columns.push_back(Json{{"label", bindings.Text("hero.exp")}, {"w", "40px"}});
            columns.push_back(Json{{"label", ""}, {"w", special ? "32px" : "28px"}, {"align", "right"}});
        }
        Json rows = Json::array();
        std::vector<const Widget *> source = a_->All(node);
        for (const Widget *row : source)
        {
            // TABLE_SKILL_n: icon, name, experience %, bonus, value; TABLE_SPECIAL: letter, name, bonus, value.
            const int value_col = special ? 3 : 4;
            // The game names blade and gun skills by pairs ("Рапиры/Шпаги"): the first word fits
            // the column (mockup V2-Hero-Skills).
            const std::string full = Trim(row->CellText(1));
            const std::string name = special || full.find('/') == std::string::npos ? full : Trim(full.substr(0, full.find('/')));
            const std::string bonus = Trim(row->CellText(special ? 2 : 3));
            const std::string value = Trim(row->CellText(value_col));
            const std::string tone = bonus.find('-') != std::string::npos   ? "danger"
                                     : bonus.find('+') != std::string::npos ? "ok"
                                     : special                              ? ""
                                                                            : "gold";
            // A name that does not fit wraps to two lines and steps down (ui-clamp min-size).
            Json first{{"text", name}, {"lines", 2}};
            if (!special && !points && *SkillIcon(row->row_id))
                first["icon"] = SkillIcon(row->row_id), first["iconSize"] = 24;
            Json cells = Json::array({first});
            if (points)
            {
                const int added = added_.count(row->row_id) ? added_[row->row_id] : 0;
                const bool may_down = added > 0 || (special && a_->One("GAME_ACCEPT"));
                cells.push_back(Json{{"text", value}, {"arrows", true}, {"noLeft", !may_down}, {"tone", added > 0 ? "ok" : "gold"}, {"bold", true}});
            }
            else
            {
                if (!special)
                    cells.push_back(Json{{"bar", std::clamp(Number(row->CellText(2)), 0, 100)}, {"barKind", "xp"}, {"text", ""}});
                Json last{{"text", value}, {"bold", true}};
                if (!tone.empty())
                    last["tone"] = tone;
                cells.push_back(last);
            }
            rows.push_back(Json{{"cells", cells}});
        }
        const auto arrow = [this, &bindings, node, source](const Event &event, const char *verb) {
            const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
            if (index >= source.size())
                return;
            Intent intent;
            intent.kind = Intent::Arrow;
            intent.node = node;
            intent.row = source[index]->row_id;
            intent.verb = verb;
            Start(bindings, intent);
        };
        bindings.On(id, "inc", [arrow](const Event &event) { arrow(event, "up"); });
        bindings.On(id, "dec", [arrow](const Event &event) { arrow(event, "down"); });
        bindings.Ignore(id, "row");
        return Json{{"columns", columns}, {"rows", rows}};
    }

    void SkillsView(Json &view, Bindings &bindings, int free_skill)
    {
        const bool points = free_skill > 0 || !added_.empty();
        const int free_special = Number(a_->Label("FREE_SPECIAL"));
        const bool special_points = (free_special > 0 || a_->One("GAME_ACCEPT")) && !a_->All("TABLE_SPECIAL").empty() &&
                                    a_->All("TABLE_SPECIAL").front()->Offers("select");
        view["special"] = SkillTable(bindings, "TABLE_SPECIAL", "special", bindings.Text("hero.special"), special_points, true);
        view["personal"] = SkillTable(bindings, "TABLE_SKILL_1", "personal", GroupCaption(a_->Label("TABSTR_1")).first, points, false);
        view["ship"] = SkillTable(bindings, "TABLE_SKILL_2", "ship", GroupCaption(a_->Label("TABSTR_2")).first, points, false);
        view["points"] = points;
    }

    void PerksView(Json &view, Bindings &bindings, const Widget *subject, bool full, int free_self, int free_ship)
    {
        // The game's two groups: TABBTN_n with TABSTR_n "Личные 1"; the active one shows TabDeSelected (value 1).
        const Widget *tab1 = a_->One("TABBTN_1");
        const Widget *tab2 = a_->One("TABBTN_2");
        const bool ship = tab2 && tab2->value == "1";
        const auto group1 = GroupCaption(a_->Label("TABSTR_1"));
        const auto group2 = GroupCaption(a_->Label("TABSTR_2"));
        view["groups"] = Json::array({Json{{"id", "1"}, {"label", group1.first}, {"badge", free_self > 0 ? Json(free_self) : Json("")}, {"selected", !ship}},
                                      Json{{"id", "2"}, {"label", group2.first}, {"badge", free_ship > 0 ? Json(free_ship) : Json("")}, {"selected", ship}}});
        bindings.On("perk-groups", "select", [this, &bindings, tab1, tab2, ship](const Event &event) {
            const bool want_ship = event.value == "2";
            if (want_ship == ship)
                return;
            sent_revision_ = 0; // a player action: its first step may go at once
            if (window_ != Window::None)
                return CloseWindow(bindings);
            perk_.clear();
            Step(bindings, want_ship ? tab2 : tab1, "press");
        });
        const std::string exp = Field(subject, ship ? "expship" : "expself");
        const std::string rate = Field(subject, ship ? "rateship" : "rateself");
        view["progress"] = full && Number(rate) > 0 ? bindings.Text("hero.next_point") + " " + Thousands(exp.empty() ? "0" : exp) + " / " + Thousands(rate) : "";

        std::map<std::string, const Widget *> facts;
        for (const Widget *fact : a_->All("ANDROID_PERKS"))
            facts[fact->row_id] = fact;
        const std::vector<const Widget *> rows = a_->All("TABLE_PERKS");
        if (perk_.empty() || !Row("TABLE_PERKS", perk_))
            perk_ = rows.empty() ? "" : rows.front()->row_id;
        Json list = Json::array();
        for (const Widget *row : rows)
        {
            const Widget *fact = facts.count(row->row_id) ? facts[row->row_id] : nullptr;
            const bool learned = Field(fact, "learned") == "1";
            const std::string need = Field(fact, "need");
            Json cell{{"text", Trim(row->CellText(1))}};
            if (HasPerkIcon(row->row_id))
                cell["image"] = Json{{"kind", "token"}, {"icon", "perk-" + Lowercase(row->row_id)}, {"enamel", !learned && !need.empty() ? "iron" : "red"}};
            else
                cell["image"] = PictureOf(row->cells.empty() || row->cells[0].images.empty() ? nullptr : &row->cells[0].images[0]);
            if (learned)
                cell["badge"] = "check";
            else if (!need.empty())
                cell["badge"] = "lock", cell["tone"] = "dim";
            list.push_back(Json{{"cells", Json::array({cell})}, {"selected", row->row_id == perk_}});
        }
        view["perks"] = list;
        bindings.On("perks", "row", [this, &bindings, rows](const Event &event) {
            const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
            if (index < rows.size())
            {
                perk_ = rows[index]->row_id;
                bindings.Rebuild();
            }
        });

        // The detail panel of the perk the player looks at.
        const Widget *row = Row("TABLE_PERKS", perk_);
        const Widget *fact = facts.count(perk_) ? facts[perk_] : nullptr;
        if (!row)
        {
            view["detail"] = nullptr;
            return;
        }
        const bool learned = Field(fact, "learned") == "1";
        const std::string need = Field(fact, "need");
        const int free = ship ? free_ship : free_self;
        Json detail{{"title", Trim(row->CellText(1))}, {"sub", LowerText(ship ? group2.first : group1.first)}, {"text", Field(fact, "text")}};
        if (HasPerkIcon(perk_))
            detail["image"] = Json{{"kind", "token"}, {"icon", "perk-" + Lowercase(perk_)}, {"enamel", !learned && !need.empty() ? "iron" : "red"}};
        else
            detail["image"] = PictureOf(row->cells.empty() || row->cells[0].images.empty() ? nullptr : &row->cells[0].images[0]);
        if (!need.empty() && !learned)
            detail["need"] = bindings.Text("hero.needs") + " " + need;
        if (Field(fact, "officer") == "1")
            detail["note"] = bindings.Text("hero.officer_has");
        if (full)
        {
            const std::string reason = learned ? bindings.Text("hero.learned") : !need.empty() ? bindings.Text("hero.needs") + " " + need
                                                                   : free <= 0                ? bindings.Text("hero.no_points")
                                                                                              : std::string();
            detail["learn"] = Json{{"id", "perk-learn"},
                                   {"label", learned ? bindings.Text("hero.learned") : bindings.Text("hero.learn")},
                                   {"sub", learned ? "" : bindings.Text("common.free") + ": " + std::to_string(std::max(free, 0))},
                                   {"disabled", !reason.empty()},
                                   {"reason", reason},
                                   {"busy", intent_.kind == Intent::Learn}};
            bindings.On("perk-learn", "", [this, &bindings](const Event &) {
                Intent intent;
                intent.kind = Intent::Learn;
                intent.row = perk_;
                Start(bindings, intent);
            });
        }
        view["detail"] = detail;
    }

    void OfficersView(Json &view, Bindings &bindings)
    {
        Json on = Json::array(), free = Json::array(), passengers = Json::array();
        int posts = 0;
        for (const Widget *post : a_->All("ANDROID_POSTS"))
        {
            const int slot = std::atoi(Field(post, "slot").c_str());
            if (slot == 0)
                continue;
            ++posts;
            if (!Field(post, "character").empty())
                on.push_back(Json{{"id", std::to_string(slot)},
                                  {"name", post->label},
                                  {"meta", LowerText(Field(post, "role"))},
                                  {"image", Portrait(Field(post, "face"))},
                                  {"selected", intent_.kind == Intent::Post && intent_.row == std::to_string(slot)}});
            else
                free.push_back(Json{{"id", std::to_string(slot)}, {"role", Field(post, "role")}, {"icon", PostIcon(Field(post, "post"), slot)},
                                    {"selected", intent_.kind == Intent::Post && intent_.row == std::to_string(slot)}});
        }
        for (const Widget *passenger : a_->All("ANDROID_PASSENGERS"))
            passengers.push_back(Json{{"name", passenger->label},
                                      {"meta", bindings.Text("hero.rank_short") + " " + Field(passenger, "rank")},
                                      {"image", Portrait(Field(passenger, "face"))}});
        view["posts"] = on;
        view["postsCount"] = std::to_string(on.size()) + " " + bindings.Text("common.of") + " " + std::to_string(posts);
        view["freePosts"] = free;
        view["passengers"] = passengers;
        view["passengersCount"] = passengers.empty() ? "" : std::to_string(passengers.size());
        // A tap on a post (element "post-<slot>") opens the game's post chooser or remove window.
        for (const Json &rows : {on, free})
            for (const Json &row : rows)
            {
                const std::string slot = row.value("id", "");
                bindings.On("post-" + slot, "", [this, &bindings, slot](const Event &) {
                    Intent intent;
                    intent.kind = Intent::Post;
                    intent.row = slot;
                    Start(bindings, intent);
                });
            }
    }

    Json DialogView(Bindings &bindings)
    {
        const Widget *slot = Selected("CHARACTERS_SCROLL");
        const Widget *post = nullptr;
        for (const Widget *p : a_->All("ANDROID_POSTS"))
            if (slot && Field(p, "slot") == std::to_string(slot->index))
                post = p;
        const auto close = [this, &bindings](const Event &) {
            sent_revision_ = 0; // a player action: its first step may go at once
            CloseWindow(bindings);
        };
        bindings.On("dialog", "dismiss", close);
        bindings.On("dialog-close", "", close);
        if (window_ == Window::Remove && post)
        {
            Json facts = Json::array();
            if (!Field(post, "skill").empty())
                facts.push_back(Json{{"k", Field(post, "skill")}, {"v", Field(post, "skillvalue")}});
            if (!Field(post, "loyalty").empty())
                facts.push_back(Json{{"k", bindings.Text("hero.loyalty")}, {"v", LowerText(Field(post, "loyalty"))}});
            if (Number(Field(post, "salary")) > 0)
                facts.push_back(Json{{"k", bindings.Text("hero.salary")}, {"v", Thousands(Field(post, "salary")) + " " + bindings.Text("hero.per_month")}});
            const Widget *remove = a_->One("REMOVE_ACCEPT_OFFICER");
            Json actions = Json::array();
            if (remove)
                actions.push_back(Json{{"id", "officer-remove"}, {"label", bindings.Text("hero.dismiss")}, {"variant", "secondary"},
                                       {"disabled", !remove->Offers("press")}, {"busy", bindings.Pending("officer-remove")}});
            actions.push_back(Json{{"id", "officer-skills"}, {"label", bindings.Text("hero.tab_skills")}, {"variant", "primary"}});
            bindings.On("officer-remove", "", [this, &bindings, remove](const Event &) {
                sent_revision_ = 0;
                Step(bindings, remove, "press");
            });
            bindings.On("officer-skills", "", [this, &bindings](const Event &) {
                tab_ = Tab::Skills;
                sent_revision_ = 0; // a player action: its first step may go at once
                CloseWindow(bindings);
                bindings.Rebuild();
            });
            return Json{{"kind", "officer"}, {"title", post->label}, {"subtitle", LowerText(Field(post, "role"))}, {"facts", facts}, {"actions", actions}};
        }
        if (window_ == Window::Assign)
        {
            Json rows = Json::array();
            for (const Widget *candidate : a_->All("ANDROID_CANDIDATES"))
            {
                std::string meta = bindings.Text("hero.rank_short") + " " + Field(candidate, "rank");
                if (Number(Field(candidate, "salary")) > 0)
                    meta += " · " + Thousands(Field(candidate, "salary")) + " " + bindings.Text("hero.per_month");
                rows.push_back(Json{{"id", Field(candidate, "index")},
                                    {"name", candidate->label},
                                    {"meta", meta},
                                    {"value", Field(candidate, "skill") + " " + Field(candidate, "skillvalue")},
                                    {"image", Portrait(Field(candidate, "face"))},
                                    {"busy", intent_.kind == Intent::Assign && intent_.row == Field(candidate, "index")}});
            }
            for (const Json &row : rows)
            {
                const std::string index = row.value("id", "");
                bindings.On("candidate-" + index, "", [this, &bindings, index](const Event &) {
                    Intent intent;
                    intent.kind = Intent::Assign;
                    intent.row = index;
                    Start(bindings, intent);
                });
            }
            return Json{{"kind", "assign"}, {"title", post ? Field(post, "role") : std::string()}, {"rows", rows}, {"empty", bindings.Text("hero.no_candidates")}};
        }
        if (window_ == Window::Message)
        {
            const Widget *ok = a_->One("MSG_OK");
            Json actions = Json::array({ButtonView(bindings, *ok, "msg-ok", "primary")});
            return Json{{"kind", "message"}, {"title", Trim(a_->Label("MSG_WINDOW_CAPTION"))}, {"text", Trim(a_->Label("MSG_WINDOW_TEXT"))}, {"actions", actions}};
        }
        return nullptr;
    }
};

} // namespace

std::unique_ptr<Screen> CreateHeroScreen()
{
    return std::make_unique<HeroScreen>();
}

} // namespace storm::ui::screens
