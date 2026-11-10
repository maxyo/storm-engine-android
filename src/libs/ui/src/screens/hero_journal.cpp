// Hero section «Судовой журнал» (interface questbook; mockups V2-Hero-JournalQuests, -JournalDocs,
// -JournalAssets, -JournalStats). Fills the pack screen <s-journal>: a rail of four sections over
// the game's seven tabs (quests: active / completed; documents; assets: cash book, moored ships,
// warehouses; statistics), a list with the open entry on parchment, and the game's tables.
//
// The stock screen shows either the quest titles (QUEST_TITLE) or one quest's text (QUEST_TEXT),
// never both. The screen keeps the titles it last saw for each tab, so the list stays while a
// text is open; opening another entry first closes the text (the engine's back, which the
// script's ProcessCancelExit turns into "back to the titles"), then opens it.
#include "hero.h"

#include <map>

namespace storm::ui::screens
{

using namespace hero;

namespace
{

constexpr NodeRule kNodes[] = {
    {"TABBTN_QUEST", "image", true},
    {"TABBTN_*", "image"},
    {"TABSTR_*", "text"},
    {"TITLE", "heading"},
    {"QUEST_TITLE", "choice|text"},
    {"QUEST_TEXT", "text"},
    {"CURRENT_DATE_CAPTION", "text"},
    {"TABLE_*", "choice"},
    {"MAIN_BACK", "image"},
    {"EXIT_BTN", "button"},
    {"I_CHARACTER", "button"},
    {"I_SHIP", "button"},
    {"I_TRADEBOOK", "button"},
    {"I_NATIONS", "button"},
    {"I_ITEMS", "button"},
    {"STR_BUTTONS", "text"}, // captions of the section buttons: the header icons replace them
};

constexpr double kStepTimeout = 3.0;

// The game's tabs by section.
struct Tab
{
    const char *button, *section;
};
constexpr Tab kTabs[] = {{"TABBTN_QUEST", "quests"},       {"TABBTN_QUESTARJ", "quests"},  {"TABBTN_INFO", "docs"},
                         {"TABBTN_CASHBOOK", "assets"},    {"TABBTN_SHIP_PLACE", "assets"}, {"TABBTN_STOREBOOK", "assets"},
                         {"TABBTN_STATISTIC", "stats"}};
struct Section
{
    const char *id, *label, *first;
};
constexpr Section kSections[] = {{"quests", StringKey("journal.quests"), "TABBTN_QUEST"},
                                 {"docs", StringKey("journal.docs"), "TABBTN_INFO"},
                                 {"assets", StringKey("journal.assets"), "TABBTN_CASHBOOK"},
                                 {"stats", StringKey("journal.stats"), "TABBTN_STATISTIC"}};
// The statistics tables by local sub-tab (mockup: battles, by nation, other).
struct StatsTab
{
    const char *id, *label;
    std::vector<const char *> tables;
};
const StatsTab kStats[] = {{"battles", StringKey("journal.battles"), {"TABLE_SHIPCLASS", "TABLE_HUMAN"}},
                           {"nations", StringKey("journal.by_nation"), {"TABLE_NATION"}},
                           {"other", StringKey("journal.other"), {"TABLE_OTHER", "TABLE_MONEY"}}};
// The asset tables of the game's three asset tabs.
const std::vector<const char *> kAssetTables = {"TABLE_CREDIT", "TABLE_DEBIT", "TABLE_SHIP_PLACE", "TABLE_CITY", "TABLE_GOODS"};

// A date line of the game's quest text ("1 октября 1654 11:14"): starts with a digit, holds a
// four-digit year and a time.
bool DateLine(const std::string &line)
{
    if (line.empty() || line[0] < '0' || line[0] > '9' || line.size() > 48)
        return false;
    int digits = 0, longest = 0;
    for (const char c : line)
    {
        digits = c >= '0' && c <= '9' ? digits + 1 : 0;
        longest = std::max(longest, digits);
    }
    return longest >= 4 && line.find(':') != std::string::npos;
}

// The entries of a quest text: {date, text} per date line.
Json Entries(const std::string &text)
{
    Json entries = Json::array();
    std::string line;
    const auto flush = [&entries](const std::string &l) {
        const std::string trimmed = Trim(l);
        if (trimmed.empty())
            return;
        if (DateLine(trimmed) || entries.empty())
        {
            entries.push_back(Json{{"date", DateLine(trimmed) ? trimmed : std::string()}, {"text", DateLine(trimmed) ? std::string() : trimmed}});
            return;
        }
        std::string body = entries.back()["text"].get<std::string>();
        entries.back()["text"] = body.empty() ? trimmed : body + "\n" + trimmed;
    };
    for (const char c : text)
    {
        if (c == '\n')
        {
            flush(line);
            line.clear();
        }
        else if (c != '\r')
            line += c;
    }
    flush(line);
    return entries;
}

class JournalScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-journal";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        admission_ = admitted; // handlers run after Build returns (pointers into the router's model)
        a_ = &admission_;
        revision_ = admitted.model->revision;
        const Widget *text = admitted.One("QUEST_TEXT");
        text_open_ = text != nullptr;

        // The game's active tab and our section.
        std::string active;
        for (const Tab &tab : kTabs)
            if (const Widget *button = admitted.One(tab.button); button && button->value == "1")
                active = tab.button;
        std::string section = "quests";
        for (const Tab &tab : kTabs)
            if (active == tab.button)
                section = tab.section;

        // The titles of this tab: live, or the ones last seen while a text is open.
        const std::vector<const Widget *> titles = admitted.All("QUEST_TITLE");
        if (!text_open_)
        {
            auto &cache = titles_[active];
            cache.clear();
            for (const Widget *row : titles)
                if (row->type == "choice")
                    cache.push_back({row->row_id, Trim(row->label), row->index});
            open_.clear();
        }
        else if (!pending_open_.empty())
        {
            open_ = pending_open_; // the text the engine opened for us
            pending_open_.clear();
        }
        Walk(bindings);

        Json view = Json::object();
        view["title"] = Trim(admitted.Label("TITLE"));
        view["section"] = section;
        view["date"] = Trim(admitted.Label("CURRENT_DATE_CAPTION"));
        Json sections = Json::array();
        for (const Section &s : kSections)
        {
            Json entry{{"id", s.id}, {"label", bindings.Text(s.label)}, {"selected", section == s.id}};
            if (std::string(s.id) == "quests" && titles_.count("TABBTN_QUEST"))
                entry["count"] = std::to_string(titles_["TABBTN_QUEST"].size());
            sections.push_back(std::move(entry));
            const std::string id = s.id, first = s.first;
            bindings.On("section-" + id, "", [this, &bindings, section, id, first](const Event &) {
                if (id == section)
                    return;
                Go(bindings, first);
            });
        }
        view["sections"] = sections;

        // Sub-tabs of the section: the game's tabs, or our statistics split.
        Json subtabs = Json::array();
        if (section == "quests" || section == "assets")
            for (const Tab &tab : kTabs)
                if (section == tab.section)
                {
                    const Widget *button = admitted.One(tab.button);
                    std::string label = Trim(admitted.Label(std::string("TABSTR_") + (tab.button + 7)));
                    if (std::string(tab.button) == "TABBTN_QUEST")
                        label = bindings.Text("journal.active");
                    else if (std::string(tab.button) == "TABBTN_QUESTARJ")
                        label = bindings.Text("journal.completed");
                    if (button && !label.empty())
                        subtabs.push_back(Json{{"id", tab.button}, {"label", label}, {"selected", active == tab.button}});
                }
        if (section == "stats")
            for (const StatsTab &tab : kStats)
                subtabs.push_back(Json{{"id", tab.id}, {"label", bindings.Text(tab.label)}, {"selected", stats_ == tab.id}});
        view["subtabs"] = subtabs;
        bindings.On("journal-tabs", "select", [this, &bindings, section](const Event &event) {
            if (section == "stats")
            {
                stats_ = event.value;
                bindings.Rebuild();
                return;
            }
            Go(bindings, event.value);
        });

        // Quests and documents: the list and the open entry.
        if (section == "quests" || section == "docs")
        {
            Json list = Json::array();
            for (const Title &title : titles_[active])
                list.push_back(Json{{"cells", Json::array({Json{{"text", title.label}}})},
                                    {"selected", text_open_ && open_ == title.row}});
            view["list"] = list;
            view["empty"] = bindings.Text(section == "docs" ? "journal.no_docs" : "journal.no_quests");
            const std::vector<Title> shown = titles_[active];
            bindings.On("entries", "row", [this, &bindings, shown](const Event &event) {
                const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
                if (index >= shown.size() || (text_open_ && open_ == shown[index].row))
                    return;
                intent_ = {shown[index].row, bindings.Now()};
                sent_revision_ = 0;
                Walk(bindings);
                bindings.RebuildAfter(kStepTimeout);
            });
            if (text_open_)
            {
                std::string heading;
                for (const Title &title : titles_[active])
                    if (title.row == open_)
                        heading = title.label;
                view["entry"] = Json{{"title", heading}, {"entries", Entries(text->label)}};
            }
            else
                view["entry"] = nullptr;
        }

        // Assets and statistics: the game's tables.
        Json tables = Json::array();
        const auto table = [&](const char *node) {
            const std::vector<const Widget *> rows = admitted.All(node);
            if (rows.empty())
                return;
            Json columns = Json::array(), body = Json::array();
            const Widget *first = rows.front();
            const size_t count = first->cells.size();
            for (size_t c = 0; c < count; ++c)
            {
                std::string head = c < first->column_headers.size() ? Trim(first->column_headers[c]) : std::string();
                columns.push_back(Json{{"label", head}, {"w", c == 0 ? "2" : "1"}, {"align", c == 0 ? "left" : "right"}});
            }
            for (const Widget *row : rows)
            {
                Json cells = Json::array();
                for (size_t c = 0; c < count; ++c)
                {
                    Json cell{{"text", Trim(row->CellText(static_cast<int>(c)))}};
                    if (c < row->cells.size() && !row->cells[c].images.empty())
                        cell["image"] = PictureOf(&row->cells[c].images[0]);
                    cells.push_back(std::move(cell));
                }
                // Only the warehouse list selects (TABLE_CITY fills TABLE_GOODS); the other
                // tables only read.
                body.push_back(Json{{"cells", cells}, {"selected", std::string(node) == "TABLE_CITY" && row->value == "1"}});
            }
            const std::string id = std::string("table-") + node;
            // The sheet scrolls, not each table: a table is as tall as its rows (head + a 44 dp row each).
            tables.push_back(Json{{"id", id}, {"columns", columns}, {"rows", body}, {"height", 56 + 44 * static_cast<int>(body.size())}});
            if (std::string(node) != "TABLE_CITY")
            {
                bindings.Ignore(id, "row");
                tables.back()["interactive"] = false;
                return;
            }
            bindings.On(id, "row", [&bindings, rows](const Event &event) {
                const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
                if (index < rows.size() && rows[index]->value != "1" && rows[index]->Offers("select"))
                    bindings.Send(*rows[index], "select", rows[index]->row_id);
            });
        };
        if (section == "assets")
            for (const char *node : kAssetTables)
                table(node);
        if (section == "stats")
            for (const StatsTab &tab : kStats)
                if (stats_ == tab.id)
                    for (const char *node : tab.tables)
                        table(node);
        view["tables"] = tables;
        view["noTables"] = bindings.Text("journal.nothing");

        // Header: sections and ←.
        bindings.On("header", "section", [this, &bindings](const Event &event) {
            if (event.value == "journal")
                return;
            for (const Widget *widget : a_->known)
                if (widget->type == "button" && SectionOf(*widget) == event.value && widget->Offers("press"))
                    return bindings.Send(*widget, "press");
        });
        bindings.On("header-back", "", [this, &bindings](const Event &) {
            if (text_open_)
                Back(bindings);
            else
                bindings.Back(nullptr);
        });
        return view;
    }

    // While a text is open the engine's back first returns to the titles; the player's back
    // leaves the journal, so it goes on once the titles are back.
    bool OwnsBack() const override
    {
        return text_open_;
    }
    void Back(Bindings &bindings) override
    {
        leave_ = true;
        bindings.Back(nullptr);
    }

  private:
    struct Title
    {
        std::string row, label;
        int index = -1;
    };
    struct Intent
    {
        std::string row; // the entry to open; empty: none
        double since = 0.0;
    };

    Admission admission_;
    const Admission *a_ = nullptr;
    uint64_t revision_ = 0;
    uint64_t sent_revision_ = 0;
    bool text_open_ = false;
    bool leave_ = false;
    std::string open_; // the entry whose text is open
    std::string stats_ = "battles";
    std::map<std::string, std::vector<Title>> titles_; // by the game's tab
    Intent intent_;
    std::string pending_open_; // opened, not yet shown by the engine

    void Go(Bindings &bindings, const std::string &button)
    {
        const Widget *widget = a_->One(button);
        if (widget && widget->Offers("press") && widget->value != "1")
            bindings.Send(*widget, "press");
    }

    // Close the open text, then open the wanted entry from the titles.
    void Walk(Bindings &bindings)
    {
        if (leave_ && !text_open_)
        {
            leave_ = false;
            if (sent_revision_ != revision_)
                bindings.Back(nullptr);
            return;
        }
        if (intent_.row.empty())
            return;
        if (bindings.Now() - intent_.since > kStepTimeout)
        {
            bindings.Error("questbook: the engine did not answer a step (" + intent_.row + ")");
            intent_ = {};
            return;
        }
        if (sent_revision_ == revision_)
            return;
        if (text_open_)
        {
            sent_revision_ = revision_;
            bindings.Back(nullptr);
            return;
        }
        for (const Widget *row : a_->All("QUEST_TITLE"))
            if (row->type == "choice" && row->row_id == intent_.row && row->Offers("open"))
            {
                sent_revision_ = revision_;
                bindings.Send(*row, "open", row->row_id);
                pending_open_ = row->row_id;
                intent_ = {};
                return;
            }
        intent_ = {};
    }
};

} // namespace

std::unique_ptr<Screen> CreateJournalScreen()
{
    return std::make_unique<JournalScreen>();
}

} // namespace storm::ui::screens
