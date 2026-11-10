// Hero section «Нации» (interface nationrelation, mockup V2-Hero-Nations): the relations
// matrix between the nations with its legend, the hero's reputation with each nation, the
// flag the ship will raise (‹ candidate ›, «Смена флага») and the game's two help texts as a
// parchment sheet. The six-section header switches to the other hero sections.
//
// The engine exports the matrix as source identities (xinterface mobile_nation_relations.hpp):
// ANDROID_NATION_COLUMNS (header cells), one ANDROID_NATION_ROW per nation with one cell per
// column ("self" or a picture of the "relations" atlas group), ANDROID_NATION_REPUTATION
// rows and the marker ANDROID_NATION_RELATIONS (value "1" = complete). Every control offers
// a one-shot capability token "nation@<opening>:<generation>:<press|help|close>"; the token
// itself is the engine command, and the engine consumes the generation with it, so at most
// one goes out per model revision.
#include "controls.h"

#include <map>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"ANDROID_NATION_RELATIONS", "nation_relations", true},
    {"ANDROID_NATION_COLUMNS", "nation_columns", true},
    {"ANDROID_NATION_ROW", "nation_row", true},
    {"ANDROID_NATION_REPUTATION", "nation_reputation"},
    {"ANDROID_NATION_SOURCE", "nation_source"},
    {"TITLE", "heading"},
    {"FlagPic", "image|nation_help"},
    {"INFO_CLICK", "nation_help"},
    {"LEFTCHANGE_NATION", "button"},
    {"RIGHTCHANGE_NATION", "button"},
    {"FLAGS", "button"},
    {"EXIT_BTN", "button"},
    {"I_CHARACTER", "button"},
    {"I_SHIP", "button"},
    {"I_QUESTBOOK", "button"},
    {"I_TRADEBOOK", "button"},
    {"I_ITEMS", "button"},
    {"STR_BUTTONS", "text"}, // captions of the section buttons: the header icons replace them
    {"ENG_TEXT", "text"},    // the reputation words again (ANDROID_NATION_REPUTATION has them)
    {"FRA_TEXT", "text"},
    {"SPA_TEXT", "text"},
    {"HOL_TEXT", "text"},
    {"ICON_*", "image"},
    {"TOOLTIP_*", "heading|text|image"},
    {"tooltip_fader", "nation_help_close"},
};

// The capability token of `verb` the widget offers in this snapshot, or empty.
std::string Token(const Widget *widget, const std::string &verb)
{
    if (!widget || !widget->enabled)
        return {};
    for (const std::string &action : widget->actions)
        if (action.rfind("nation@", 0) == 0 && action.size() > verb.size() &&
            action.compare(action.size() - verb.size() - 1, std::string::npos, ":" + verb) == 0)
            return action;
    return {};
}

std::string Trim(std::string text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::string Lower(std::string text)
{
    for (char &c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return text;
}

// Our own flag icon of a nation the game names in its NATIONS atlas group; empty for a
// nation this port has no flag for (a mod): the game's own picture is drawn then.
std::string FlagIcon(const std::string &image)
{
    static const char *const flags[][2] = {{"england", "nation-england"}, {"france", "nation-france"}, {"spain", "nation-spain"},
                                           {"holland", "nation-holland"}, {"pirate", "nation-pirates"}};
    for (const auto &pair : flags)
        if (Lower(image) == pair[0])
            return pair[1];
    return {};
}

// The row image of a nation: our flag, else the game's picture.
Json NationImage(const ImageRef *image)
{
    if (!image)
        return nullptr;
    const std::string icon = FlagIcon(image->image);
    if (!icon.empty())
        return Json{{"icon", icon}};
    return PictureOf(image);
}

// The legend glyph of a relation picture of the "relations" atlas group.
const char *RelationIcon(const std::string &image)
{
    const std::string name = Lower(image);
    if (name == "enemy")
        return "rel-war";
    if (name == "neutral")
        return "rel-neutral";
    if (name == "friend" || name == "ally")
        return "rel-ally";
    return "";
}

// The text of the game's help (RPGDescribe): a line holding only "~" is a blank paragraph.
std::string HelpText(const std::string &raw)
{
    std::string out, line;
    std::vector<std::string> lines;
    for (size_t at = 0; at <= raw.size(); ++at)
    {
        if (at == raw.size() || raw[at] == '\n')
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines.push_back(line);
            line.clear();
        }
        else
            line += raw[at];
    }
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (i > 0 && i + 1 < lines.size() && Trim(lines[i]) == "~")
            lines[i].clear();
        out += (i ? "\n" : "") + lines[i];
    }
    return out;
}

class NationsScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-nations";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const Widget *marker = admitted.One("ANDROID_NATION_RELATIONS");
        const Widget *columns = admitted.One("ANDROID_NATION_COLUMNS");
        const uint64_t revision = admitted.model->revision;
        if (marker->value != "1" && reported_ != marker->unavailable_reason)
        {
            reported_ = marker->unavailable_reason;
            bindings.Error("nationrelation: the engine could not read the relations (" + marker->unavailable_reason + ")");
        }
        // One capability per revision: the engine consumes its generation with the first.
        // `target` goes with the token: "<token>=<target>" (the flag a pick asks for).
        const auto send = [this, &bindings, revision](const Widget *widget, const std::string &verb, const std::string &target = {}) {
            const std::string token = Token(widget, verb);
            if (token.empty() || sent_revision_ == revision)
                return;
            sent_revision_ = revision;
            bindings.Send(*widget, target.empty() ? token : token + "=" + target);
        };

        Json view = Json::object();
        view["title"] = Trim(admitted.Label("TITLE"));

        // The matrix.
        Json heads = Json::array();
        if (columns->header_cells)
            for (const Cell &cell : *columns->header_cells)
                heads.push_back(Json{{"label", cell.text}, {"image", NationImage(cell.images.empty() ? nullptr : &cell.images[0])}});
        Json rows = Json::array();
        for (const Widget *row : admitted.All("ANDROID_NATION_ROW"))
        {
            Json cells = Json::array();
            for (const Cell &cell : row->cells)
            {
                if (cell.text == "self")
                {
                    cells.push_back(Json{{"self", true}});
                    continue;
                }
                const ImageRef *picture = cell.images.empty() ? nullptr : &cell.images[0];
                const std::string icon = picture ? RelationIcon(picture->image) : "";
                if (icon.empty() && unknown_.insert(picture ? picture->image : cell.text).second)
                    bindings.Error("nationrelation: unknown relation picture '" + (picture ? picture->image : cell.text) +
                                   "'; the game's picture is drawn");
                Json entry{{"label", row->label + " — " + cell.text}};
                if (!icon.empty())
                    entry["icon"] = icon;
                else
                    entry["image"] = PictureOf(picture);
                cells.push_back(std::move(entry));
            }
            rows.push_back(Json{{"label", row->label}, {"image", NationImage(row->FirstImage())}, {"cells", cells}});
        }
        view["columns"] = heads;
        view["rows"] = rows;
        view["legend"] = Json::array({Json{{"icon", "rel-ally"}, {"label", bindings.Text("nations.ally")}},
                                      Json{{"icon", "rel-neutral"}, {"label", bindings.Text("nations.neutral")}},
                                      Json{{"icon", "rel-war"}, {"label", bindings.Text("nations.war")}}});

        // The hero's reputation.
        Json reputation = Json::array();
        for (const Widget *row : admitted.All("ANDROID_NATION_REPUTATION"))
        {
            Json entry{{"label", row->label}, {"value", row->value}, {"image", NationImage(row->FirstImage())}};
            // The number behind the word, -100 … 100 (0 = neutral, below 0 hostile, ≤ -10 a bounty).
            const auto score = row->context.find("score");
            if (score != row->context.end())
            {
                const int value = std::clamp(std::atoi(score->second.c_str()), -100, 100);
                entry["bar"] = (value + 100) / 2;
                entry["tone"] = value < 0 ? "danger" : value > 20 ? "ok" : "";
            }
            reputation.push_back(std::move(entry));
        }
        view["reputationTitle"] = marker->label;
        view["reputation"] = reputation;

        // Help: the game's own texts behind the (i) of each panel.
        const Widget *help_reputation = nullptr;
        const Widget *help_flag = nullptr;
        for (const Widget *widget : admitted.known)
            if (widget->type == "nation_help")
            {
                if (SameNode(widget->node, "INFO_CLICK"))
                    help_reputation = widget;
                else if (SameNode(widget->node, "FlagPic"))
                    help_flag = widget;
            }
        view["helpReputation"] = !Token(help_reputation, "help").empty();
        view["helpFlag"] = !Token(help_flag, "help").empty();
        bindings.On("help-reputation", "click", [send, help_reputation](const Event &) { send(help_reputation, "help", {}); });
        bindings.On("help-flag", "click", [send, help_flag](const Event &) { send(help_flag, "help", {}); });

        // The flag the ship raises.
        const Widget *candidate = nullptr;
        for (const Widget *widget : admitted.All("FlagPic"))
            if (widget->type == "image")
                candidate = widget;
        const Widget *right = admitted.One("RIGHTCHANGE_NATION");
        const Widget *apply = admitted.One("FLAGS");
        view["flagTitle"] = bindings.Text("nations.flag");
        // The flags to choose from: the nations of the matrix, in its order. A tap picks one
        // directly (the engine presses › until the script's candidate is that flag).
        const std::string current = candidate && candidate->FirstImage() ? candidate->FirstImage()->image : "";
        // Which flags may be raised: "England=1,France=0,…" (interface adapter android_nation_flags.c).
        std::map<std::string, bool> allowed;
        const std::string listed = candidate && candidate->context.count("flags") ? candidate->context.at("flags") : "";
        for (size_t at = 0; at < listed.size();)
        {
            const size_t end = std::min(listed.find(',', at), listed.size());
            const std::string pair = listed.substr(at, end - at);
            const size_t eq = pair.find('=');
            if (eq != std::string::npos)
                allowed[Lower(pair.substr(0, eq))] = pair.substr(eq + 1) == "1";
            at = end + 1;
        }
        if (candidate && listed.empty() && !reported_flags_)
        {
            reported_flags_ = true;
            bindings.Error("nationrelation: the engine sent no flag choice (interface adapter android_nation_flags.c not loaded)");
        }
        Json flags = Json::array();
        std::vector<std::string> flag_ids;
        if (columns->header_cells)
            for (const Cell &cell : *columns->header_cells)
            {
                const std::string image = cell.images.empty() ? "" : cell.images[0].image;
                const std::string icon = FlagIcon(image);
                if (icon.empty())
                {
                    if (!image.empty() && unknown_.insert("flag:" + image).second)
                        bindings.Error("nationrelation: no flag for nation '" + image + "'; it cannot be picked here");
                    continue;
                }
                const bool can = allowed.count(Lower(image)) && allowed.at(Lower(image));
                flags.push_back(Json{{"id", image}, {"icon", icon}, {"label", cell.text}, {"selected", Lower(image) == Lower(current)}, {"disabled", !can}});
                flag_ids.push_back(image);
            }
        view["flags"] = flags;
        view["flagsDisabled"] = Token(right, "pick").empty();
        view["flag"] = candidate ? Json{{"label", candidate->label}} : Json(nullptr);
        bindings.On("flags", "pick", [send, right, current, allowed](const Event &event) {
            const auto can = allowed.find(Lower(event.value));
            if (Lower(event.value) == Lower(current) || can == allowed.end() || !can->second)
                return;
            send(right, "pick", event.value);
        });
        view["apply"] = apply ? Json{{"id", "flag-apply"},
                                     {"label", apply->label},
                                     {"variant", "primary"},
                                     {"disabled", Token(apply, "press").empty()},
                                     {"reason", apply->unavailable_reason}}
                              : Json(nullptr);
        bindings.On("flag-apply", "click", [send, apply](const Event &) { send(apply, "press"); });

        // The game's help sheet, open while the engine shows its tooltip.
        const Widget *close = admitted.One("tooltip_fader");
        std::string heading, text;
        for (const Widget *widget : admitted.known)
            if (std::string_view(widget->node).rfind("TOOLTIP_", 0) == 0 || std::string_view(widget->node).rfind("tooltip_", 0) == 0)
            {
                if (widget->type == "heading" && heading.empty())
                    heading = Trim(widget->label);
                else if (widget->type == "text" && !Trim(widget->label).empty())
                    text += (text.empty() ? "" : "\n\n") + HelpText(widget->label);
            }
        view["help"] = close && !text.empty() ? Json{{"title", heading.empty() ? bindings.Text("common.description") : heading}, {"text", text}}
                                              : Json(nullptr);
        const auto dismiss = [send, close](const Event &) { send(close, "close"); };
        bindings.On("help-close", "", dismiss);
        bindings.On("help", "dismiss", dismiss);
        help_open_ = !view["help"].is_null();
        close_ = close;
        send_ = send;

        // Header: the sections and ←.
        const std::vector<const Widget *> sections = [&] {
            std::vector<const Widget *> out;
            for (const char *node : {"I_CHARACTER", "I_SHIP", "I_QUESTBOOK", "I_TRADEBOOK", "I_ITEMS"})
                if (const Widget *button = admitted.One(node))
                    out.push_back(button);
            return out;
        }();
        bindings.On("header", "section", [send, sections](const Event &event) {
            for (const Widget *button : sections)
                if (SectionOf(*button) == event.value)
                    send(button, "press");
        });
        const Widget *exit = admitted.One("EXIT_BTN");
        bindings.On("header-back", "", [&bindings, send, exit, close, this](const Event &) {
            if (help_open_)
                send(close, "close");
            else if (!Token(exit, "press").empty())
                send(exit, "press");
            else
                bindings.Back(nullptr);
        });
        return view;
    }

    // Android back closes the game's help sheet first.
    bool OwnsBack() const override
    {
        return help_open_;
    }
    void Back(Bindings &) override
    {
        if (send_)
            send_(close_, "close", {});
    }

  private:
    uint64_t sent_revision_ = 0;
    std::string reported_;
    std::set<std::string> unknown_;
    bool help_open_ = false;
    bool reported_flags_ = false;
    const Widget *close_ = nullptr;
    std::function<void(const Widget *, const std::string &, const std::string &)> send_;
};

} // namespace

std::unique_ptr<Screen> CreateNationsScreen()
{
    return std::make_unique<NationsScreen>();
}

} // namespace storm::ui::screens
