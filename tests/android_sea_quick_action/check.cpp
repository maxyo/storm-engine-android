#include "platform/android_sea_quick_action.hpp"
#include <cassert>
#include <iostream>
using namespace storm::android;

static SeaQuickActionFrame frame(std::string action = "Map", std::string target = "sea:42")
{
    return {19, true, true, true, "main:17", std::move(action), "Native caption", std::move(target)};
}
static std::string request(const SeaQuickActionState &state, const std::string &verb, const std::string &value)
{
    return "1\t" + std::to_string(state.activity()) + "\t" + std::to_string(state.revision()) + "\t" + verb + "\t" + value;
}

int main()
{
    SeaQuickActionState state;
    state.update(frame());
    assert(state.valid() && !state.covered(19, 10));
    assert(state.json().find("\"available\":true,\"action\":\"Map\",\"label\":\"Native caption\"") != std::string::npos);

    // nothing is activated, and nothing native is hidden, before the control is drawn
    assert(!state.offer(request(state, "activate", "Map"), 10));
    assert(state.offer(request(state, "ack", "1"), 20));
    assert(state.covered(19, 20) && !state.covered(20, 20));
    assert(!state.covered(19, 20 + SeaQuickActionState::coverMs + 1)); // the lease is not renewed by itself
    assert(state.offer(request(state, "ack", "1"), 700) && state.covered(19, 1400));

    // exactly the offered action, once
    assert(!state.offer(request(state, "activate", "Moor"), 701));
    const auto tap = request(state, "activate", "Map");
    const auto revision = state.revision();
    state.update(frame()); // the same frame sampled again keeps the revision
    assert(state.revision() == revision);
    assert(state.offer(tap, 702) && !state.offer(tap, 703));
    assert(state.consume(704) == "Map" && !state.consume(705));
    assert(!state.offer(tap, 706) && !state.covered(19, 706)); // the tap's echo is stale; the control must be drawn again
    assert(state.offer(request(state, "ack", "1"), 707) && state.offer(request(state, "activate", "Map"), 708));

    // another target under the same name: the queued tap must not fire, the native icon returns
    state.update(frame("Map", "another target"));
    assert(!state.consume(709) && !state.covered(19, 709));
    assert(!state.offer(tap, 710));

    // a tap that waited too long, and a control that went away
    assert(state.offer(request(state, "ack", "1"), 800) && state.offer(request(state, "activate", "Map"), 801));
    assert(!state.consume(801 + SeaQuickActionState::activateMs + 1));
    assert(state.offer(request(state, "ack", "1"), 3000) && state.offer(request(state, "activate", "Map"), 3001));
    assert(state.offer(request(state, "ack", "0"), 3002) && !state.covered(19, 3002) && !state.consume(3003));

    // a new Activity owns nothing of the old one
    assert(state.offer(request(state, "ack", "1"), 4000));
    const auto old = request(state, "ack", "1");
    state.resetActivity();
    assert(!state.covered(19, 4001) && !state.offer(old, 4001));

    for (const auto *id : {"Map", "Moor", "Board", "LandTroops", "Reload"})
    {
        SeaQuickActionState one;
        one.update(frame(id));
        assert(one.offer(request(one, "ack", "1"), 1) && one.offer(request(one, "activate", id), 2) && one.consume(3) == id);
    }
    for (const auto *id : {"Nothing", "Closed", "EnterToSea", "Unknown", ""})
    {
        SeaQuickActionState one;
        one.update(frame(id));
        assert(!one.valid() && !one.offer(request(one, "ack", "1"), 1) && !one.covered(19, 1));
        assert(one.json().find("\"available\":false,\"action\":\"\"") != std::string::npos);
    }
    {
        auto incomplete = frame();
        incomplete.complete = false;
        SeaQuickActionState one;
        one.update(incomplete);
        assert(!one.valid() && !one.offer(request(one, "ack", "1"), 1));
    }
    for (const auto *bad : {"", "1", "1\t1\t1\tack", "2\t1\t1\tack\t1", "1\t1\t1\tack\t2", "1\t1\t1\tack\t1\t1", "1\tx\t1\tack\t1"})
        assert(!state.offer(bad, 5000));
    SeaQuickActionState wrap; // SDL ticks wrap around
    wrap.update(frame());
    assert(wrap.offer(request(wrap, "ack", "1"), UINT32_MAX - 300) && wrap.covered(19, 100) && !wrap.covered(19, 1300));
    std::cout << "sea quick action: coverage lease, exact single activation, stale target and tap, activity, unknown actions PASS\n";
}
