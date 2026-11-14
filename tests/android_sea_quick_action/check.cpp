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
    assert(state.valid() && !state.covered(19));
    assert(state.json().find("\"available\":true,\"action\":\"Map\",\"label\":\"Native caption\"") != std::string::npos);

    // nothing is activated, and nothing native is hidden, before the control is drawn
    assert(!state.offer(request(state, "activate", "Map"), 10));
    assert(state.offer(request(state, "ack", "1"), 20));
    assert(state.covered(19) && !state.covered(20));
    assert(state.offer(request(state, "ack", "1"), 700) && state.covered(19)); // no lease: the word lasts

    // exactly the offered action, once
    assert(!state.offer(request(state, "activate", "Moor"), 701));
    const auto tap = request(state, "activate", "Map");
    const auto revision = state.revision();
    state.update(frame()); // the same frame sampled again keeps the revision
    assert(state.revision() == revision);
    assert(state.offer(tap, 702) && !state.offer(tap, 703));
    assert(state.consume(704) == "Map" && !state.consume(705));
    assert(!state.offer(tap, 706) && !state.covered(19)); // the tap's echo is stale; the control must be drawn again
    assert(state.offer(request(state, "ack", "1"), 707) && state.offer(request(state, "activate", "Map"), 708));

    // another target under the same name: the queued tap must not fire, the native icon returns
    state.update(frame("Map", "another target"));
    assert(!state.consume(709) && !state.covered(19));
    assert(!state.offer(tap, 710));

    // a tap that waited too long, and a control that went away
    assert(state.offer(request(state, "ack", "1"), 800) && state.offer(request(state, "activate", "Map"), 801));
    assert(!state.consume(801 + SeaQuickActionState::activateMs + 1));
    assert(state.offer(request(state, "ack", "1"), 3000) && state.offer(request(state, "activate", "Map"), 3001));
    assert(state.offer(request(state, "ack", "0"), 3002) && !state.covered(19) && !state.consume(3003));

    // a new Activity owns nothing of the old one
    assert(state.offer(request(state, "ack", "1"), 4000));
    const auto old = request(state, "ack", "1");
    state.resetActivity();
    assert(!state.covered(19) && !state.offer(old, 4001));

    // One coverage rule: while the UI presents the sea HUD the native icon is off, whatever the UI
    // answered; a tap is accepted only for a control the UI said is drawn.
    hudPresenters.sea = true;
    state.update(frame("Moor"));
    assert(state.covered(19) && !state.covered(20));
    assert(state.offer(request(state, "ack", "0"), 4100) && state.covered(19)); // not drawn by the UI: still no native icon
    state.update(frame("Board"));
    assert(state.covered(19) && !state.offer(request(state, "activate", "Board"), 4101)); // not answered drawn: no tap
    assert(state.offer(request(state, "ack", "1"), 60000) && state.covered(19) && state.offer(request(state, "activate", "Board"), 60001));
    hudPresenters.sea = false;

    // The UI never answers (a hung or broken runtime): the native icon must not stay hidden
    // forever. The engine loop watches the unanswered revision and withdraws the presenter
    // loudly; answers that still come keep working.
    {
        SeaQuickActionState silent;
        assert(hudPresenters.declare("sea"));
        silent.update(frame("Moor"));
        assert(silent.awaiting() && silent.covered(19));
        // Frames of 10 ms: the watch counts frame time, so kSilentMs takes kSilentMs / 10 frames.
        uint32_t tick = 1000;
        for (uint32_t i = 0; i < HudPresenters::kSilentMs / 10; ++i, tick += 10)
            assert(!hudPresenters.watch("sea", silent.awaiting(), tick) && silent.covered(19));
        const auto line = hudPresenters.watch("sea", silent.awaiting(), tick);
        assert(line && line->find("HUD presenter sea withdrawn") != std::string::npos && line->find("legacy HUD is back") != std::string::npos);
        assert(!hudPresenters.sea && !silent.covered(19)); // native back
        assert(!hudPresenters.watch("sea", silent.awaiting(), 60000)); // said once
        assert(silent.offer(request(silent, "ack", "1"), 60001) && silent.covered(19)); // a late answer still covers
        // The UI answers again: the presenter comes back at the next watched frame, loudly.
        const auto back = hudPresenters.watch("sea", silent.awaiting(), 60002);
        assert(back && back->find("HUD presenter sea back") != std::string::npos && hudPresenters.sea);

        // Answers in time reset the watch; a UI runtime failure reported by the host withdraws at once.
        assert(hudPresenters.declare("sea"));
        silent.update(frame("Board"));
        tick = 100;
        for (uint32_t i = 0; i < HudPresenters::kSilentMs / 20; ++i, tick += 10) assert(!hudPresenters.watch("sea", true, tick));
        assert(!hudPresenters.watch("sea", false, tick)); // answered: the silence starts again
        for (uint32_t i = 0; i < HudPresenters::kSilentMs / 10; ++i, tick += 10) assert(!hudPresenters.watch("sea", true, tick));
        assert(hudPresenters.lost().find("UI runtime failed") != std::string::npos && !hudPresenters.sea && silent.awaiting() && !silent.covered(19));
        assert(hudPresenters.lost().empty()); // nothing left to lose: no second line
    }

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
        assert(!one.valid() && !one.offer(request(one, "ack", "1"), 1) && !one.covered(19));
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
    assert(wrap.offer(request(wrap, "ack", "1"), UINT32_MAX - 300) && wrap.offer(request(wrap, "activate", "Map"), 100) && wrap.consume(200) == "Map");
    std::cout << "sea quick action: coverage until answered otherwise, presenter pending, silent UI withdrawn loudly (native back), host-reported UI loss, exact single activation, stale target and tap, activity, unknown actions PASS\n";
}
