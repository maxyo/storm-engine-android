// Shipyard (interface "shipyard", mockups V2-Shipyard-*): our ship and the yard's catalogue
// on the left, the ship being looked at (ours or one on offer) on the right, the game's own
// windows (confirmation, repair, captain, officer, cannons, help) as sheets. Fills <s-shipyard>.
//
// The engine describes the shipyard with special widgets (xinterface/mobile_shipyard.inc):
// `shipyard_state` (index 1 = protocol version) carries the window mode and the selected
// owner in its context, `shipyard_catalog` the catalogue captions, `shipyard_control` the
// buttons that are not plain widgets (repair steps, help, close). Every action a widget
// offers is a capability token "yard@<opening>:<generation>:<verb>" that is valid for that
// snapshot only: the screen sends the token itself, at most one per model revision, and
// nothing while the engine says the state is not ready. No money, eligibility or repair
// rule lives here; the game decides and answers with the next model.
#include "controls.h"

#include <algorithm>
#include <cstdlib>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"ANDROID_SHIPYARD_STATE", "shipyard_state", true},
    {"ANDROID_SHIPYARD_CATALOG", "shipyard_catalog", true},
    {"ANDROID_SHIPYARD_CLOSE", "shipyard_control"},
    {"SHIPS_SCROLL", "choice|shipyard_control", true},
    {"BUTTON_BUY", "button", true},
    {"BUTTON_SELL", "button", true},
    {"BUTTON_REPAIR", "button", true},
    {"EXIT_BTN", "shipyard_control|button"},
    {"TITLE", "heading|text"},
    {"MAIN_BACK", "image"},
    {"SHIP_BIG_PICTURE", "image|shipyard_control"},
    {"MAIN_CHARACTER_PICTURE", "image|shipyard_control"},
    {"TABLE_SHIPYARD", "choice|shipyard_control"},
    {"TABLE_OTHER", "choice"},
    {"HERO_NAME", "text"},
    {"HERO_RANK", "text"},
    {"SHIP_RANK", "text"},
    {"EDIT_BOX", "edit"},
    {"FRAME_INFO_CAPTION", "text|heading"},
    {"INFO_TEXT", "text"},
    {"MONEY_TEXT", "text"},
    {"MONEY_PIC", "image"},
    {"MSG_WINDOW_CAPTION", "text|heading"},
    {"MSG_WINDOW_TEXT", "text"},
    {"MSG_OK", "button"},
    {"MSG_CANCEL", "button"},
    {"MSG_EXIT_BTN", "button"},
    {"REPAIR_WINDOW_CAPTION", "text|heading"},
    {"REPAIR_WINDOW_TEXT", "text"},
    {"REPAIR_QTY_H", "text"},
    {"REPAIR_QTY_S", "text"},
    {"REPAIR_LEFT_H", "shipyard_control|button"},
    {"REPAIR_RIGHT_H", "shipyard_control|button"},
    {"REPAIR_LEFT_S", "shipyard_control|button"},
    {"REPAIR_RIGHT_S", "shipyard_control|button"},
    {"REPAIR_OK", "button"},
    {"REPAIR_ALL", "button"},
    {"REPAIR_CANCEL", "button"},
    {"REPAIR_EXIT_BTN", "button"},
    {"REPAIR_*", "image"},
    {"OFFICERS_WINDOW_CAPTION", "text|heading"},
    {"PASSENGERSLIST", "choice"},
    {"OFFICER_NAME", "text"},
    {"TABLE_SMALLSKILL", "choice|text"},
    {"TABLE_SMALLOTHER", "choice|text"},
    {"ACCEPT_ADD_OFFICER", "button"},
    {"CANCEL_ADD_OFFICER", "button"},
    {"OFFICER_EXIT_BTN", "button"},
    {"RPG_OFFICERS_WINDOW_CAPTION", "text|heading"},
    {"RPG_OFFICER_NAME", "text"},
    {"RPG_TABLE_SMALLSKILL", "choice|text"},
    {"RPG_TABLE_SMALLOTHER", "choice|text"},
    {"CANNONS_CAPTION", "text|heading"},
    {"CANNONS_TEXT", "text"},
    {"CANNONS_QTY_F", "text"},
    {"CANNONS_QTY_B", "text"},
    {"CANNONS_QTY_L", "text"},
    {"CANNONS_QTY_R", "text"},
    {"CANNONS_CANCEL", "button"},
    {"CANNONS_EXIT_BTN", "button"},
    {"CANNONS_SHIP", "image"},
    {"CANNONS_PIC", "image"},
    {"TOOLTIP_*", "text|heading|image"},
};

// The capability token of `verb` that the widget offers in this snapshot, or empty.
std::string Token(const Widget *widget, const std::string &verb)
{
    if (!widget || !widget->enabled)
        return {};
    for (const std::string &action : widget->actions)
        if (action.rfind("yard@", 0) == 0 && action.size() > verb.size() &&
            action.compare(action.size() - verb.size() - 1, std::string::npos, ":" + verb) == 0)
            return action;
    return {};
}

std::string Context(const Widget *widget, const std::string &key)
{
    if (!widget)
        return {};
    const auto found = widget->context.find(key);
    return found == widget->context.end() ? std::string() : found->second;
}

std::string Trim(std::string text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

// The game writes line breaks in table cells as the two characters "\n".
std::string Lines(std::string text)
{
    for (size_t at = text.find("\\n"); at != std::string::npos; at = text.find("\\n", at))
        text.replace(at, 2, "\n");
    std::replace(text.begin(), text.end(), '\r', '\n');
    return text;
}

// "23824" → "23 824"; any other text stays as it is.
std::string Grouped(const std::string &text)
{
    const size_t sign = !text.empty() && text[0] == '-' ? 1 : 0;
    if (text.size() <= sign + 3 || text.find_first_not_of("0123456789", sign) != std::string::npos)
        return text;
    std::string out = text.substr(0, sign);
    for (size_t i = sign; i < text.size(); ++i)
    {
        if (i > sign && (text.size() - i) % 3 == 0)
            out += ' ';
        out += text[i];
    }
    return out;
}

// "11 575" never breaks inside the number.
std::string KeepTogether(std::string text)
{
    for (size_t at = text.find(' '); at != std::string::npos; at = text.find(' ', at))
        text.replace(at, 1, "\u00a0");
    return text;
}

int FirstNumber(const std::string &text)
{
    const size_t first = text.find_first_of("0123456789");
    return first == std::string::npos ? -1 : std::atoi(text.c_str() + first);
}

// "Тартана\n\nПальмье" → {type, proper name}.
std::pair<std::string, std::string> TypeAndName(const std::string &cell)
{
    const std::string text = Lines(cell);
    const size_t gap = text.find('\n');
    if (gap == std::string::npos)
        return {Trim(text), {}};
    return {Trim(text.substr(0, gap)), Trim(text.substr(gap))};
}

class ShipyardScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-shipyard";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const Widget *state = Typed(admitted, "ANDROID_SHIPYARD_STATE", "shipyard_state");
        // Version 1 of the engine's shipyard context; anything else is not understood, and a
        // state that is not ready (frames still settling) is drawn but offers nothing.
        const bool ready = state && state->index == 1 && state->value == "1" && Context(state, "ready") == "1";
        const std::string mode = Context(state, "mode");
        const uint64_t revision = admitted.model ? admitted.model->revision : 0;
        if (state && state->index != 1 && logged_version_ != state->index)
        {
            logged_version_ = state->index;
            bindings.Error("unknown shipyard state version " + std::to_string(state->index));
        }
        // Frames settle in a moment; a state that stays unusable is reported, with the engine's reason.
        if (ready || !state)
        {
            not_ready_since_ = -1.0;
            reported_ = false;
        }
        else
        {
            if (not_ready_since_ < 0.0)
                not_ready_since_ = bindings.Now();
            const double waited = bindings.Now() - not_ready_since_;
            if (waited < kSettling)
                bindings.RebuildAfter(kSettling - waited);
            else if (!reported_)
            {
                reported_ = true;
                bindings.Error("the engine's shipyard state is not usable (" +
                               (state->unavailable_reason.empty() ? std::string("not ready") : state->unavailable_reason) + ")");
            }
        }
        // A capability may be used once: the engine consumes the generation with it.
        const auto send = [this, &bindings, ready, revision](const Widget *widget, const std::string &verb) {
            const std::string token = ready ? Token(widget, verb) : std::string();
            if (token.empty() || sent_revision_ == revision)
                return false;
            sent_revision_ = revision;
            bindings.Send(*widget, token);
            return true;
        };
        const auto offers = [ready](const Widget *widget, const std::string &verb) { return ready && !Token(widget, verb).empty(); };
        const bool main = mode == "main" || mode.empty();

        Json view = Json::object();
        const std::string title = Trim(admitted.Label("TITLE"));
        view["title"] = title.empty() ? bindings.Text("yard.title") : title;

        // Whose ship: the fleet slots.
        std::vector<const Widget *> fleet;
        for (const Widget *slot : admitted.All("SHIPS_SCROLL"))
            if (slot->type == "choice")
                fleet.push_back(slot);
        size_t current = 0;
        for (size_t i = 0; i < fleet.size(); ++i)
            if (fleet[i]->value == "1")
                current = i;
        const Widget *owned = fleet.empty() ? nullptr : fleet[current];
        const bool empty_slot = Context(owned, "ownerKind") == "EMPTY_SLOT";
        const auto slot_name = [&bindings](const Widget *slot) {
            return Context(slot, "ownerKind") == "EMPTY_SLOT" ? bindings.Text("yard.empty_slot")
                   : !Context(slot, "shipName").empty()       ? Context(slot, "shipName")
                                                              : slot->label;
        };
        view["subject"] = Json();
        if (fleet.size() > 1)
        {
            view["subject"] = Json{{"label", slot_name(owned)},
                                   {"sub", std::to_string(current + 1) + " " + bindings.Text("common.of") + " " + std::to_string(fleet.size())},
                                   {"prev", main && current > 0 && offers(fleet[current - 1], "select")},
                                   {"next", main && current + 1 < fleet.size() && offers(fleet[current + 1], "select")},
                                   {"list", false}};
            const auto choose = [send, fleet](size_t index) {
                if (index < fleet.size())
                    send(fleet[index], "select");
            };
            bindings.On("header", "subject-prev", [choose, current](const Event &) { choose(current - 1); });
            bindings.On("header", "subject-next", [choose, current](const Event &) { choose(current + 1); });
            bindings.Ignore("header", "subject-list");
        }
        Json stats = Json::array();
        if (const std::string money = Trim(admitted.Label("MONEY_TEXT")); !money.empty())
            stats.push_back(Json{{"icon", "money"}, {"value", money}, {"tone", "gold"}});
        view["stats"] = stats;

        // Our ship: its picture and trade-in value, repair and sell.
        const std::string quote = Context(owned, "saleQuote");
        Json own{{"title", owned ? slot_name(owned) : std::string()},
                 {"sub", !quote.empty() ? bindings.Text("yard.trade_in") + " " + KeepTogether(quote) : Context(owned, "class")},
                 {"image", PictureOf(owned ? owned->FirstImage() : nullptr)},
                 {"actions", Json::array()}};
        for (const auto &[node, id] : {std::pair{"BUTTON_REPAIR", "repair"}, std::pair{"BUTTON_SELL", "sell"}})
            if (const Widget *button = Typed(admitted, node, "button"))
            {
                own["actions"].push_back(Json{{"id", id},
                                              {"label", button->label},
                                              {"disabled", !main || !offers(button, "press")},
                                              {"reason", button->unavailable_reason},
                                              {"busy", bindings.Pending(id)}});
                bindings.On(id, "", [send, button](const Event &) { send(button, "press"); });
            }
        view["own"] = own;

        // The catalogue: ship, class, price (the card on the right has the rest).
        const Widget *catalog = Typed(admitted, "ANDROID_SHIPYARD_CATALOG", "shipyard_catalog");
        const auto caption = [catalog](int column) { return catalog ? Trim(Lines(catalog->CellText(column))) : std::string(); };
        const bool yard_preview = Context(state, "previewKind") == "yard";
        std::vector<const Widget *> offers_list;
        Json rows = Json::array();
        const Widget *chosen = nullptr;
        for (const Widget *offer : admitted.All("TABLE_SHIPYARD"))
        {
            if (offer->type != "choice")
                continue;
            const auto [type, name] = TypeAndName(offer->CellText(0));
            const Cell *first = nullptr;
            for (const Cell &cell : offer->cells)
                if (cell.column == 0)
                    first = &cell;
            const bool selected = yard_preview && offer->value == "1";
            if (selected)
                chosen = offer;
            rows.push_back(Json{{"cells", Json::array({Json{{"text", type}, {"sub", name.empty() ? name : "«" + name + "»"}, {"image", PictureOf(first && !first->images.empty() ? &first->images.front() : nullptr)}, {"icon", "ship"}},
                                                       Json{{"text", offer->CellText(1)}},
                                                       Json{{"text", Grouped(offer->CellText(4))}, {"bold", true}}})},
                                {"selected", selected},
                                {"disabled", !offers(offer, "select")},
                                {"reason", offer->unavailable_reason}});
            offers_list.push_back(offer);
        }
        view["catalog"] = Json{{"columns", Json::array({Json{{"label", caption(0)}, {"w", 3}},
                                                        Json{{"label", caption(1)}, {"w", "64px"}, {"align", "right"}},
                                                        Json{{"label", caption(4)}, {"w", "96px"}, {"align", "right"}}})},
                               {"rows", rows},
                               {"empty", rows.empty() ? bindings.Text("yard.no_ships") : std::string()}};
        bindings.On("catalog", "row", [send, offers_list, main](const Event &event) {
            const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
            if (main && index < offers_list.size() && offers_list[index]->value != "1")
                send(offers_list[index], "select");
        });
        bindings.Ignore("catalog", "select");

        // The ship being looked at.
        const bool preview = yard_preview || Context(owned, "ownerKind") == "OWNER_SHIP";
        view["detail"] = Json();
        view["empty"] = empty_slot ? bindings.Text("yard.empty_slot") : Context(owned, "shipName");
        if (preview)
        {
            std::string name = yard_preview && chosen ? TypeAndName(chosen->CellText(0)).second : std::string();
            if (!yard_preview)
                if (const Widget *edit = Typed(admitted, "EDIT_BOX", "edit"))
                    name = edit->value;
            const std::string type = Trim(admitted.Label("FRAME_INFO_CAPTION"));
            const std::string rank = Trim(admitted.Label("SHIP_RANK"));
            Json facts = Json::array();
            const Widget *cannon_type = nullptr;
            for (const Widget *row : admitted.All("TABLE_OTHER"))
            {
                if (row->row_id == "CannonType")
                {
                    cannon_type = row;
                    continue;
                }
                if (!row->CellText(1).empty())
                    facts.push_back(Json{{"k", row->CellText(1)}, {"v", row->CellText(2)}});
            }
            const Widget *picture = Control(admitted, "SHIP_BIG_PICTURE", -2);
            Json detail{{"title", name.empty() ? type : type + " «" + name + "»"},
                        {"sub", rank.empty() ? std::string() : bindings.Text("yard.class") + " " + rank},
                        {"image", PictureOf(Typed(admitted, "SHIP_BIG_PICTURE", "image") ? Typed(admitted, "SHIP_BIG_PICTURE", "image")->FirstImage() : nullptr)},
                        {"help", offers(picture, "help")},
                        {"helpLabel", bindings.Text("common.description")},
                        {"facts", facts},
                        {"cannons", Json()},
                        {"text", Lines(admitted.Label("INFO_TEXT"))},
                        {"primary", Json()}};
            bindings.On("detail-help", "", [send, picture](const Event &) { send(picture, "help"); });
            if (cannon_type)
            {
                detail["cannons"] = Json{{"title", cannon_type->CellText(1)},
                                         {"value", cannon_type->CellText(2)},
                                         {"disabled", !main || !offers(cannon_type, "activate")},
                                         {"reason", cannon_type->unavailable_reason}};
                bindings.On("cannons", "", [send, cannon_type](const Event &) { send(cannon_type, "activate"); });
            }
            const Widget *buy = Typed(admitted, "BUTTON_BUY", "button");
            if (yard_preview && buy)
            {
                detail["primary"] = Json{{"label", buy->label},
                                         {"sub", chosen ? Grouped(chosen->CellText(4)) : std::string()},
                                         {"disabled", !main || !offers(buy, "press")},
                                         {"reason", buy->unavailable_reason},
                                         {"busy", bindings.Pending("primary")}};
                bindings.On("primary", "", [send, buy](const Event &) { send(buy, "press"); });
            }
            view["detail"] = detail;
        }

        // The game's windows.
        const Widget *close = nullptr;
        std::string close_verb = "press";
        view["sheet"] = Json();
        if (mode == "confirm")
        {
            close = Typed(admitted, "MSG_CANCEL", "button");
            const Widget *ok = Typed(admitted, "MSG_OK", "button");
            const bool selling = Context(state, "messageMode") == "ShipSell";
            const Widget *meaning = Typed(admitted, selling ? "BUTTON_SELL" : "BUTTON_BUY", "button");
            Json actions = Json::array();
            if (close)
                actions.push_back(Json{{"id", "sheet-cancel"}, {"label", close->label}, {"variant", "quiet"}, {"disabled", !offers(close, "press")}});
            if (ok)
                actions.push_back(Json{{"id", "sheet-ok"},
                                       {"label", meaning && !meaning->label.empty() ? meaning->label : ok->label},
                                       {"variant", selling ? "danger" : "primary"},
                                       {"disabled", !offers(ok, "press")},
                                       {"reason", ok->unavailable_reason}});
            view["sheet"] = Json{{"kind", "confirm"},
                                 {"title", Trim(admitted.Label("MSG_WINDOW_CAPTION"))},
                                 {"text", Trim(admitted.Label("MSG_WINDOW_TEXT"))},
                                 {"actions", actions},
                                 {"width", 400}};
            bindings.On("sheet-ok", "", [send, ok](const Event &) { send(ok, "press"); });
        }
        else if (mode == "repair")
        {
            close = Typed(admitted, "REPAIR_CANCEL", "button");
            Json parts = Json::array();
            for (const auto &[kind, stat] : {std::pair{'H', "Hull"}, std::pair{'S', "Sails"}})
            {
                const int value = FirstNumber(admitted.Label(std::string("REPAIR_QTY_") + kind));
                Repair &part = kind == 'H' ? hull_ : sails_;
                if (last_mode_ != "repair")
                {
                    part = Repair();
                    part.base = value; // the window opens with nothing ordered yet
                }
                std::string label;
                for (const Widget *row : admitted.All("TABLE_OTHER"))
                    if (row->row_id == stat)
                        label = row->CellText(1);
                const std::string id = kind == 'H' ? "hull" : "sails";
                parts.push_back(Json{{"id", id},
                                     {"q", Json{{"value", part.target >= 0 ? part.target : std::max(0, value)},
                                                {"min", std::max(0, part.base)},
                                                {"max", 100},
                                                {"unit", "%"},
                                                {"label", label},
                                                {"steps", Json::array({1, 5})},
                                                {"disabled", !ready}}}});
                const auto set = [this, &bindings, kind](const Event &event) {
                    (kind == 'H' ? hull_ : sails_).target = std::atoi(event.value.c_str());
                    bindings.Rebuild();
                };
                bindings.On(id, "change", set);
                bindings.On(id, "commit", set);
                // One step toward the amount asked for per model revision: +-5 while far, then +-1.
                if (part.target >= 0 && value >= 0 && part.target != value)
                {
                    if (part.stepped == revision)
                    {
                        // the step of this revision is in flight
                    }
                    else if (part.last_value == value && part.stepped != 0)
                        part.target = -1; // the game did not move (money, full repair): stop there
                    else
                    {
                        const int delta = part.target - value;
                        const std::string side = std::string(delta > 0 ? "REPAIR_RIGHT_" : "REPAIR_LEFT_") + kind;
                        const bool five = std::abs(delta) >= 5;
                        const Widget *step = Control(admitted, side, five ? -5 : -1);
                        if (send(step, five ? "step5" : "press"))
                        {
                            part.stepped = revision;
                            part.last_value = value;
                        }
                        else if (!offers(step, five ? "step5" : "press"))
                            part.target = -1;
                    }
                }
                else if (part.target == value)
                    part.target = -1;
            }
            const Widget *all = Typed(admitted, "REPAIR_ALL", "button");
            const Widget *ok = Typed(admitted, "REPAIR_OK", "button");
            Json actions = Json::array();
            if (all)
                actions.push_back(Json{{"id", "sheet-all"}, {"label", all->label}, {"disabled", !offers(all, "press")}, {"reason", all->unavailable_reason}});
            if (ok)
                actions.push_back(Json{{"id", "sheet-ok"},
                                       {"label", ok->label},
                                       {"sub", Trim(admitted.Label("REPAIR_WINDOW_TEXT"))},
                                       {"variant", "primary"},
                                       {"disabled", !offers(ok, "press")},
                                       {"reason", ok->unavailable_reason}});
            view["sheet"] = Json{{"kind", "repair"}, {"title", Trim(admitted.Label("REPAIR_WINDOW_CAPTION"))}, {"closable", true}, {"parts", parts}, {"actions", actions}, {"width", 460}};
            bindings.On("sheet-all", "", [send, all](const Event &) { send(all, "press"); });
            bindings.On("sheet-ok", "", [send, ok](const Event &) { send(ok, "press"); });
        }
        else if (mode == "captain" || mode == "rpg")
        {
            const bool rpg = mode == "rpg";
            const std::string prefix = rpg ? "RPG_" : "";
            Json candidates = Json::array();
            if (!rpg)
            {
                close = Typed(admitted, "CANCEL_ADD_OFFICER", "button");
                size_t i = 0;
                for (const Widget *person : admitted.All("PASSENGERSLIST"))
                {
                    const std::string id = "candidate-" + std::to_string(i++);
                    candidates.push_back(Json{{"id", id},
                                              {"title", person->label},
                                              {"image", PictureOf(person->FirstImage())},
                                              {"selected", person->value == "1"},
                                              {"disabled", !offers(person, "select")},
                                              {"reason", person->unavailable_reason}});
                    bindings.On(id, "", [send, person](const Event &) {
                        if (person->value != "1")
                            send(person, "select");
                    });
                }
            }
            else
            {
                close = Control(admitted, "ANDROID_SHIPYARD_CLOSE", -1);
                close_verb = "close";
            }
            const bool vacant = !rpg && Context(state, "captain").empty();
            Json facts = Json::array();
            if (!vacant)
            {
                for (const Widget *row : admitted.All(prefix + "TABLE_SMALLOTHER"))
                    if (!row->CellText(1).empty())
                        facts.push_back(Json{{"k", row->CellText(1)}, {"v", row->CellText(2)}});
                // The ship skills (the third pair of rows: names, then values) decide a captain.
                const std::vector<const Widget *> skills = admitted.All(prefix + "TABLE_SMALLSKILL");
                if (skills.size() >= 6)
                    for (int column = 0; column < 7; ++column)
                        if (!skills[4]->CellText(column).empty())
                            facts.push_back(Json{{"k", skills[4]->CellText(column)}, {"v", skills[5]->CellText(column)}});
            }
            Json actions = Json::array();
            const Widget *accept = rpg ? nullptr : Typed(admitted, "ACCEPT_ADD_OFFICER", "button");
            if (accept)
                actions.push_back(Json{{"id", "sheet-ok"}, {"label", accept->label}, {"variant", "primary"}, {"disabled", !offers(accept, "press")}, {"reason", accept->unavailable_reason}});
            view["sheet"] = Json{{"kind", mode},
                                 {"title", Trim(admitted.Label(prefix + "OFFICERS_WINDOW_CAPTION"))},
                                 {"closable", true},
                                 {"candidates", candidates},
                                 {"empty", !rpg && candidates.empty() ? bindings.Text("yard.no_captains") : std::string()},
                                 {"name", vacant ? std::string() : Trim(admitted.Label(prefix + "OFFICER_NAME"))},
                                 {"facts", facts},
                                 {"actions", actions},
                                 {"width", 440}};
            if (accept)
                bindings.On("sheet-ok", "", [send, accept](const Event &) { send(accept, "press"); });
        }
        else if (mode == "cannons")
        {
            close = Typed(admitted, "CANNONS_CANCEL", "button");
            const auto side = [&](const char *key, const char *node) {
                return Json::array({Json{{"k", bindings.Text(key)}, {"v", Trim(admitted.Label(node))}}});
            };
            view["sheet"] = Json{{"kind", "cannons"},
                                 {"title", Trim(admitted.Label("CANNONS_CAPTION"))},
                                 {"subtitle", Trim(admitted.Label("CANNONS_TEXT"))},
                                 {"closable", true},
                                 {"bow", side(StringKey("yard.bow"), "CANNONS_QTY_F")},
                                 {"stern", side(StringKey("yard.stern"), "CANNONS_QTY_B")},
                                 {"port", side(StringKey("yard.port"), "CANNONS_QTY_L")},
                                 {"starboard", side(StringKey("yard.starboard"), "CANNONS_QTY_R")},
                                 {"actions", Json::array()},
                                 {"width", 400}};
        }
        else if (mode == "help")
        {
            close = Control(admitted, "ANDROID_SHIPYARD_CLOSE", -1);
            close_verb = "close";
            Json lines = Json::array();
            for (int i = 1; i <= 4; ++i)
                if (const std::string text = Trim(Lines(admitted.Label("TOOLTIP_TEXT" + std::to_string(i)))); !text.empty())
                    lines.push_back(text);
            const std::string heading = Trim(admitted.Label("TOOLTIP_TITILE"));
            view["sheet"] = Json{{"kind", "help"},
                                 {"title", heading.empty() ? bindings.Text("common.description") : heading},
                                 {"tone", "parchment"},
                                 {"closable", true},
                                 {"lines", lines},
                                 {"actions", Json::array()},
                                 {"width", 560}};
        }
        if (!view["sheet"].is_null())
        {
            const auto dismiss = [send, close, close_verb](const Event &) { send(close, close_verb); };
            bindings.On("sheet-close", "", dismiss);
            bindings.On("sheet-cancel", "", dismiss);
            bindings.On("sheet", "dismiss", dismiss);
        }

        last_mode_ = mode;
        // ← and Android back close an open window first, then the shipyard.
        const Widget *exit = Control(admitted, "EXIT_BTN", -1);
        bindings.On("header-back", "", [send, close, close_verb, exit, main](const Event &) {
            if (!main)
                send(close, close_verb);
            else
                send(exit, "press");
        });
        return view;
    }

  private:
    struct Repair
    {
        int base = -1;       // the percentage when the window opened (nothing ordered)
        int target = -1;     // what the player asked for; -1 = nothing to do
        int last_value = -1; // the percentage when the last step was sent
        uint64_t stepped = 0; // the model revision that step was sent in
    };
    static const Widget *Typed(const Admission &admitted, const std::string &node, const char *type)
    {
        for (const Widget *widget : admitted.All(node))
            if (widget->type == type)
                return widget;
        return nullptr;
    }
    static const Widget *Control(const Admission &admitted, const std::string &node, int index)
    {
        for (const Widget *widget : admitted.All(node))
            if (widget->type == "shipyard_control" && widget->index == index)
                return widget;
        return nullptr;
    }

    uint64_t sent_revision_ = UINT64_MAX; // a token was sent for this model revision
    static constexpr double kSettling = 2.0;
    int logged_version_ = 1;
    double not_ready_since_ = -1.0;
    bool reported_ = false;
    std::string last_mode_;
    Repair hull_, sails_;
};

} // namespace

std::unique_ptr<Screen> CreateShipyardScreen()
{
    return std::make_unique<ShipyardScreen>();
}

} // namespace storm::ui::screens
