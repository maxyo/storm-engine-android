// Saves (interface "save_load", mockups V2-Save-Load, -Save, -Empty, -ConfirmLoad,
// -ConfirmDelete, -Overwrite): every record in one C-Table, the chosen record in the detail
// column with its primary action and the bare trash glyph. Fills the pack screen <s-saves>.
//
// The engine lists the records of every profile (no profiles in this port; when saving, those
// of the game in progress) as ANDROID_SAVE_LIST rows under the ANDROID_SAVES list
// (value = number of records; label = the file name, `delete` / `activate` verbs; the save
// slot of a new record has the reason "new"). Each row's context carries what the record
// says about itself (xinterface mobile_save_list.hpp SaveFacts): hero, location, gameDate
// (YYYY-MM-DD), gameTime, created (local YYYY-MM-DD HH:MM), playMinutes; the new record has
// the hero, location and play time of the game in progress. Records are named
// «<hero> · <location>» (no player profile, no naming); a record written before the engine
// stored the hero shows its location alone. The game asks its own questions (CONFIRM_*):
// they become the C-Dialog. The game's name editor (CUSTOM_SAVE_*) is accepted as it
// comes: records are named by the game, never by the player.
#include "controls.h"
#include "menu.h"

#include <cstdlib>
#include <algorithm>
#include <array>
#include <ctime>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"ANDROID_SAVES", "list", true},
    {"ANDROID_SAVE_LIST", "save"},
    {"TITLE", "heading|text"},
    {"BTN_SAVELOAD", "button"},
    {"BTN_DELETE", "button"},
    {"BTN_EXIT", "button"},
    {"CONFIRM_TEXT", "text"},
    {"CONFIRM_YES", "button"},
    {"CONFIRM_NO", "button"},
    {"CUSTOM_SAVE_NAME", "edit"},
    {"CUSTOM_SAVE_MSG", "text"},
    {"CUSTOM_SAVE_BTN_OK", "button"},
    {"CUSTOM_SAVE_BTN_CANCEL", "button"},
    // Drawn by the game's page only: its slot pager and notes (we list every record with
    // the engine's facts), the profile (there are no profiles in this port), the officers
    // of the selected record.
    {"SAVE_SELECTER", "stepper"},
    {"SAVENOTES", "text"},
    {"SAVEINFO", "text"},
    {"BTN_PROFILE", "button"},
    // The game's profile chooser: the engine answers it before the player sees it (no
    // profiles; xinterface MobileKeepSaveProfile), the list shows every profile's records.
    {"PROFILE_*", "*"},
    {"OFFICER*", "image|text"},
    {"MAIN_BACK", "image"},
};

const char *const kMonths[] = {StringKey("common.month_1"), StringKey("common.month_2"),  StringKey("common.month_3"),
                               StringKey("common.month_4"), StringKey("common.month_5"),  StringKey("common.month_6"),
                               StringKey("common.month_7"), StringKey("common.month_8"),  StringKey("common.month_9"),
                               StringKey("common.month_10"), StringKey("common.month_11"), StringKey("common.month_12")};

struct Stamp
{
    int day = 0, month = 0, year = 0;
    std::string clock; // "HH:MM"
    bool Valid() const
    {
        return day > 0 && month >= 1 && month <= 12 && year > 0;
    }
};

// "YYYY-MM-DD" and an optional clock "HH:MM".
Stamp ParseStamp(const std::string &date, const std::string &clock)
{
    Stamp stamp;
    if (date.size() != 10 || date[4] != '-' || date[7] != '-' || (!clock.empty() && clock.size() != 5))
        return stamp;
    stamp.year = std::atoi(date.c_str());
    stamp.month = std::atoi(date.c_str() + 5);
    stamp.day = std::atoi(date.c_str() + 8);
    stamp.clock = clock;
    return stamp;
}

std::string Context(const Widget &row, const char *key)
{
    const auto found = row.context.find(key);
    return found == row.context.end() ? std::string() : found->second;
}

std::string GameDate(const Bindings &bindings, const Stamp &stamp)
{
    return std::to_string(stamp.day) + " " + bindings.Text(kMonths[stamp.month - 1]) + " " + std::to_string(stamp.year);
}

// "сегодня, 22:14", "вчера, 23:05", "7 нояб., 20:12", "18 окт. 2025, 08:53"
std::string CreatedDate(const Bindings &bindings, const Stamp &stamp)
{
    const std::time_t now = std::time(nullptr);
    std::tm today{};
    localtime_r(&now, &today);
    std::tm then{};
    then.tm_year = stamp.year - 1900;
    then.tm_mon = stamp.month - 1;
    then.tm_mday = stamp.day;
    then.tm_hour = 12;
    std::tm noon = today;
    noon.tm_hour = 12;
    noon.tm_min = noon.tm_sec = 0;
    const double days = std::difftime(std::mktime(&noon), std::mktime(&then)) / 86400.0;
    std::string day;
    if (days > -0.5 && days < 0.5)
        day = bindings.Text("save.today");
    else if (days >= 0.5 && days < 1.5)
        day = bindings.Text("save.yesterday");
    else
    {
        day = std::to_string(stamp.day) + " " + bindings.Text(kMonths[stamp.month - 1]);
        if (stamp.year != today.tm_year + 1900)
            day += " " + std::to_string(stamp.year);
    }
    return day + ", " + stamp.clock;
}

// 402 → "6 ч 42 мин"
std::string PlayTime(const Bindings &bindings, int minutes)
{
    const std::string hours = minutes >= 60 ? std::to_string(minutes / 60) + " " + bindings.Text("save.hours") + " " : std::string();
    return hours + std::to_string(minutes % 60) + " " + bindings.Text("save.minutes");
}

// The application's background autosaves, two rotating records the game lists like any
// other (app PROGRAM/interface/android_background_save.c names their files
// "Автосохранение 1|2"; the name is matching data, not UI text). Returns the slot number,
// 0 for other records. They are shown as their own pair and never deleted or overwritten.
int AutosaveSlot(const std::string &label)
{
    static const std::string prefix = "Автосохранение ";
    if (label.size() <= prefix.size() || label.compare(0, prefix.size(), prefix) != 0)
        return 0;
    const std::string number = label.substr(prefix.size());
    return number.find_first_not_of("0123456789") == std::string::npos ? std::atoi(number.c_str()) : 0;
}

struct Record
{
    const Widget *row = nullptr;
    bool fresh = false;
    int autosave = 0;
    std::string hero, location;
    Stamp played, created;
    int minutes = -1;

    static Record Of(const Widget *row)
    {
        Record record{row, row->unavailable_reason == "new", 0};
        record.autosave = record.fresh ? 0 : AutosaveSlot(row->label);
        record.hero = Context(*row, "hero");
        record.location = Context(*row, "location");
        record.played = ParseStamp(Context(*row, "gameDate"), Context(*row, "gameTime"));
        const std::string created = Context(*row, "created");
        record.created = ParseStamp(created.substr(0, 10), created.size() > 11 ? created.substr(11) : std::string());
        const std::string minutes = Context(*row, "playMinutes");
        record.minutes = minutes.empty() ? -1 : std::atoi(minutes.c_str());
        return record;
    }
    // «<hero> · <location>»; the location alone for a record written without the hero.
    std::string Identity() const
    {
        if (location.empty())
            return fresh ? std::string() : row->label;
        return hero.empty() ? location : hero + " · " + location;
    }
};

// What the player sees as the record's name.
std::string Name(const Bindings &bindings, const Record &record)
{
    if (record.fresh)
        return bindings.Text("save.new");
    if (record.autosave)
        return bindings.Text("save.autosave") + " " + std::to_string(record.autosave);
    return record.Identity();
}

class SavesScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-saves";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        std::vector<Record> records;
        for (const Widget *row : admitted.All("ANDROID_SAVE_LIST"))
            records.push_back(Record::Of(row));
        const bool saving = std::any_of(records.begin(), records.end(), [](const Record &r) { return r.fresh; });

        // The game lists the records newest first: «Продолжить» takes the first.
        const Widget *newest = nullptr;
        for (const Record &record : records)
            if (!newest && !record.fresh)
                newest = record.row;
        // Autosaves are never an overwrite target; when loading they form their own group
        // at the top. The new record leads the list (V2-Save-Save). The player's choice
        // holds while it exists, else the new record when saving, else the game's selection.
        if (saving)
            std::erase_if(records, [](const Record &r) { return r.autosave != 0; });
        std::stable_sort(records.begin(), records.end(),
                         [](const Record &a, const Record &b) { return (a.fresh ? 0 : a.autosave ? 1 : 2) < (b.fresh ? 0 : b.autosave ? 1 : 2); });
        const Record *chosen = nullptr;
        for (const Record &record : records)
            if (record.row->row_id == chosen_)
                chosen = &record;
        for (const Record &record : records)
            if (!chosen && (saving ? record.fresh : record.row->value == "1"))
                chosen = &record;
        if (!chosen && !records.empty())
            chosen = &records.front();
        chosen_ = chosen ? chosen->row->row_id : std::string();

        // «Продолжить» from the title screen: the newest record (the game lists them newest
        // first) is loaded at once; with no record the list stays open.
        if (ContinueRequested())
        {
            SetContinueRequested(false);
            if (!saving && newest && newest->Offers("activate"))
            {
                asked_ = "load";
                asked_row_ = newest->label;
                asked_name_ = newest->label;
                chosen_ = newest->row_id;
                for (const Record &record : records)
                    if (record.row == newest)
                        chosen = &record;
                bindings.Send(*newest, "activate", newest->row_id);
            }
        }

        Json rows = Json::array();
        for (const Record &record : records)
        {
            const Json dim = Json{{"tone", "dim"}};
            Json cells = Json::array();
            if (record.fresh)
                cells = Json::array({Json{{"text", Name(bindings, record)}, {"sub", record.Identity()}, {"icon", "plus"}, {"iconTone", "brass"}}, "", "", ""});
            else
                cells = Json::array({record.autosave ? Json{{"text", Name(bindings, record)}, {"sub", record.Identity()}} : Json(Name(bindings, record)),
                                     Json{{"text", record.played.Valid() ? GameDate(bindings, record.played) : ""}, {"tone", "dim"}},
                                     Json{{"text", record.created.Valid() ? CreatedDate(bindings, record.created) : ""}, {"tone", "dim"}},
                                     record.autosave ? Json{{"icon", "energy"}, {"iconTone", "gold"}, {"iconOnly", true}} : Json("")});
            rows.push_back(Json{{"cells", cells}, {"selected", &record == chosen}, {"disabled", !record.row->enabled}});
        }
        bindings.On("saves", "row", [this, &bindings, records](const Event &event) {
            const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
            if (index < records.size())
                chosen_ = records[index].row->row_id;
            bindings.Rebuild();
        });

        Json view = Json::object();
        view["title"] = bindings.Text(saving ? "save.title_save" : "save.title_load");
        view["columns"] = Json::array({Json{{"label", bindings.Text("save.record")}, {"w", "1"}},
                                       Json{{"label", bindings.Text("save.game_date")}, {"w", "104px"}, {"align", "right"}},
                                       Json{{"label", bindings.Text("save.created")}, {"w", "150px"}, {"align", "right"}}, // «18 окт. 2025, 08:53» whole at 915 dp
                                       Json{{"label", ""}, {"w", "24px"}, {"align", "center"}}});
        view["rows"] = rows;
        view["detail"] = chosen ? Detail(bindings, *chosen, saving) : Json();
        view["confirm"] = Confirm(admitted, bindings, records);

        // The game's name editor: the record keeps the name the game proposes.
        const Widget *name_ok = admitted.One("CUSTOM_SAVE_BTN_OK");
        if (name_ok && name_ok->Offers("press") && named_ != admitted.model->revision)
        {
            named_ = admitted.model->revision;
            bindings.Send(*name_ok, "press");
        }

        const Widget *exit = admitted.One("BTN_EXIT");
        const Widget *no = admitted.One("CONFIRM_NO");
        bindings.On("header-back", "", [&bindings, exit, no](const Event &) {
            if (no && no->Offers("press"))
                bindings.Send(*no, "press");
            else
                bindings.Back(exit);
        });
        return view;
    }

  private:
    Json Detail(Bindings &bindings, const Record &record, bool saving)
    {
        Json facts = Json::array();
        if (record.played.Valid())
            facts.push_back(Json{{"k", bindings.Text("save.game_date")},
                                 {"v", GameDate(bindings, record.played) + (record.played.clock.empty() ? "" : ", " + record.played.clock)}});
        if (record.minutes >= 0)
            facts.push_back(Json{{"k", bindings.Text("save.play_time")}, {"v", PlayTime(bindings, record.minutes)}});
        if (record.created.Valid())
            facts.push_back(Json{{"k", bindings.Text("save.created")}, {"v", CreatedDate(bindings, record.created)}});

        const std::string primary = bindings.Text(record.fresh ? "save.save" : saving ? "save.overwrite" : "save.load");
        const Widget *row = record.row;
        const bool busy = bindings.Pending("primary") || bindings.Pending("delete");
        const char *kind = record.fresh ? "save" : saving ? "overwrite" : "load";
        const std::string name = Name(bindings, record);
        bindings.On("primary", "", [this, &bindings, row, kind, name](const Event &) {
            asked_ = kind;
            asked_row_ = row->label;
            asked_name_ = name;
            bindings.Send(*row, "activate", row->row_id);
        });
        bindings.On("delete", "", [this, &bindings, row, name](const Event &) {
            asked_ = "delete";
            asked_row_ = row->label;
            asked_name_ = name;
            bindings.Send(*row, "delete", row->row_id);
        });
        // V2-Save-*: the location as the title, the hero under it.
        const bool located = !record.location.empty();
        return Json{{"title", located ? record.location : Name(bindings, record)},
                    {"sub", located ? (record.autosave ? Name(bindings, record) + (record.hero.empty() ? "" : " · " + record.hero) : record.hero) : ""},
                    {"facts", facts},
                    {"primary", primary},
                    {"disabled", !row->Offers("activate")},
                    {"reason", row->unavailable_reason == "new" ? std::string() : row->unavailable_reason},
                    {"delete", row->Offers("delete") && !record.autosave}, // no quick delete of an autosave
                    {"busy", busy}};
    }

    // The game's own question, with its whole text. It answers what the player asked for
    // last, so it is titled by that; a question the player did not cause is titled by the
    // game's text alone. A load asked for from the title screen is confirmed at once.
    Json Confirm(const Admission &admitted, Bindings &bindings, const std::vector<Record> &records)
    {
        const Widget *yes = admitted.One("CONFIRM_YES");
        const Widget *no = admitted.One("CONFIRM_NO");
        if (!yes)
            return Json();
        std::string text;
        for (const Widget *line : admitted.All("CONFIRM_TEXT"))
            if (!line->label.empty())
                text += (text.empty() ? "" : "\n") + line->label;
        bool asked_here = asked_ == "save";
        for (const Record &record : records)
            asked_here = asked_here || (!record.fresh && record.row->label == asked_row_);
        if (asked_here && asked_ == "load" && FromTitle() && yes->Offers("press") && confirmed_ != admitted.model->revision)
        {
            confirmed_ = admitted.model->revision;
            bindings.Send(*yes, "press");
            return Json();
        }
        static const struct
        {
            const char *kind, *preset, *title;
        } kinds[] = {{"delete", "danger", StringKey("save.delete")},
                     {"overwrite", "danger", StringKey("save.overwrite")},
                     {"load", "confirm", StringKey("save.load")},
                     {"save", "confirm", StringKey("save.save")}};
        Json view{{"preset", "confirm"}, {"title", text}, {"subject", ""}, {"text", ""}, {"yes", yes->label},
                  {"no", no ? no->label : bindings.Text("common.cancel")}};
        for (const auto &kind : kinds)
            if (asked_here && asked_ == kind.kind)
                view = Json{{"preset", kind.preset},       {"title", bindings.Text(kind.title)},
                            {"subject", asked_ == "save" ? "" : asked_name_}, {"text", ""},
                            {"yes", bindings.Text(kind.title)}, {"no", no ? no->label : bindings.Text("common.cancel")}};
        bindings.Forward("confirm-confirm", *yes, "press");
        if (no)
        {
            bindings.Forward("confirm-cancel", *no, "press");
            bindings.Forward("confirm", *no, "press");
        }
        else
        {
            bindings.Ignore("confirm-cancel", "");
            bindings.Ignore("confirm", "dismiss");
        }
        return view;
    }

    std::string asked_;     // what the player asked for last: load, save, overwrite, delete
    std::string asked_row_; // and the record it was asked for (the game's name of it)
    std::string asked_name_; // as the player sees it
    std::string chosen_;    // row identity of the record in the detail column
    uint64_t named_ = 0;     // model revision whose name editor was accepted
    uint64_t confirmed_ = 0; // model revision whose load question was answered for the player
};

} // namespace

std::unique_ptr<Screen> CreateSavesScreen()
{
    return std::make_unique<SavesScreen>();
}

} // namespace storm::ui::screens
