#pragma once
// Calls `visit` for every element of a list that the visit itself may grow or shrink (Shadow::prepareAll:
// a shadow's prepare runs script events and its character's realize, which can create or delete shadows).
// Elements are taken by index from the live vector, never through an iterator or reference that a
// reallocation would leave dangling; elements added during the walk are visited too.
#include <cstddef>
#include <vector>

namespace storm::shadow
{
template <class T, class Visit> void forEachLive(std::vector<T *> &list, Visit visit)
{
    for (size_t i = 0; i < list.size(); ++i)
        visit(list[i]);
}
} // namespace storm::shadow
