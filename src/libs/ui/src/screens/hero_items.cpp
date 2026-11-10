// Hero section «Предметы» (interface items; mockups V2-Hero-Items, V2-Extra-TreasureMap). Fills
// the pack screen <s-items>: the equipment slots on the left (a tap filters the list to what
// fits; a tap on a row of the filtered list puts the item on), the item list with the game's
// four tabs, the selected item's description on parchment with the game's own action
// (EQUIP_BUTTON: put on, take off, use), and the game's map window drawn as the map sheet.
//
// What the stock screen does not show by row (the slot an item goes to, worn or not, a quest
// item) and what each slot holds come from the interface adapter android_hero.c
// (ANDROID_ITEMS, ANDROID_SLOTS; ANDROID_POSTS for the officer switch).
#include "hero.h"

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
        view["subject"] = Subject(admitted, bindings, [this, &bindings](const Widget &row) {
            sent_revision_ = 0;
            filter_.clear();
            Step(bindings, &row, "select", row.row_id);
        });
        std::string weight = Trim(admitted.Label("Weight_TEXT"));
        if (const size_t colon = weight.find(':'); colon != std::string::npos)
            weight = Trim(weight.substr(colon + 1));
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
            if (item)
            {
                tile["label"] = item->label;
                tile["sub"] = bindings.Text(slot.label);
                ImageRef picture;
                picture.kind = "atlas";
                picture.group = Field(item, "group");
                picture.image = Field(item, "image");
                tile["image"] = PictureOf(&picture);
                tile["icon"] = slot.icon;
                if (!Field(item, "remain").empty())
                    tile["sub"] = Field(item, "remain") + " / " + Field(item, "time");
            }
            else
            {
                tile["label"] = bindings.Text(slot.label);
                tile["icon"] = slot.icon;
                tile["empty"] = true;
            }
            slots.push_back(std::move(tile));
            const std::string id = slot.id;
            bindings.On("slot-" + id, "", [this, &bindings, id](const Event &) {
                filter_ = filter_ == id ? std::string() : id;
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
            if (!filter_slot.empty() && Field(fact, "slot") != filter_slot)
                continue;
            if (row->value == "1")
                selected = row;
            Json name{{"text", Trim(row->CellText(0))}};
            name["image"] = PictureOf(row->cells.empty() || row->cells[0].images.empty() ? nullptr : &row->cells[0].images[0]);
            if (Field(fact, "worn") == "1")
                name["badge"] = "check";
            else if (Field(fact, "quest") == "1")
                name["badge"] = "seal";
            const int count = Number(row->CellText(2));
            rows.push_back(Json{{"cells", Json::array({name, count > 1 ? "\xC3\x97" + std::to_string(count) : std::string(), Trim(row->CellText(3))})},
                                {"selected", row->value == "1"}});
            shown.push_back(row);
        }
        view["columns"] = Json::array({Json{{"label", bindings.Text("items.item")}, {"w", "1fr"}},
                                       Json{{"label", ""}, {"w", "40px"}, {"align", "right"}},
                                       Json{{"label", bindings.Text("items.weight")}, {"w", "52px"}, {"align", "right"}}});
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

        // The selected item.
        view["detail"] = selected ? DetailView(bindings, *selected, facts.count(selected->index) ? facts[selected->index] : nullptr) : Json(nullptr);

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

    Json DetailView(Bindings &bindings, const Widget &row, const Widget *fact)
    {
        const bool worn = Field(fact, "worn") == "1";
        const bool quest = Field(fact, "quest") == "1";
        std::string sub = LowerText(bindings.Text(SlotLabel(Field(fact, "slot"))));
        if (worn)
            sub += (sub.empty() ? "" : " \xC2\xB7 ") + bindings.Text("items.worn");
        const Widget *info = a_->One("INFO_PIC");
        Json image = PictureOf(info ? info->FirstImage() : nullptr);
        if (image.is_null())
            image = PictureOf(row.cells.empty() || row.cells[0].images.empty() ? nullptr : &row.cells[0].images[0]);
        Json detail{{"title", Trim(row.CellText(0))}, {"sub", sub}, {"text", Trim(a_->Label("INFO_TEXT"))}, {"image", image}};
        const Widget *equip = a_->One("EQUIP_BUTTON");
        // A quest item has no action (the game offers none either).
        if (equip && !quest && !Trim(equip->label).empty())
        {
            const bool ready = equip->Offers("press") && equip->row_id == row.row_id;
            detail["action"] = Json{{"id", "item-equip"},
                                    {"label", Trim(equip->label)},
                                    {"disabled", !ready},
                                    {"reason", equip->unavailable_reason},
                                    {"busy", bindings.Pending("item-equip")}};
            const Widget *target = equip;
            bindings.On("item-equip", "", [this, &bindings, target](const Event &) {
                sent_revision_ = 0;
                if (target->Offers("press") && !target->row_id.empty())
                    Step(bindings, target, "press", target->row_id);
            });
        }
        return detail;
    }
};

} // namespace

std::unique_ptr<Screen> CreateItemsScreen()
{
    return std::make_unique<ItemsScreen>();
}

} // namespace storm::ui::screens
