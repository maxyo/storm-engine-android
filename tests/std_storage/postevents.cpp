#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "s_postevents.h"

namespace
{
void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}

S_EVENTMSG *event(uint32_t number)
{
    auto payload = std::make_unique<MESSAGE>();
    payload->Reset("ls", static_cast<int32_t>(number), std::string(256, 'a' + number % 26));
    const auto name = "posted_" + std::to_string(number);
    return new S_EVENTMSG(name.c_str(), payload.release(), number + 1);
}

void check(S_EVENTMSG *value, uint32_t number)
{
    require(value != nullptr, "missing posted event");
    const auto name = "posted_" + std::to_string(number);
    require(std::strcmp(value->pEventName, name.c_str()) == 0, "posted event order/name");
    require(value->nPeriod == number + 1, "posted event period");
    value->pMessageClass->Move2Start();
    require(value->pMessageClass->Long() == static_cast<int32_t>(number), "posted message integer payload");
    require(value->pMessageClass->String() == std::string(256, 'a' + number % 26), "posted message string payload");
}

void growthAndDeletion()
{
    POSTEVENTS_LIST events;
    require(events.GetClassesNum() == 0 && !events.Read(0), "empty posted queue");
    events.Add(event(0));
    auto *borrowed = events.Read(0);
    constexpr uint32_t count = 4096;
    std::vector<S_EVENTMSG *> pointers{borrowed};
    // ProcessEvent may post more events while COMPILER holds a pointer from Read.
    for (uint32_t i = 1; i < count; ++i)
    {
        events.Add(event(i));
        pointers.push_back(events.Read(i));
    }
    require(events.Read(0) == borrowed, "borrowed event survives queue growth");
    require(events.GetClassesNum() == count, "posted event count after growth");
    for (uint32_t i = 0; i < count; ++i)
        check(events.Read(i), i);
    events.Del(count);
    events.Del(UINT32_MAX);
    require(!events.Read(UINT32_MAX) && !events.Read(count), "out-of-bounds read/delete");
    events.Del(2048);
    require(events.Read(2047) == pointers[2047] && events.Read(2048) == pointers[2049], "middle deletion preserves object addresses");
    check(events.Read(2048), 2049);
    events.Del(0);
    require(events.Read(0) == pointers[1], "head deletion preserves order/address");
    events.Del(events.GetClassesNum() - 1);
    check(events.Read(events.GetClassesNum() - 1), count - 2);
    events.Release();
    events.Release();
    require(events.GetClassesNum() == 0 && !events.Read(0), "release resets queue");
    events.Add(event(7));
    check(events.Read(0), 7);
    // Scope exit must release the new event and its actual MESSAGE payload.
}

void invalidation()
{
    POSTEVENTS_LIST events;
    for (uint32_t i = 0; i < 4; ++i)
        events.Add(event(i));
    auto *survivor = events.Read(2);
    events.Read(0)->Invalidate();
    events.Read(3)->Invalidate();
    events.RemoveInvalidated();
    require(events.GetClassesNum() == 2, "remove nonadjacent invalidated events");
    check(events.Read(0), 1);
    require(events.Read(1) == survivor, "survivor address after compaction");
    events.InvalidateAll();
    require(events.Read(0)->bInvalide && events.Read(1)->bInvalide, "invalidate all");
    events.RemoveInvalidated();
    // Preserve legacy scheduling: resetting n=0 after erase skips the new index
    // zero until the next pass. Storage migration must not silently fix it.
    require(events.GetClassesNum() == 1 && events.Read(0) == survivor, "legacy adjacent invalidation pass");
    events.RemoveInvalidated();
    require(events.GetClassesNum() == 0, "second invalidation pass");
    events.RemoveInvalidated();
    events.InvalidateAll();
}
}

int main()
{
    growthAndDeletion();
    invalidation();
    std::cout << "PASS: real posted-event queue ownership, payloads, growth, pointers, order and invalidation\n";
}
