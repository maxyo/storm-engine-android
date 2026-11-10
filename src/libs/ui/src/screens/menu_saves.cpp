// Saves (interface "save_load", mockups V2-Save-Load, -Save, -Empty, -ConfirmLoad,
// -ConfirmDelete, -Overwrite): every record of the profile in one C-Table, the chosen
// record in the detail column with its primary action and the bare trash glyph. Fills the
// pack screen <s-saves>.
//
// The engine lists the records as ANDROID_SAVE_LIST rows (label = the name the game gave
// the record, `delete` / `activate` verbs; the save slot of a new record has the reason
// "new"). The game's own page of ten slots publishes SAVENOTES in triples per slot:
// name, in-game "HH:MM  DD.MM.YYYY", created "HH:MM:SS DD.MM.YYYY"; SAVEINFO 1 is the
// play time of the record the game has selected. The game asks its own questions
// (CONFIRM_*): they become the C-Dialog. The game's name editor (CUSTOM_SAVE_*) is
// accepted as it comes: records are named by the game, never by the player.
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
    {"ANDROID_SAVE_LIST", "save", true},
    {"TITLE", "heading|text"},
    {"SAVENOTES", "text"},
    {"SAVEINFO", "text"},
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
    // Drawn by the game's page only: its slot pager (we list every record), the profile
    // (there are no profiles in this port), the officers of the selected record.
    {"SAVE_SELECTER", "stepper"},
    {"BTN_PROFILE", "button"},
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

// "12:20  01.10.1654" (in game) and "08:53:21 18.10.2026" (created).
Stamp ParseStamp(const std::string &text)
{
    Stamp stamp;
    const size_t colon = text.find(':');
    const size_t dot = text.find('.');
    if (colon == std::string::npos || dot == std::string::npos || colon < 2 || dot < 2)
        return stamp;
    stamp.clock = text.substr(colon - 2, 5);
    stamp.day = std::atoi(text.c_str() + dot - 2);
    stamp.month = std::atoi(text.c_str() + dot + 1);
    const size_t second = text.find('.', dot + 1);
    stamp.year = second == std::string::npos ? 0 : std::atoi(text.c_str() + second + 1);
    return stamp;
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

// "<caption>: 6 hours 42 min." → {caption, "6 ч 42 мин"}
std::pair<std::string, std::string> PlayTime(const Bindings &bindings, const std::string &text)
{
    const size_t colon = text.find(':');
    if (colon == std::string::npos)
        return {};
    std::vector<int> numbers;
    for (size_t i = colon; i < text.size(); ++i)
        if (text[i] >= '0' && text[i] <= '9')
        {
            numbers.push_back(std::atoi(text.c_str() + i));
            while (i < text.size() && text[i] >= '0' && text[i] <= '9')
                ++i;
        }
    if (numbers.size() != 2)
        return {};
    std::string value = numbers[0] > 0 ? std::to_string(numbers[0]) + " " + bindings.Text("save.hours") + " " : std::string();
    return {text.substr(0, colon), value + std::to_string(numbers[1]) + " " + bindings.Text("save.minutes")};
}

struct Record
{
    const Widget *row = nullptr;
    bool fresh = false;
    Stamp played, created;
};

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
            records.push_back({row, row->unavailable_reason == "new"});
        const bool saving = std::any_of(records.begin(), records.end(), [](const Record &r) { return r.fresh; });
        Dates(admitted, records);

        // The new record leads the list (V2-Save-Save); the player's choice holds while it
        // exists, else the new record when saving, else the one the game has selected.
        std::stable_partition(records.begin(), records.end(), [](const Record &r) { return r.fresh; });
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
            if (!saving && !records.empty() && records.front().row->Offers("activate"))
            {
                asked_ = "load";
                asked_row_ = records.front().row->label;
                chosen_ = records.front().row->row_id;
                chosen = &records.front();
                bindings.Send(*records.front().row, "activate", records.front().row->row_id);
            }
        }

        Json rows = Json::array();
        for (const Record &record : records)
        {
            Json cells = record.fresh ? Json::array({Json{{"text", bindings.Text("save.new")}, {"icon", "plus"}, {"iconTone", "brass"}}, "", ""})
                                      : Json::array({record.row->label,
                                                     Json{{"text", record.played.Valid() ? GameDate(bindings, record.played) : ""}, {"tone", "dim"}},
                                                     Json{{"text", record.created.Valid() ? CreatedDate(bindings, record.created) : ""}, {"tone", "dim"}}});
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
                                       Json{{"label", bindings.Text("save.created")}, {"w", "132px"}, {"align", "right"}}});
        view["rows"] = rows;
        view["detail"] = chosen ? Detail(admitted, bindings, *chosen, saving) : Json();
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
    // Dates of the game's page of slots, matched to the records by name: the page shows
    // ten consecutive records starting at the one whose name opens it.
    static void Dates(const Admission &admitted, std::vector<Record> &records)
    {
        std::vector<std::array<std::string, 3>> slots;
        for (const Widget *note : admitted.All("SAVENOTES"))
        {
            if (note->index < 0)
                continue;
            const size_t slot = static_cast<size_t>(note->index) / 3;
            if (slots.size() <= slot)
                slots.resize(slot + 1);
            slots[slot][static_cast<size_t>(note->index) % 3] = note->label;
        }
        if (slots.empty())
            return;
        // A slot without a valid in-game date is empty or not filled in yet (the game fills
        // its page over a few frames).
        const auto filled = [&slots](size_t s) { return ParseStamp(slots[s][1]).Valid(); };
        for (size_t first = 0; first < records.size(); ++first)
        {
            bool fits = !records[first].fresh, any = false;
            for (size_t s = 0; s < slots.size() && first + s < records.size() && fits; ++s)
                if (filled(s))
                {
                    fits = !records[first + s].fresh && slots[s][0] == records[first + s].row->label;
                    any = true;
                }
            if (!fits || !any)
                continue;
            for (size_t s = 0; s < slots.size() && first + s < records.size(); ++s)
                if (filled(s))
                {
                    records[first + s].played = ParseStamp(slots[s][1]);
                    records[first + s].created = ParseStamp(slots[s][2]);
                }
            return;
        }
    }

    Json Detail(const Admission &admitted, Bindings &bindings, const Record &record, bool saving)
    {
        Json facts = Json::array();
        if (record.played.Valid())
            facts.push_back(Json{{"k", bindings.Text("save.game_date")}, {"v", GameDate(bindings, record.played) + ", " + record.played.clock}});
        if (record.row->value == "1")
            for (const Widget *info : admitted.All("SAVEINFO"))
                if (info->index == 1)
                {
                    const auto [label, value] = PlayTime(bindings, info->label);
                    if (!value.empty())
                        facts.push_back(Json{{"k", label}, {"v", value}});
                }
        if (record.created.Valid())
            facts.push_back(Json{{"k", bindings.Text("save.created")}, {"v", CreatedDate(bindings, record.created)}});

        const std::string primary = bindings.Text(record.fresh ? "save.save" : saving ? "save.overwrite" : "save.load");
        const Widget *row = record.row;
        const bool busy = bindings.Pending("primary") || bindings.Pending("delete");
        const char *kind = record.fresh ? "save" : saving ? "overwrite" : "load";
        bindings.On("primary", "", [this, &bindings, row, kind](const Event &) {
            asked_ = kind;
            asked_row_ = row->label;
            bindings.Send(*row, "activate", row->row_id);
        });
        bindings.On("delete", "", [this, &bindings, row](const Event &) {
            asked_ = "delete";
            asked_row_ = row->label;
            bindings.Send(*row, "delete", row->row_id);
        });
        return Json{{"title", record.fresh ? bindings.Text("save.new") : row->label},
                    {"sub", ""},
                    {"facts", facts},
                    {"primary", primary},
                    {"disabled", !row->Offers("activate")},
                    {"reason", row->unavailable_reason == "new" ? std::string() : row->unavailable_reason},
                    {"delete", row->Offers("delete")},
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
                            {"subject", asked_ == "save" ? "" : asked_row_}, {"text", text},
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
    std::string asked_row_; // and the record it was asked for
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
