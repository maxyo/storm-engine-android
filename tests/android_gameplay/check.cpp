#include "platform/android_gameplay.hpp"
#include <cassert>
#include <limits>
int main()
{
    using namespace storm::android;
    GameplayContext context;
    context.group = "PrimaryLand";
    context.land = true;
    assert(gameplayMode(context) == "unknown"); // missing native player fails closed
    context.nativeFight = false;
    assert(gameplayMode(context) == "explore");
    context.nativeFight = true;
    assert(gameplayMode(context) == "unknown"); // sword drawn after control group update
    context.group = "FightModeControls";
    assert(gameplayMode(context) == "melee");
    context.nativeFight = false;
    assert(gameplayMode(context) == "unknown"); // sword sheathed, stale fight group
    context.group = "Sailing3Pers";
    context.sea = true;
    assert(gameplayMode(context) == "sailing");
    context.naval = true;
    assert(gameplayMode(context) == "naval");
    context.blocked = true;
    assert(gameplayMode(context) == "unknown"); // ComState/video/worldmap overrides stale sailing group
    context.blocked = false;
    context.group = "PrimaryLand";
    assert(gameplayMode(context) == "unknown"); // entering sea before sailing group is activated
    GameplayState state;
    state.update({});
    assert(!state.offer("1\tShip_Fire", 0));
    GameplaySnapshot sea{"sailing", "russian", "Sailing3Pers", "1", {"Ship_Fire", "Sea_CameraSwitch", "hk_charge1", "Ship_SailUp"}};
    assert(state.update(sea));
    const auto first = state.revision();
    auto request = [&state](std::string control) { return std::to_string(state.revision()) + '\t' + control; };
    assert(!state.update(sea)); // identical frames do not invalidate held input
    assert(state.offer(request("Ship_Fire"), 0));
    assert(state.take(1) == "Ship_Fire");
    assert(!state.take(1)); // exactly once
    assert(!state.offer(request("Ship_SailUp"), 1)); // polled controls cannot be faked by script events
    assert(!state.offer(request("hk_charge2"), 1)); // unbound action
    assert(!state.offer("2x\tShip_Fire", 1));
    assert(!state.offer("-2\tShip_Fire", 1));
    assert(!state.offer("18446744073709551616\tShip_Fire", 1));
    for (int i = 0; i < 8; ++i) assert(state.offer(request("hk_charge1"), 10));
    assert(!state.offer(request("hk_charge1"), 10)); // bounded
    state.update({}); // pause/load/menu invalidates all pending actions
    assert(!state.take(11));
    state.update(sea);
    assert(state.revision() > first);
    assert(!state.offer(std::to_string(first) + "\tShip_Fire", 12));
    assert(state.offer(request("Ship_Fire"), 0));
    assert(!state.take(1501)); // stale touch
    assert(state.offer(request("Ship_Fire"), std::numeric_limits<uint32_t>::max()-10));
    assert(state.take(10) == "Ship_Fire"); // SDL tick rollover
    assert(state.offer(request("Ship_Fire"), 12));
    sea.scene = "2";
    assert(state.update(sea));
    assert(!state.take(13)); // same mode, new scene
    assert(state.offer(request("Ship_Fire"), 13));
    sea.capabilities = {"Sea_CameraSwitch"};
    state.update(sea);
    assert(!state.take(14));
    assert(state.json().find("\"schemaVersion\":1") != std::string::npos);
    GameplayState restarted;
    restarted.update(sea);
    assert(restarted.revision() > state.revision());
    assert(!restarted.offer(std::to_string(state.revision()) + "\tSea_CameraSwitch", 15));
    assert(gameplayJsonString("a\"\\\nb") == "\"a\\\"\\\\b\"");
}
