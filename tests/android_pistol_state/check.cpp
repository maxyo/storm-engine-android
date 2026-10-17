#include "platform/android_pistol_state.hpp"
#include <cassert>
#include <limits>
using storm::android::pistolState;
int main() {
    using O = std::optional<double>;
    assert(pistolState(false, true, 8, 0).state == "no_weapon");
    assert(pistolState(true, false, 1, 0).state == "unknown");
    assert(pistolState(true, true, O{}, 1).state == "unknown");
    assert(pistolState(true, true, 1, O{}).state == "unknown");
    assert(pistolState(true, true, -1, 0).state == "unknown");
    assert(pistolState(true, true, std::numeric_limits<double>::quiet_NaN(), 0).state == "unknown");
    assert(pistolState(true, true, 1, std::numeric_limits<double>::infinity()).state == "unknown");
    assert(pistolState(true, true, 0, 1).state == "unloaded");
    assert(pistolState(true, true, .54, 1).progress == 10);
    assert(pistolState(true, true, .999999, 1).progress == 19);
    assert(pistolState(true, true, 1, 1).state == "loaded");
    assert(pistolState(true, true, 1.2, 1).progress == 20); // usable partial multi-shot reload
    assert(pistolState(true, true, 4, 0).state == "loaded");
}
