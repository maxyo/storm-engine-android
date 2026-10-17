#include "mobile_inventory.hpp"
#include <cassert>
#include <cstdio>
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
namespace storm::android { bool imagePoll = true, imageReply = true; }
namespace storm::mobile
{
ImageRef AtlasReference(void *, const char *, const char *) { return {}; }
ImageRef Reference(Descriptor) { return {}; }
}
struct Core
{
    ATTRIBUTES *table = nullptr;
    ATTRIBUTES *Entity_GetAttributeClass(int, const char *name)
    {
        return std::string(name) == "TABLE_LIST" ? table : nullptr;
    }
} core;
constexpr int g_idInterface = 1, ACTION_ACTIVATE = 1, ACTION_MOUSEDBLCLICK = 2;
struct CXI_TABLE
{
    const char *m_nodeName = "TABLE_LIST";
    int m_nLineQuantity = 2, m_nColQuantity = 3, m_nSelectIndex = 0;
    struct { int top = 0, left = 0; } m_rect;
    bool m_bClickable = true, m_bLockStatus = false;
    uint32_t m_dwFontCellColor = 0xffeeeeee;
    void *pPictureService = nullptr;
    bool CheckCommandUsed(int) const { return true; }
    void MobileCollect(std::vector<MobileWidget> &) const;
};
#include "table_collect.inc"

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
}
