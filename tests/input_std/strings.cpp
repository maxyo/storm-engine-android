#include "strings_list.h"
#include <array>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>

int main()
{
    static_assert(!std::is_copy_constructible_v<STRINGS_LIST>);
    STRINGS_LIST strings;
    assert(strings.GetString(0) == nullptr);
    assert(strings.GetStringCode(nullptr) == INVALID_ORDINAL_NUMBER);
    bool nullRejected = false;
    try { strings.AddString(nullptr); } catch (const std::runtime_error &) { nullRejected = true; }
    assert(nullRejected);
    assert(strings.MakeHashValue("abc") == 0x6783);
    assert(strings.MakeHashValue("ABC") == 0x6783);

    // Odd payload size guards the existing arbitrary-byte data contract.
    strings.SetStringDataSize(3);
    strings.AddString("First.C");
    char *first = strings.GetString(0);
    std::array<unsigned char, 3> data{255, 255, 255};
    assert(strings.GetStringData(0, data.data()));
    assert((data == std::array<unsigned char, 3>{0, 0, 0}));
    data = {0x00, 0x80, 0xff};
    assert(strings.SetStringData(0, data.data()));
    assert(!strings.AddUnicalString("FIRST.c"));
    assert(strings.GetStringCode("first.C") == 0);

    // Force several table growths; callers keep pointers into file names.
    for (int i = 0; i < 4096; ++i)
        strings.AddString(("script-" + std::to_string(i)).c_str());
    assert(strings.GetString(0) == first);
    assert(std::strcmp(first, "First.C") == 0);
    assert(strings.GetStringsCount() == 4097);
    char *survivor = strings.GetString(3000);
    strings.DeleteString(1);
    assert(strings.GetString(2999) == survivor);
    assert(std::strcmp(strings.GetString(1), "script-1") == 0);
    assert(strings.GetStringCode("SCRIPT-2999") == 2999);
    std::array<unsigned char, 3> roundtrip{};
    assert(strings.GetStringData(0, roundtrip.data()) && roundtrip == data);
    strings.DeleteString(0);
    assert(strings.GetString(2998) == survivor);
    assert(strings.GetStringCode("First.C") == INVALID_ORDINAL_NUMBER);
    auto count = strings.GetStringsCount();
    strings.DeleteString(count);
    assert(strings.GetStringsCount() == count);
    strings.DeleteString(count-1);
    assert(strings.GetStringsCount() == count-1);
    assert(!strings.GetStringData(count, roundtrip.data()));
    assert(!strings.SetStringData(0, nullptr));

    // Compiler offsets are opaque binary payloads; same-size changes preserve
    // entries, changing payload size invalidates them, as before.
    strings.SetStringDataSize(3);
    assert(strings.GetStringsCount() == count-1);
    strings.SetStringDataSize(sizeof(uint32_t));
    assert(strings.GetStringsCount() == 0);
    strings.AddString("jump");
    uint32_t offset = 0x89abcdef, restored = 0;
    strings.SetStringData(0, &offset);
    strings.GetStringData(0, &restored);
    assert(restored == offset);
    strings.Release();
    strings.Release();
    assert(strings.GetStringsCount() == 0);
    strings.AddString("new-segment");
    strings.GetStringData(0, &restored);
    assert(restored == 0);
    strings.DeleteString(0);
    strings.Release(); // The old empty-list release leaked its pointer table.
    strings.AddString("");
    assert(strings.GetStringCode("") == 0);
}
