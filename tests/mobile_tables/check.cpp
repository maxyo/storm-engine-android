#include "mobile_inventory.hpp"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <map>
#include <deque>

class Codec final : public VSTRING_CODEC
{
    std::map<uint32_t, std::string> values;
public:
    uint32_t GetNum() override { return values.size(); }
    uint32_t Convert(const char *value) override { return Convert(value, std::char_traits<char>::length(value)); }
    uint32_t Convert(const char *value, int32_t length) override
    {
        const std::string text(value, length);
        for (const auto &entry : values) if (entry.second == text) return entry.first;
        const auto code = uint32_t(values.size() + 1);
        values[code] = text;
        return code;
    }
    const char *Convert(uint32_t code) override { return values.at(code).c_str(); }
    void VariableChanged() override {}
};

// Only engine service boundaries are stubbed; collection and ATTRIBUTES are real.
namespace storm::android {
bool imagePoll = true, imageReply = true;
std::string mobileScreen = "ship";
uint64_t mobileScreenRevision = 9;
struct Action {std::string screen, node, value; int index;};
std::deque<Action> uiWidgetActions;
}
namespace storm::mobile
{
ImageRef AtlasReference(void *, const char *, const char *) { return {}; }
ImageRef Reference(Descriptor) { return {}; }
}
struct Core
{
    ATTRIBUTES *table = nullptr, *root = nullptr;
    ATTRIBUTES *Entity_GetAttributeClass(int, const char *name)
    {
        if (root) return root->FindAClass(root, name);
        return std::string(name) == "TABLE_LIST" ? table : nullptr;
    }
} core;
constexpr int g_idInterface = 1, ACTION_ACTIVATE = 1, ACTION_MOUSEDBLCLICK = 2;
struct CXI_TABLE
{
    const char *m_nodeName = "TABLE_LIST";
    int m_nLineQuantity = 2, m_nColQuantity = 3, m_nSelectIndex = 0;
    struct { int top = 0, left = 0; } m_rect;
    bool m_bClickable = true, m_bLockStatus = false, m_bUse = true, m_bLockedNode = false;
    int activated = 0;
    void SetTopIndexForSelect(int) {}
    void SelectLine(int row) {m_nSelectIndex = row;}
    void DoAction(int, bool &, bool) {++activated;}
    bool MobileApply(int, const std::string &);
    uint32_t m_dwFontCellColor = 0xffeeeeee;
    void *pPictureService = nullptr;
    bool CheckCommandUsed(int) const { return true; }
    void MobileCollect(std::vector<MobileWidget> &) const;
};
#include "table_collect.inc"
#include "table_apply.inc"

int main()
{
    Codec codec;
    ATTRIBUTES data(codec);
    core.table = &data;
    for (int row = 1; row <= 2; ++row)
    {
        auto *line = data.CreateAttribute(("tr" + std::to_string(row)).c_str(), nullptr);
        line->CreateAttribute("td1", nullptr)->SetAttribute("str", "cargo");
        line->CreateAttribute("td3", nullptr)->SetAttribute("str", "20");
        line->CreateAttribute("UserData", nullptr)->SetAttribute("ID", ("cargo:" + std::to_string(row)).c_str());
    }
    CXI_TABLE table;
    auto collect = [&] {
        std::vector<MobileWidget> widgets;
        table.MobileCollect(widgets);
        assert(widgets.size() == 2);
        for (int row = 0; row < 2; ++row)
        {
            assert(widgets[row].index == row && widgets[row].type == "choice");
            assert(widgets[row].cells.size() == 3 && widgets[row].cells[1].text.empty());
            assert(widgets[row].cells[2].column == 2 && widgets[row].cells[2].text == "20");
            assert(widgets[row].rowId == "cargo:" + std::to_string(row + 1));
            assert(widgets[row].actions == std::vector<std::string>({"select", "activate"}));
        }
        std::cout << MobileWidgetJson("ship", widgets, 9, 1) << '\n';
        return widgets;
    };
    assert(collect()[0].columnHeaders.empty());
    auto *header = data.CreateAttribute("hr", nullptr);
    auto *first = header->CreateAttribute("td1", nullptr);
    first->SetAttribute("str", "Товар \"A\"\nшт.");
    auto *last = header->CreateAttribute("td3", nullptr);
    last->SetAttribute("str", "Вес");
    header->CreateAttribute("td4", nullptr)->SetAttribute("str", "outside table");
    auto rows = collect();
    assert(rows[0].columnHeaders.size() == 3 && rows[0].columnHeaders[1].empty());
    assert(rows[0].columnHeaders == rows[1].columnHeaders);
    first->SetAttribute("str", "");
    header->CreateAttribute("td2", nullptr); // A present cell without str is also empty.
    last->SetAttribute("str", "Новый вес");
    collect();
    data.DeleteAttributeClassX(header);
    collect();
    core.table = nullptr;
    std::vector<MobileWidget> absent;
    table.MobileCollect(absent);
    assert(absent.empty());

    // Real stock commerce structure: row.index + selector.current/picN.owner.
    // No synthetic UserData.ID is supplied by store/itemstrade/storage scripts.
    ATTRIBUTES commerce(codec);
    auto *goods = commerce.CreateAttribute("TABLE_LIST", nullptr);
    auto *item = goods->CreateAttribute("tr1", nullptr);
    item->SetAttribute("index", "42");
    item->CreateAttribute("td1", nullptr)->SetAttribute("str", "commodity");
    auto *ships = commerce.CreateAttribute("SHIPS_SCROLL", nullptr);
    ships->SetAttribute("current", "0");
    ships->CreateAttribute("pic1", nullptr)->SetAttribute("companionIndex", "7");
    auto *people = commerce.CreateAttribute("CHARACTERS_SCROLL", nullptr);
    people->SetAttribute("current", "0");
    people->CreateAttribute("pic1", nullptr)->SetAttribute("character", "8");
    core.root = &commerce;
    table.m_nLineQuantity = 1;
    for (const auto *screen : {"store", "storage", "contraband", "cannons", "itemstrade", "itemsbox"})
    {
        storm::android::mobileScreen = screen;
        const bool personal = std::string(screen) == "itemstrade" || std::string(screen) == "itemsbox";
        const auto id = storm::mobile::InterfaceTableRowId(screen, "TABLE_LIST", item, &commerce);
        assert(id == "commerce:" + std::string(screen) + ":item:42:owner:" + (personal ? "8" : "7") + ":role:0");
        std::vector<MobileWidget> live;
        table.MobileCollect(live);
        assert(live.size() == 1 && live[0].rowId == id && live[0].enabled);
        assert(!table.MobileApply(0, "select")); // no unversioned fallback for managed trade
        assert(!table.MobileApply(0, "activate@9:"));
        assert(!table.MobileApply(0, "select@8:" + id));
        assert(table.MobileApply(0, "select@9:" + id));
        assert(table.MobileApply(0, "activate@9:" + id));
        assert(storm::android::uiWidgetActions.front().value == "dispatch@9:" + id);
        item->SetAttribute("index", "43");
        assert(!table.MobileApply(0, "dispatch@9:" + id)); // recycled row after callback
        item->SetAttribute("index", "42");
        auto *selector = personal ? people : ships;
        auto *owner = selector->GetAttributeClass("pic1");
        const auto field = personal ? "character" : "companionIndex";
        owner->SetAttribute(field, "90");
        assert(!table.MobileApply(0, "select@9:" + id)); // same item, different owner
        owner->SetAttribute(field, personal ? "8" : "7");
        selector->SetAttribute("current", "1");
        assert(!table.MobileApply(0, "select@9:" + id)); // missing selector entry: fail closed
        live.clear();table.MobileCollect(live);assert(!live[0].enabled && live[0].actions.empty());
        selector->SetAttribute("current", "0");
        table.m_bLockedNode = true;assert(!table.MobileApply(0, "select@9:" + id));table.m_bLockedNode = false;
        assert(table.MobileApply(0, "dispatch@9:" + id));
        storm::android::mobileScreenRevision = 10;
        assert(!table.MobileApply(0, "dispatch@9:" + id)); // different interface lifetime
        storm::android::mobileScreenRevision = 9;
    }
    goods->SetAttribute("select", "1");
    const auto quantityId=storm::mobile::InterfaceTableRowId("store", "TABLE_LIST", item, &commerce);
    const auto guarded="guard@9:"+quantityId+"|press";
    assert(storm::mobile::CommerceQuantityValue("store","QTY_OK_BUTTON",9,&commerce,guarded)=="press");
    assert(storm::mobile::CommerceQuantityValue("store","qty_edit",9,&commerce,"guard@9:"+quantityId+"|-12")=="-12");
    assert(!storm::mobile::CommerceQuantityValue("store","QTY_OK_BUTTON",10,&commerce,guarded));
    assert(!storm::mobile::CommerceQuantityValue("items","QTY_OK_BUTTON",9,&commerce,guarded));
    assert(!storm::mobile::CommerceQuantityValue("store","I_SHIP",9,&commerce,guarded));
    assert(!storm::mobile::CommerceQuantityValue("store","QTY_OK_BUTTON",9,&commerce,"press"));
    assert(!storm::mobile::CommerceQuantityValue("store","QTY_OK_BUTTON",9,&commerce,guarded+"|press"));
    item->SetAttribute("index","43");
    assert(!storm::mobile::CommerceQuantityValue("store","QTY_OK_BUTTON",9,&commerce,guarded));
    item->SetAttribute("index","42");
    ships->GetAttributeClass("pic1")->SetAttribute("companionIndex","99");
    assert(!storm::mobile::CommerceQuantityValue("store","QTY_OK_BUTTON",9,&commerce,guarded));
    ships->GetAttributeClass("pic1")->SetAttribute("companionIndex","7");
    goods->SetAttribute("select","0");
    assert(!storm::mobile::CommerceQuantityValue("store","QTY_OK_BUTTON",9,&commerce,guarded));
    for (const auto *bad : {"", "-1", "+1", "42x", "4294967296"})
    {
        item->SetAttribute("index", bad);
        assert(storm::mobile::InterfaceTableRowId("store", "TABLE_LIST", item, &commerce).empty());
        assert(!table.MobileApply(0, "activate@9:"));
    }
    ATTRIBUTES reference(codec);
    auto *cities=reference.CreateAttribute("TABLE_CITY",nullptr);
    auto *city=cities->CreateAttribute("tr1",nullptr);
    city->CreateAttribute("td1",nullptr)->SetAttribute("str","Actual city");
    auto *cityData=city->CreateAttribute("UserData",nullptr);cityData->SetAttribute("CityID","FortFrance");
    core.root=&reference;table.m_nodeName="TABLE_CITY";table.m_nLineQuantity=1;storm::android::mobileScreen="tradebook";
    const std::string cityId="tradebook:city:FortFrance";
    assert(storm::mobile::InterfaceTableRowId("tradebook","TABLE_CITY",city,&reference)==cityId);
    std::vector<MobileWidget> cityRows;table.MobileCollect(cityRows);assert(cityRows.size()==1&&cityRows[0].rowId==cityId);
    assert(table.MobileApply(0,"select@9:"+cityId));assert(!table.MobileApply(0,"select"));
    assert(!table.MobileApply(0,"select@8:"+cityId));
    cityData->SetAttribute("CityID","PortRoyal");assert(!table.MobileApply(0,"select@9:"+cityId));
    assert(table.MobileApply(0,"select@9:tradebook:city:PortRoyal"));
    for(const auto *bad:{"","wrong\ncity","wrong|city","wrong city"}){
        cityData->SetAttribute("CityID",bad);assert(!table.MobileApply(0,"select@9:"+cityId));
        cityRows.clear();table.MobileCollect(cityRows);assert(!cityRows[0].enabled&&cityRows[0].actions.empty());
    }
    cityData->SetAttribute("CityID",std::string(129,'x'));assert(storm::mobile::InterfaceTableRowId("tradebook","TABLE_CITY",city,&reference).empty());
    cityData->SetAttribute("ID","Food");assert(storm::mobile::InterfaceTableRowId("tradebook","TABLE_GOODS",city,&reference)=="Food");
    assert(!storm::mobile::LeasedTableIdentityRequired("other","TABLE_CITY"));

}
