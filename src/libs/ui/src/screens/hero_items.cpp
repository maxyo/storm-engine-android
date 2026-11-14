// Hero section «Предметы» (interface items; mockups V2-Hero-Items, V2-Extra-TreasureMap). Fills
// the pack screen <s-items>: the equipment slots on the left (a tap filters the list to what
// fits; a tap on a row of the filtered list puts the item on), the item list with the game's
// four tabs, the selected item's description on parchment with the game's own action
// (EQUIP_BUTTON: put on, take off, use, open a map, load ammunition; taking off a timed artefact
// destroys it and is held to confirm), and the game's map window drawn as the map sheet.
//
// What the stock screen does not show by row (the slot an item goes to, worn or not, a quest
// item, what the button does with it, an artefact's days) and what each slot holds come from
// the interface adapter android_hero.c
// (ANDROID_ITEMS, ANDROID_SLOTS; ANDROID_POSTS for the officer switch).
#include "hero.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace storm::ui::screens
{

using namespace hero;

namespace
{

constexpr NodeRule kNodes[] = {
    {"TABBTN_1", "image", true},
    {"ANDROID_POSTS", "fact"}, // the adapter's facts: CheckAdapter reports them missing
    {"ANDROID_ITEMS", "fact"},
    {"ANDROID_SLOTS", "fact"},
    {"TITLE", "heading"},
    {"TABLE_ITEMS", "choice"},
    {"EQUIP_BUTTON", "button"},
    {"TABBTN_*", "image"},
    {"TABSTR_*", "text"},
    {"CHARACTERS_SCROLL", "choice"},
    {"UP_BUTTON", "button"},
    {"DOWN_BUTTON", "button"},
    {"SETUP_FRAME_CAPTION", "text"}, // "Экипировка: <name>": the officer switch names the character
    {"SETUP_BIG_PICTURE", "image"},
    {"ANDROID_INFO", "fact"},        // the selected item's description in parts (the adapter)
    {"ITEM_*", "image"},             // what each slot holds: ANDROID_SLOTS has it with names
    {"SLOT_ITEM_*", "image"},
    {"SLOT_PIC_*", "image"},
    {"SLOT*_TEXT", "text"},
    {"INFO_PIC", "image"},
    {"INFO_TEXT", "text"},
    {"Weight_PIC", "image"},
    {"Weight_TEXT", "text"},
    {"Money_PIC", "image"},
    {"Money_TEXT", "text"},
    {"MAIN_BACK", "image"},
    {"EXIT_BTN", "button"},
    {"I_CHARACTER", "button"},
    {"I_SHIP", "button"},
    {"I_QUESTBOOK", "button"},
    {"I_TRADEBOOK", "button"},
    {"I_NATIONS", "button"},
    {"STR_BUTTONS", "text"}, // captions of the section buttons: the header icons replace them
    {"MAP_*", "text|image|button"},
};

constexpr double kStepTimeout = 3.0;

// The equipment slots in the order of the mockup, with their placeholder icons.
struct Slot
{
    const char *id;       // ANDROID_SLOTS row / ANDROID_ITEMS slot
    const char *filter;   // the slot value of the items that fit it
    const char *label;    // string key
    const char *icon;
};
constexpr Slot kSlots[] = {
    {"blade", "blade", StringKey("items.slot_blade"), "sabre"},
    {"gun", "gun", StringKey("items.slot_gun"), "pistol"},
    {"musket", "musket", StringKey("items.slot_musket"), "musket"},
    {"spyglass", "spyglass", StringKey("items.slot_spyglass"), "spyglass"},
    {"cirass", "cirass", StringKey("items.slot_cirass"), "shield"},
    {"talisman", "talisman", StringKey("items.slot_talisman"), "c-ruby"},
    {"tool", "tool", StringKey("items.slot_tool"), "compass"},
    {"slot1", "amulet", StringKey("items.slot_amulet"), "c-amulet"},
    {"slot2", "amulet", StringKey("items.slot_amulet"), "c-amulet"},
    {"slot3", "amulet", StringKey("items.slot_amulet"), "c-amulet"},
};

const char *SlotLabel(const std::string &filter)
{
    for (const Slot &slot : kSlots)
        if (filter == slot.filter)
            return slot.label;
    return "";
}

class ItemsScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-items";
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
        CheckAdapter(admitted, bindings, "ANDROID_POSTS", checked_opening_);
        map_open_ = admitted.One("MAP_EXIT_BTN") || admitted.One("MAP_CAPTION") || admitted.One("MAP_PICTURE");
        Walk(bindings);

        Json view = Json::object();
        view["title"] = Trim(admitted.Label("TITLE"));
        view["subject"] = Subject(admitted, bindings, subjects_, [this, &bindings](const Widget &row) {
            sent_revision_ = 0;
            filter_.clear();
            Step(bindings, &row, "select", row.row_id);
        });
        std::string weight = Trim(admitted.Label("Weight_TEXT"));
        if (const size_t colon = weight.find(':'); colon != std::string::npos)
            weight = Trim(weight.substr(colon + 1));
        weight = Decimal(weight, admitted.model->language);
        view["stats"] = weight.empty() ? Json::array() : Json::array({Json{{"icon", "weight"}, {"value", weight}}});

        // Facts by row and slot.
        std::map<int, const Widget *> facts; // TABLE_ITEMS row index (tr<n>, n = index + 1)
        for (const Widget *fact : admitted.All("ANDROID_ITEMS"))
            facts[std::atoi(fact->row_id.c_str() + 2) - 1] = fact;
        std::map<std::string, const Widget *> worn;
        for (const Widget *fact : admitted.All("ANDROID_SLOTS"))
            worn[fact->row_id] = fact;

        // The slots.
        Json slots = Json::array();
        for (const Slot &slot : kSlots)
        {
            const Widget *item = worn.count(slot.id) ? worn[slot.id] : nullptr;
            Json tile{{"id", slot.id}, {"selected", filter_ == slot.id}};
            // A tile is the slot: its caption under what is worn there (the item's name is in the list
            // and on the parchment).
            tile["label"] = bindings.Text(slot.label);
            if (item)
            {
                tile["name"] = item->label;
                ImageRef picture;
                picture.kind = "atlas";
                picture.group = Field(item, "group");
                picture.image = Field(item, "image");
                tile["image"] = PictureOf(&picture);
                tile["icon"] = slot.icon;
                // A timed artefact: the days of force it has left (they run down while it is worn) in place of the
                // slot's caption (the three amulet slots are captioned alike; a second line does not fit the tile).
                if (!Field(item, "remain").empty())
                    tile["label"] = Fill(bindings.Text("items.days"), {Field(item, "remain")});
            }
            else
            {
                tile["icon"] = slot.icon;
                tile["empty"] = true;
            }
            slots.push_back(std::move(tile));
            const std::string id = slot.id;
            // A slot that holds an item selects that item's row: the parchment and its action are the slot's.
            const std::string held = item ? item->label : std::string();
            bindings.On("slot-" + id, "", [this, &bindings, id, held](const Event &) {
                filter_ = filter_ == id ? std::string() : id;
                if (!filter_.empty() && !held.empty())
                    if (const Widget *row = WornRow(held); row && row->value != "1")
                    {
                        sent_revision_ = 0;
                        Step(bindings, row, "select", row->row_id);
                    }
                bindings.Rebuild();
            });
        }
        view["slots"] = slots;
        std::string filter_slot;
        for (const Slot &slot : kSlots)
            if (filter_ == slot.id)
                filter_slot = slot.filter;

        // The game's tabs.
        Json tabs = Json::array();
        for (int n = 1; n <= 4; ++n)
        {
            const Widget *button = admitted.One("TABBTN_" + std::to_string(n));
            const std::string label = Trim(admitted.Label("TABSTR_" + std::to_string(n)));
            if (!button || label.empty())
                continue;
            tabs.push_back(Json{{"id", std::to_string(n)}, {"label", label}, {"selected", button->value == "1"}});
        }
        view["tabs"] = tabs;
        bindings.On("item-tabs", "select", [this, &bindings](const Event &event) {
            const Widget *button = a_->One("TABBTN_" + event.value);
            if (!button || button->value == "1")
                return;
            sent_revision_ = 0;
            Step(bindings, button, "press");
        });

        // The list.
        std::vector<const Widget *> shown;
        const Widget *selected = nullptr;
        Json rows = Json::array();
        for (const Widget *row : admitted.All("TABLE_ITEMS"))
        {
            const Widget *fact = facts.count(row->index) ? facts[row->index] : nullptr;
            if (row->value == "1")
                selected = row; // a slot filter below also guards the parchment and its action
            if (!filter_slot.empty() && Field(fact, "slot") != filter_slot)
                continue;
            // A worn amulet belongs to one particular slot, not to every slot of the category.
            // The other slots offer only items that can go on there, never another slot's delete action.
            if (filter_slot == "amulet" && Field(fact, "worn") == "1" &&
                (!worn.count(filter_) || worn[filter_]->label != Trim(row->CellText(0))))
                continue;
            Json name{{"text", Trim(row->CellText(0))}};
            name["image"] = PictureOf(row->cells.empty() || row->cells[0].images.empty() ? nullptr : &row->cells[0].images[0]);
            if (Field(fact, "worn") == "1")
                name["badge"] = "check";
            else if (Field(fact, "quest") == "1")
                name["badge"] = "seal";
            const int count = Number(row->CellText(2));
            rows.push_back(Json{{"cells", Json::array({name, count > 1 ? "\xC3\x97" + std::to_string(count) : std::string(), Decimal(Trim(row->CellText(3)), admitted.model->language)})},
                                {"selected", row->value == "1"}});
            shown.push_back(row);
        }
        view["rows"] = rows;
        view["empty"] = bindings.Text(filter_slot.empty() ? "items.none" : "items.none_fits");
        bindings.On("items", "row", [this, &bindings, shown](const Event &event) {
            const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
            if (index >= shown.size())
                return;
            const Widget *row = shown[index];
            Intent intent;
            intent.row = row->row_id;
            // In a slot's list a tap puts the item on (R19); otherwise it shows the item.
            intent.equip = !filter_.empty() && !Worn(*row);
            if (row->value == "1" && !intent.equip)
                return;
            Start(bindings, intent);
        });

        // The selected item. With a slot chosen the parchment speaks for that slot only: the item it holds or
        // one that can go on; a selection left from before (another slot's item, an item outside the list)
        // is not shown, nor its action.
        if (selected && !filter_.empty())
        {
            const bool listed = std::find(shown.begin(), shown.end(), selected) != shown.end();
            const Widget *fact = facts.count(selected->index) ? facts[selected->index] : nullptr;
            const bool held = worn.count(filter_) && worn[filter_]->label == Trim(selected->CellText(0));
            if (!listed || (Field(fact, "worn") == "1" && !held))
                selected = nullptr;
        }
        const Widget *selected_fact = selected && facts.count(selected->index) ? facts[selected->index] : nullptr;
        // What is worn in the selected item's slot: its figures are set against it. An amulet goes to any
        // of three slots and is compared with none.
        const std::string selected_slot = Field(selected_fact, "slot");
        const Widget *against = Field(selected_fact, "worn") != "1" && worn.count(selected_slot) ? worn[selected_slot] : nullptr;
        view["detail"] = selected ? DetailView(bindings, *selected, selected_fact, against) : Json(nullptr);

        // The game's map window.
        view["map"] = map_open_ ? Json{{"title", Trim(admitted.Label("MAP_CAPTION"))},
                                       {"text", Trim(admitted.Label("MAP_TEXT"))},
                                       {"image", PictureOf(admitted.One("MAP_PICTURE") ? admitted.One("MAP_PICTURE")->FirstImage() : nullptr)}}
                                : Json(nullptr);

        // Header: sections and ←.
        bindings.On("header", "section", [this, &bindings](const Event &event) {
            if (event.value == "items")
                return;
            sent_revision_ = 0;
            if (map_open_)
                return CloseMap(bindings);
            for (const Widget *widget : a_->known)
                if (widget->type == "button" && SectionOf(*widget) == event.value && widget->Offers("press"))
                    return (void)Step(bindings, widget, "press");
        });
        bindings.On("header-back", "", [this, &bindings](const Event &) {
            sent_revision_ = 0;
            if (map_open_)
                CloseMap(bindings);
            else
                bindings.Back(nullptr);
        });
        return view;
    }

    // Android back closes the map window first.
    bool OwnsBack() const override
    {
        return map_open_;
    }
    void Back(Bindings &bindings) override
    {
        sent_revision_ = 0;
        CloseMap(bindings);
    }

  private:
    struct Intent
    {
        std::string row; // TABLE_ITEMS row id; empty: none
        bool equip = false;
        double since = 0.0;
    };

    Admission admission_;
    const Admission *a_ = nullptr;
    uint64_t revision_ = 0;
    uint64_t sent_revision_ = 0;
    uint64_t checked_opening_ = 0;
    SubjectSwitch subjects_;
    bool map_open_ = false;
    std::string filter_; // the slot whose items the list shows
    Intent intent_;

    bool Step(Bindings &bindings, const Widget *widget, const std::string &verb, const std::string &value = {})
    {
        if (!widget || sent_revision_ == revision_)
            return false;
        sent_revision_ = revision_;
        bindings.Send(*widget, verb, value);
        return true;
    }

    // The worn row of the item a slot holds (by the name ANDROID_SLOTS gives it).
    const Widget *WornRow(const std::string &name) const
    {
        for (const Widget *row : a_->All("TABLE_ITEMS"))
            if (Trim(row->CellText(0)) == name && Worn(*row))
                return row;
        return nullptr;
    }

    bool Worn(const Widget &row) const
    {
        for (const Widget *fact : a_->All("ANDROID_ITEMS"))
            if (std::atoi(fact->row_id.c_str() + 2) - 1 == row.index)
                return Field(fact, "worn") == "1";
        return false;
    }

    void CloseMap(Bindings &bindings)
    {
        if (const Widget *close = a_->One("MAP_EXIT_BTN"); close && close->Offers("press"))
            Step(bindings, close, "press");
    }

    void Start(Bindings &bindings, Intent intent)
    {
        sent_revision_ = 0; // a player action: its first step may go at once
        intent.since = bindings.Now();
        intent_ = std::move(intent);
        Walk(bindings);
        bindings.RebuildAfter(kStepTimeout);
    }

    // Select the row, then (equip) press the game's EQUIP_BUTTON once it carries that row.
    void Walk(Bindings &bindings)
    {
        if (intent_.row.empty())
            return;
        if (bindings.Now() - intent_.since > kStepTimeout)
        {
            bindings.Error("items: the engine did not answer a step (" + intent_.row + ")");
            intent_ = {};
            return;
        }
        const Widget *row = nullptr;
        for (const Widget *r : a_->All("TABLE_ITEMS"))
            if (r->row_id == intent_.row)
                row = r;
        if (!row)
        {
            intent_ = {};
            return;
        }
        if (row->value != "1")
        {
            Step(bindings, row, "select", row->row_id);
            return;
        }
        const Widget *equip = a_->One("EQUIP_BUTTON");
        if (!intent_.equip)
        {
            intent_ = {};
            return;
        }
        if (equip && equip->Offers("press") && equip->row_id == row->row_id && Step(bindings, equip, "press", equip->row_id))
            intent_ = {};
    }

    // A weapon's figures from the adapter (android_hero.c AndroidItemStats), each with its difference
    // against the worn item of the slot: green where the item is better, red where worse; the balance
    // has no better side (it depends on the blade's kind), its difference stays plain.
    Json Stats(Bindings &bindings, const Widget *fact, const Widget *against)
    {
        struct Figure
        {
            const char *field;
            const char *label;
            int better; // +1 more is better, -1 less is better, 0 neither
            int digits;
        };
        static const Figure figures[] = {{"attack", StringKey("items.attack"), 1, 1},   {"balance", StringKey("items.balance"), 0, 1},
                                         {"dmgmin", StringKey("items.dmg_min"), 1, 0}, {"dmgmax", StringKey("items.dmg_max"), 1, 0},
                                         {"reload", StringKey("items.reload"), -1, 0}, {"accuracy", StringKey("items.accuracy"), 1, 0},
                                         {"weight", StringKey("items.weight"), -1, 1}};
        const std::string &language = a_->model->language;
        const auto text = [&language](double value, int digits) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
            return Decimal(buffer, language);
        };
        Json stats = Json::array();
        for (const Figure &figure : figures)
        {
            const std::string value = Field(fact, figure.field);
            if (value.empty())
                continue;
            const double number = std::atof(value.c_str());
            Json line{{"k", bindings.Text(figure.label)}, {"v", text(number, figure.digits)}};
            const std::string other = Field(against, figure.field);
            if (!other.empty())
            {
                const double difference = number - std::atof(other.c_str());
                const double shown = std::round(difference * (figure.digits ? 10.0 : 1.0)) / (figure.digits ? 10.0 : 1.0);
                if (shown != 0.0)
                {
                    line["d"] = (shown > 0 ? "+" : "\xE2\x88\x92") + text(std::abs(shown), figure.digits);
                    if (figure.better != 0)
                        line["dTone"] = (shown > 0) == (figure.better > 0) ? "ok" : "danger";
                }
            }
            stats.push_back(std::move(line));
        }
        return stats;
    }

    Json DetailView(Bindings &bindings, const Widget &row, const Widget *fact, const Widget *against)
    {
        const bool worn = Field(fact, "worn") == "1";
        // The slot the item is worn in; an item that is not worn anywhere (a ring, a gem) has none.
        const std::string slot = Field(fact, "slot");
        const char *slot_label = SlotLabel(slot);
        if (!slot.empty() && !*slot_label)
            bindings.Error("items: unknown slot '" + slot + "'");
        std::string sub = *slot_label ? LowerText(bindings.Text(slot_label)) : std::string();
        if (worn)
            sub += (sub.empty() ? "" : " \xC2\xB7 ") + bindings.Text("items.worn");
        const Widget *picture = a_->One("INFO_PIC");
        Json image = PictureOf(picture ? picture->FirstImage() : nullptr);
        if (image.is_null())
            image = PictureOf(row.cells.empty() || row.cells[0].images.empty() ? nullptr : &row.cells[0].images[0]);
        // The description in parts from the adapter (android_hero.c AndroidItemInfo): the weapon's figures
        // against what is worn, its kind, the weight, then the item's own text. The stock INFO_TEXT is one
        // wrapped text of all of them; it is shown as it is only for a row the adapter did not describe.
        const Widget *info = nullptr;
        for (const Widget *candidate : a_->All("ANDROID_INFO"))
            if (std::atoi(Field(candidate, "row").c_str() + 2) - 1 == row.index)
                info = candidate;
        const std::string &language = a_->model->language;
        Json facts = Stats(bindings, fact, against);
        std::string text;
        if (info)
        {
            if (!Field(info, "kind").empty())
                facts.push_back(Json{{"k", bindings.Text("items.kind")}, {"v", Field(info, "kind")}});
            if (Field(fact, "weight").empty() && !Field(info, "weight").empty())
            {
                char weight[32];
                std::snprintf(weight, sizeof(weight), "%.1f", std::atof(Field(info, "weight").c_str()));
                facts.push_back(Json{{"k", bindings.Text("items.weight")}, {"v", Decimal(weight, language)}});
            }
            text = Decimal(Trim(Field(info, "text")), language);
            if (!Field(info, "potion").empty())
                text += (text.empty() ? "" : "\n") + Field(info, "potion");
        }
        else
            for (const Widget *part : a_->All("INFO_TEXT"))
                if (const std::string paragraph = Trim(part->label); !paragraph.empty())
                    text += (text.empty() ? "" : "\n") + Decimal(paragraph, language);
        // A timed artefact's term: its days, and while it is worn the days it has left of them.
        if (const std::string days = Field(fact, "days"); !days.empty())
        {
            const std::string remain = Field(fact, "remain");
            facts.push_back(Json{{"k", bindings.Text("items.term")},
                                 {"v", remain.empty() ? Fill(bindings.Text("items.days"), {days}) : Fill(bindings.Text("items.days_left"), {remain, days})}});
        }
        Json detail{{"title", Trim(row.CellText(0))}, {"sub", sub}, {"facts", facts}, {"text", text}, {"image", image}};
        detail["action"] = Action(bindings, row, fact, worn);
        return detail;
    }

    // The game's EQUIP_BUTTON for the selected row, by what it does with the item (the adapter's `action`,
    // items.c EquipPress): put on / take off with the game's own words; use, open a map, load ammunition with
    // ours (the game writes «Взять» on all of them); nothing for an item the game has no use for (a jewel, a
    // potion drunk in the field). Taking off a timed artefact uses it up (RemoveCharacterArtefactEquip): the
    // game's «Удалить», held to confirm. Quest items keep their action: a letter is read, a quest map opened.
    Json Action(Bindings &bindings, const Widget &row, const Widget *fact, bool worn)
    {
        const Widget *equip = a_->One("EQUIP_BUTTON");
        if (!equip)
            return nullptr;
        // Without the adapter's row the game's button is shown as the game labels it.
        const std::string kind = fact ? Field(fact, "action") : std::string("equip");
        if (kind.empty())
            return nullptr;
        std::string label = Trim(equip->label);
        bool destroys = false;
        if (kind == "use")
            label = bindings.Text("items.use");
        else if (kind == "map")
            label = bindings.Text("items.open_map");
        else if (kind == "ammo")
            label = bindings.Text("items.load");
        else if (kind == "amulet" && worn)
        {
            destroys = !Field(fact, "days").empty();
            if (!destroys) // an artefact without a term stays in the bag (the game still writes «Удалить»)
                label = bindings.Text("items.take_off");
        }
        else if (kind != "equip" && kind != "amulet")
        {
            bindings.Error("items: unknown action '" + kind + "'");
            return nullptr;
        }
        if (label.empty())
            return nullptr;
        const bool ready = equip->Offers("press") && equip->row_id == row.row_id;
        // The stock screen gives no reason; the adapter knows why an artefact cannot go on.
        std::string reason = equip->unavailable_reason;
        if (reason.empty())
        {
            const std::string block = Field(fact, "block");
            reason = bindings.Text(block == "slots" ? "items.block_slots" : block == "kind" ? "items.block_kind" : "items.unavailable");
        }
        Json action{{"id", "item-equip"}, {"label", label}, {"variant", destroys ? "danger" : "primary"}, {"hold", destroys},
                    {"disabled", !ready}, {"reason", reason}, {"busy", bindings.Pending("item-equip")}};
        const Widget *target = equip;
        bindings.On("item-equip", "", [this, &bindings, target, ready](const Event &) {
            sent_revision_ = 0;
            if (ready && target->Offers("press") && !target->row_id.empty())
                Step(bindings, target, "press", target->row_id);
        });
        return action;
    }
};

} // namespace

std::unique_ptr<Screen> CreateItemsScreen()
{
    return std::make_unique<ItemsScreen>();
}

} // namespace storm::ui::screens
