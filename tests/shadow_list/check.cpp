// Shadow::prepareAll walks Shadow::instances_ while a shadow's prepare may create (or delete) shadows.
// The walk must never read through storage the growth reallocated (ASan in this check) and must reach
// the shadows added on the way.
#include "live_list.hpp"

#include <cstdio>
#include <memory>
#include <vector>

struct Item
{
    int value;
};

int main()
{
    std::vector<std::unique_ptr<Item>> owned;
    std::vector<Item *> list;
    const auto add = [&](int value) {
        owned.push_back(std::make_unique<Item>(Item{value}));
        list.push_back(owned.back().get());
    };
    add(0);
    list.shrink_to_fit(); // the first push_back during the walk reallocates
    int visited = 0;
    storm::shadow::forEachLive(list, [&](Item *item) {
        ++visited;
        if (item->value < 64)
            add(item->value + 1); // a "shadow" created by the previous one's prepare
    });
    if (visited != 65 || list.size() != 65)
    {
        std::printf("FAIL: visited %d of %zu items\n", visited, list.size());
        return 1;
    }
    std::printf("PASS: every shadow visited while the list grew (%d)\n", visited);
    return 0;
}
