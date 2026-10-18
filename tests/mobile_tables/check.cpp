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
static uint64_t imageHandle(const std::string &key)
{
    static std::map<std::string, uint64_t> handles;
    const auto [entry, inserted] = handles.try_emplace(key, handles.size() + 1);
    return entry->second;
}
ImageRef AtlasReference(void *, const char *group, const char *image)
{
    if (!group || !*group || !image || !*image || std::string(image) == "missing") return {};
    ImageRef ref;
    ref.handle = imageHandle(std::string(group) + ":" + image);
    ref.imageRevision = 9;
    ref.source = {"atlas", group, image, {}, {}, -1};
    return ref;
}
ImageRef Reference(Descriptor descriptor)
{
    ImageRef ref;
    ref.handle = imageHandle(descriptor.path);
    ref.imageRevision = 9;
    return ref;
}
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
struct StringService
{
    std::map<std::string, std::string> translations;
    const char *GetString(const char *key) const
    {
        const auto value = translations.find(key);
        return value == translations.end() ? nullptr : value->second.c_str();
    }
};
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
    StringService *pStringService = nullptr;
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

    // Optional metadata uses actual sources, with no positional nation dictionary.
    ATTRIBUTES nationRoot(codec);
    auto *nation = nationRoot.CreateAttribute("TABLE_NATION", nullptr);
    auto *nationHeader = nation->CreateAttribute("hr", nullptr);
    for (int row = 1; row <= 2; ++row)
    {
        auto *line = nation->CreateAttribute(("tr" + std::to_string(row)).c_str(), nullptr);
        line->CreateAttribute("td1", nullptr)->SetAttribute("str", "value");
        auto *bodyIcon = line->CreateAttribute("td3", nullptr)->CreateAttribute("icon", nullptr);
        bodyIcon->SetAttribute("group", "NATIONS");bodyIcon->SetAttribute("image", "Zeta\"\\\n");
    }
    core.root = &nationRoot;table.m_nodeName = "TABLE_NATION";table.m_nColQuantity = 7;
    StringService strings;strings.translations["Zeta\"\\\n"] = "Реальная \"страна\"\n";
    strings.translations["Empty"] = "";table.pStringService = &strings;
    const auto nationRows = [&] {
        std::vector<MobileWidget> rows;table.MobileCollect(rows);assert(rows.size() == 2);
        assert(rows[0].headerCells.has_value());
        assert(rows[0].cells[2].images[0].source.image == "Zeta\"\\\n");
        assert(rows[0].actions == std::vector<std::string>({"select", "activate"}));
        std::cout << MobileWidgetJson("questbook", rows, 9, 1) << '\n';
        return rows;
    };
    assert(nationRows()[0].headerCells->empty()); // hr present, no td cells
    nationHeader->CreateAttribute("td1", nullptr)->SetAttribute("str", "Actual heading");
    const auto icon = [&](int column, const char *name, const char *image) {
        const auto key = "td" + std::to_string(column);
        auto *cell = nationHeader->GetAttributeClass(key.c_str());
        if (!cell) cell = nationHeader->CreateAttribute(key.c_str(), nullptr);
        auto *source = cell->CreateAttribute(name, nullptr);
        source->SetAttribute("group", "NATIONS");source->SetAttribute("image", image);
        return source;
    };
    auto *zeta = icon(2, "icon", "Zeta\"\\\n");
    zeta->GetParent()->SetAttributeUseDword("color", 0xff123456);
    auto *alpha = icon(3, "icon", "Alpha"); // missing translation retains image identity
    icon(4, "icon", "Zeta\"\\\n"); // duplicate identities are exported, never assigned another nation
    icon(5, "icon", "Zeta\"\\\n")->SetAttribute("texturepointer", "1");
    icon(6, "icon1", "Zeta\"\\\n");icon(6, "icon2", "Alpha"); // ambiguous caption
    icon(7, "icon", "Zeta\"\\\n")->GetParent()->SetAttribute("str", "Explicit caption");
    auto nationBefore = nationRows();
    const auto &headers = *nationBefore[0].headerCells;
    assert(headers.size() == 7 && headers[1].column == 1);
    assert(headers[1].text == strings.translations.at("Zeta\"\\\n"));
    assert(headers[1].images[0].handle == nationBefore[0].cells[2].images[0].handle);
    assert(headers[2].text.empty() && headers[4].images.empty() && headers[5].text.empty());
    assert(headers[6].text == "Explicit caption");
    zeta->SetAttribute("image", "Alpha");alpha->SetAttribute("image", "Zeta\"\\\n");
    auto reordered = nationRows();
    assert((*reordered[0].headerCells)[1].text.empty());
    assert((*reordered[0].headerCells)[2].text == strings.translations.at("Zeta\"\\\n"));
    nationHeader->DeleteAttributeClassX(nationHeader->GetAttributeClass("td4"));
    nationHeader->DeleteAttributeClassX(nationHeader->GetAttributeClass("td6"));
    alpha->SetAttribute("image", "missing");
    auto sparseNation = nationRows();
    assert(sparseNation[0].headerCells->size() == 5 && (*sparseNation[0].headerCells)[3].column == 4);
    assert((*sparseNation[0].headerCells)[2].images.empty());
    alpha->SetAttribute("image", "Empty");
    assert((*nationRows()[0].headerCells)[2].text.empty());
    alpha->SetAttribute("image", "Zeta\"\\\n");table.pStringService = nullptr;
    assert((*nationRows()[0].headerCells)[2].text.empty());
    // Texture icons use the existing safe path; malformed UV and pointer icons are rejected.
    auto *texture = nationHeader->GetAttributeClass("td5");
    texture->DeleteAttributeClassX(texture->GetAttributeClass("icon"));
    auto *textureIcon = texture->CreateAttribute("icon", nullptr);textureIcon->SetAttribute("texture", "actual.tga");
    textureIcon->SetAttribute("uv", "bad");
    assert((*nationRows()[0].headerCells)[3].images.empty());
    textureIcon->SetAttribute("uv", "0,0,1,1");
    assert((*nationRows()[0].headerCells)[3].images.size() == 1);
    // Header content never makes an empty body visible to old clients.
    for (int row = 1; row <= 2; ++row)
        nation->GetAttributeClass(("tr" + std::to_string(row)).c_str())->GetAttributeClass("td1")->SetAttribute("str", "");
    std::vector<MobileWidget> imageOnly;
    storm::android::imagePoll = false;table.MobileCollect(imageOnly);assert(imageOnly.empty());
    storm::android::imagePoll = true;storm::android::imageReply = false;
    table.MobileCollect(imageOnly);assert(imageOnly.empty());
    storm::android::imageReply = true;table.MobileCollect(imageOnly);assert(imageOnly.size() == 2);
    for (int row = 1; row <= 2; ++row)
        nation->GetAttributeClass(("tr" + std::to_string(row)).c_str())->GetAttributeClass("td3")
            ->GetAttributeClass("icon")->SetAttribute("texturepointer", "1");
    imageOnly.clear();table.MobileCollect(imageOnly);assert(imageOnly.empty());
    core.root = nullptr;table.m_nodeName = "TABLE_LIST";table.m_nColQuantity = 3;

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
