#include "mobile_officer_selection.hpp"
#include <cassert>
#include <iostream>
using namespace storm::mobile;
int main()
{
    OfficerSelection selection;
    OfficerListIdentity state{12, "QA_skip", {"1\nHero", "42\nNavigator Folke", "0\nDoctor"}};
    selection.publish(state);
    auto action = "select@12:" + selection.rowId(1);
    assert(selection.accepts(state, 1, action));
    assert(!selection.accepts(state, 0, action));
    assert(!selection.accepts(state, -1, action));
    assert(!selection.accepts(state, 3, action));
    assert(!selection.accepts(state, 1, "select"));
    auto changed = state; changed.revision++;
    assert(!selection.accepts(changed, 1, action));
    changed = state; changed.profile = "Other";
    assert(!selection.accepts(changed, 1, action));
    changed = state; changed.rows[1] = "43\nNavigator Other";
    assert(!selection.accepts(changed, 1, action));
    selection.publish(changed);
    assert(!selection.accepts(changed, 1, action));
    selection.publish(state); // List changed away and back: reject old event too.
    assert(!selection.accepts(state, 1, action));
    action = "select@12:" + selection.rowId(1);
    selection.publish(state);
    assert(selection.accepts(state, 1, action));
    changed = state; changed.profile.clear(); selection.publish(changed);
    assert(!selection.accepts(changed, 1, "select@12:" + selection.rowId(1)));
    std::cout << "PASS officer selection identity, epoch, profile, replacement and bounds\n";
}
