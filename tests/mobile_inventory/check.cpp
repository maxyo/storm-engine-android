#include "mobile_inventory.hpp"
#include <cassert>
#include <iostream>
#include <map>

class Codec final : public VSTRING_CODEC
{
    std::map<uint32_t, std::string> values;
  public:
    uint32_t GetNum() override { return values.size(); }
    uint32_t Convert(const char *value) override { return Convert(value, std::char_traits<char>::length(value)); }
    uint32_t Convert(const char *value, int32_t length) override
    {
        std::string text(value, length);
        for (const auto &entry : values)
            if (entry.second == text) return entry.first;
        const auto code = uint32_t(values.size() + 1);
        values[code] = text;
        return code;
    }
    const char *Convert(uint32_t code) override { return values.at(code).c_str(); }
    void VariableChanged() override {}
};

int main()
{
    using namespace storm::mobile;
    Codec codec;
    ATTRIBUTES table(codec), characters(codec);
    auto *row = table.CreateAttribute("tr1", nullptr);
    row->SetAttribute("index", "42");
    table.SetAttribute("select", "1");
    characters.SetAttribute("current", "0");
    characters.CreateAttribute("pic1", nullptr)->SetAttribute("character", "7");
    characters.CreateAttribute("pic2", nullptr)->SetAttribute("character", "8");
    const auto original = MobileTableRowId("TABLE_ITEMS", row, &characters);
    assert(original == "inventory:42:character:7:role:0");
    assert(InventorySelectedRowId(&table, &characters) == original);
    assert(MobileTableRowId("TABLE_PERKS", row, &characters).empty());
    assert(MobileTableRowId("TABLE_LIST", row, &characters).empty());
    auto *data = row->CreateAttribute("UserData", nullptr);
    data->SetAttribute("ID", "existing:ship/perk-id");
    for (const auto *node : {"TABLE_ITEMS", "TABLE_LIST", "TABLE_PERKS"})
        assert(MobileTableRowId(node, row, &characters) == "existing:ship/perk-id");
    data->SetAttribute("ID", "");
    assert(MobileTableRowId("TABLE_ITEMS", row, &characters).empty());
    row->DeleteAttributeClassX(data);

    InventoryEquipCapability capability;
    const auto action = "press@11:" + original;
    auto publish = [&](bool enabled = true, bool selected = true) {
        std::vector<MobileWidget> widgets;
        MobileWidget choice;
        choice.node = "TABLE_ITEMS";
        choice.type = "choice";
        choice.value = selected ? "1" : "0";
        choice.rowId = MobileTableRowId("TABLE_ITEMS", row, &characters);
        widgets.push_back(choice);
        MobileWidget button;
        button.node = "EQUIP_BUTTON";
        button.type = "button";
        button.enabled = enabled;
        widgets.push_back(button);
        capability.published(11, widgets);
        return widgets.back();
    };
    // A predicted row token never authorizes equip before a real publication.
    assert(!capability.consume(action, 11, original));
    auto button = publish();
    assert(button.rowId == original && button.actions == std::vector<std::string>{"press"});
    assert(!capability.consume("press", 11, original));
    assert(!capability.consume("press@10:" + original, 11, original));
    assert(!capability.consume(action, 12, original));
    assert(!capability.consume(action, 11, original, false)); // table locked after publication
    assert(capability.consume(action, 11, InventorySelectedRowId(&table, &characters)));
    assert(!capability.consume(action, 11, original)); // duplicate queued equip

    publish();
    capability.invalidate(); // production dispatch invalidates on row selection
    assert(!capability.consume(action, 11, original)); // select+equip before acknowledgement
    publish();
    row->SetAttribute("index", "99"); // filter rebuild: row index reused for another item
    assert(!capability.consume(action, 11, InventorySelectedRowId(&table, &characters)));
    row->SetAttribute("index", "42");
    publish();
    characters.SetAttribute("current", "1"); // same item, different character
    assert(!capability.consume(action, 11, InventorySelectedRowId(&table, &characters)));
    characters.GetAttributeClass("pic2")->SetAttribute("character", "7"); // same character, different role
    assert(!capability.consume(action, 11, InventorySelectedRowId(&table, &characters)));
    characters.SetAttribute("current", "0");
    publish();
    table.SetAttribute("select", "0");
    assert(!capability.consume(action, 11, InventorySelectedRowId(&table, &characters)));
    table.SetAttribute("select", "2");
    assert(!capability.consume(action, 11, InventorySelectedRowId(&table, &characters)));
    table.SetAttribute("select", "1");
    assert(publish(false).actions.empty());
    assert(!capability.consume(action, 11, original));
    assert(publish(true, false).rowId.empty());
    assert(!capability.consume(action, 11, original));
    for (const auto *bad : {"", "-1", "+1", "42x", "4294967296"})
    {
        row->SetAttribute("index", bad);
        assert(MobileTableRowId("TABLE_ITEMS", row, &characters).empty());
    }
    row->SetAttribute("index", "42");
    characters.SetAttribute("current", "4294967295");
    assert(MobileTableRowId("TABLE_ITEMS", row, &characters).empty());
    assert(MobileTableRowId("TABLE_ITEMS", row, nullptr).empty());
    assert(InventorySelectedRowId(nullptr, &characters).empty());
    std::cout << "PASS inventory production identity and publication capabilities\n";
}
