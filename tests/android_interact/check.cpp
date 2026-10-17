#include <platform/android_combat_input.hpp>
#include <cassert>
#include <iostream>

struct USER_CONTROL
{
    const char *name = nullptr;
    bool bLocked = false, bSystemLocked = false;
    int control_type = 0, system_code = 0;
};
constexpr int UCT_ControlTree = 1, UNASSIGNED_CONTROL = -1;
struct Controls
{
    std::vector<USER_CONTROL> entries{{"ChrAction"}};
    int GetControlsNum() const { return entries.size(); }
    bool GetControlDesc(int index, USER_CONTROL &out) const { out = entries.at(index); return true; }
} controls;
struct Core
{
    struct Controls *Controls = &controls;
    unsigned player = 7;
    unsigned GetEntityId(const char *) const { return player; }
} core;

namespace storm::android
{
bool current = true;
bool combatContextCurrent(const GameplayState &, bool ready) { return current && ready; }
#include "adapter.inc"
}

int main()
{
    using namespace storm::android;
    assert(interactKind(true, "0") == "dialog");
    assert(interactKind(true, "123") == "dialog");
    for (const auto *value : {"", "-1", "1x", "1.5", "2147483648", "4294967295", "99999999999999999"})
        assert(interactKind(true, value) == "interact");
    assert(interactKind(false, "123") == "interact");
    GameplayState gameplay;
    GameplaySnapshot state;
    state.mode = "explore";
    state.group = "PrimaryLand";
    state.scene = "tavern:7";
    state.capabilities = {"ChrAction"};
    gameplay.update(state);
    combatInput.guard = [] { return current; };
    const auto refresh = [&] { updateCombatState(gameplay); };
    const auto packet = [&](int sequence, const char *phase = "tap") {
        return "1\t" + std::to_string(combatInput.revision()) + "\t" +
            std::to_string(combatInput.generation()) + "\t" + std::to_string(sequence) + "\tinteract\t" + phase;
    };
    refresh();
    assert(combatInput.json().find("\"interact\"") != std::string::npos);
    assert(!combatInput.offer(packet(1, "down"), 10));
    assert(combatInput.offer(packet(1), 10));
    assert(combatInput.merge("ChrAction", 0) == 0); // Not a wall-clock key pulse.
    combatInput.advance(11);
    assert(combatInput.merge("ChrAction", 0) == 3); // Core::ProcessControls observes the same edge.
    refresh();
    assert(combatInput.merge("ChrAction", 0) == 3); // Next Player::Update still observes it.
    assert(combatInput.merge("ChrAction", 1) == 1); // Physical hold retains priority.
    combatInput.advance(12);
    assert(combatInput.merge("ChrAction", 0) == 2);
    combatInput.advance(13);
    assert(combatInput.merge("ChrAction", 0) == 0);

    // A hidden quick-action icon is irrelevant: binding needs only the native
    // configured control. Existing Player/dialog scripts decide the NPC target.
    auto stale = packet(2);
    state.mode = "melee";
    gameplay.update(state); refresh();
    assert(combatInput.json().find("\"interact\"") == std::string::npos);
    assert(!combatInput.offer(stale, 20) && !combatInput.offer(packet(1), 20));
    state.mode = "explore";
    gameplay.update(state); refresh();
    for (auto *locked : {&controls.entries[0].bLocked, &controls.entries[0].bSystemLocked})
    {
        *locked = true; refresh();
        assert(!combatInput.offer(packet(1), 21));
        *locked = false; refresh();
    }
    controls.entries[0].system_code = UNASSIGNED_CONTROL; refresh();
    assert(!combatInput.offer(packet(1), 21));
    controls.entries[0].system_code = 0; refresh();
    state.capabilities.clear(); gameplay.update(state); refresh();
    assert(!combatInput.offer(packet(1), 21));
    state.capabilities = {"ChrAction"}; gameplay.update(state); refresh();

    assert(combatInput.offer(packet(1), 22));
    current = false; // Dialogue/menu/cutscene begins before the next native frame.
    combatInput.advance(23);
    assert(combatInput.merge("ChrAction", 0) != 3);
    refresh();
    assert(!combatInput.offer(packet(2), 24));
    current = true; refresh();
    assert(combatInput.offer(packet(1), 25));
    cancelAndroidCombatInput(); // Background or modal cancels the queued tap.
    combatInput.advance(26);
    assert(combatInput.merge("ChrAction", 0) != 3);
    stale = packet(2);
    core.player = 8; refresh();
    assert(!combatInput.offer(stale, 27));
    std::cout << "PASS production explore interact binding, native frame edge, physical priority and mode/control/lifecycle guards\n";
}
