#include <platform/android_combat_input.hpp>
#include <platform/android_dialog_target.hpp>
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

struct CorePrivate;
namespace storm::android
{
bool current = true, dialogAvailable = true, boardingArmed = false;
int64_t dialogIndex = 12;
int64_t dialogTarget() { return dialogAvailable ? dialogIndex : -1; }
bool boardingContinueArmed(CorePrivate *) { return boardingArmed; }
bool combatContextCurrent(const GameplayState &, bool ready) { return current && ready; }
#include "adapter.inc"
}

int main()
{
    using namespace storm::android;
    assert(validDialogTarget(0) && validDialogTarget(123));
    assert(!validDialogTarget(-1) && !validDialogTarget(2147483648LL));
    GameplayState gameplay;
    GameplaySnapshot state;
    state.mode = "explore";
    state.group = "PrimaryLand";
    state.scene = "tavern:7";
    state.capabilities = {"ChrAction"};
    gameplay.update(state);
    combatInput.guard = [] { return current; };
    const auto refresh = [&] { updateCombatState(gameplay, nullptr); };
    const auto packet = [&](int sequence, const char *phase = "tap") {
        return "2\t" + std::to_string(combatInput.generation()) + "\t" +
            std::to_string(combatInput.actionGeneration("interact")) + "\t" + std::to_string(sequence) + "\tinteract\t" + phase;
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

    // Walking away revokes both the visible capability and already queued taps.
    auto stale = packet(2);
    assert(combatInput.offer(stale, 14));
    const auto inFlight = packet(3);
    const auto activity = combatInput.generation();
    dialogAvailable = false; refresh();
    assert(combatInput.json().find("\"interact\"") == std::string::npos);
    assert(combatInput.generation() == activity); // The session survives; only interact is fenced.
    combatInput.advance(15);
    assert(combatInput.merge("ChrAction", 0) != 3);
    assert(!combatInput.offer(inFlight, 16));
    dialogAvailable = true; refresh();
    assert(!combatInput.offer(inFlight, 17)); // A new target is a new availability: taps aimed at the old one never fire.
    assert(combatInput.offer(packet(3), 18)); // A tap on the new target does.
    combatInput.advance(19);
    assert(combatInput.merge("ChrAction", 0) == 3);
    combatInput.advance(20); combatInput.advance(21);
    assert(combatInput.offer(packet(4), 21)); // Queued, then the target leaves and another walks in before the frame.
    dialogAvailable = false; refresh(); dialogAvailable = true; refresh();
    combatInput.advance(21);
    assert(combatInput.merge("ChrAction", 0) != 3);
    stale = packet(5);
    state.mode = "melee";
    gameplay.update(state); refresh();
    assert(combatInput.json().find("\"interact\"") == std::string::npos);
    assert(!combatInput.offer(stale, 22) && !combatInput.offer(packet(1), 22));
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

    // After a won boarding fight the script's LAi_ActivateReload waits for ChrAction: offered without
    // any dialog target, named "boarding", and gone (with queued taps) once the handler is disarmed.
    dialogAvailable = false; refresh();
    assert(combatInput.json().find("\"interact\"") == std::string::npos && combatInteractKind == "dialog");
    boardingArmed = true; refresh();
    assert(combatInput.json().find("\"interact\"") != std::string::npos && combatInteractKind == "boarding");
    assert(combatInput.offer(packet(1), 28));
    combatInput.advance(29);
    assert(combatInput.merge("ChrAction", 0) == 3);
    combatInput.advance(30); combatInput.advance(31);
    state.mode = "melee"; gameplay.update(state); refresh();
    assert(combatInput.json().find("\"interact\"") == std::string::npos); // fighting on: the action key is not offered
    state.mode = "explore"; gameplay.update(state); refresh();
    const auto queued = packet(2);
    assert(combatInput.offer(queued, 32));
    boardingArmed = false; refresh();
    assert(combatInput.json().find("\"interact\"") == std::string::npos && combatInteractKind == "dialog");
    combatInput.advance(33);
    assert(combatInput.merge("ChrAction", 0) != 3);
    dialogAvailable = true;
    // Another target under the button is another availability: a tap aimed at the old one is
    // refused (U0), the button stays.
    current = true; core.player = 7; refresh();
    const auto aimed = packet(30);
    dialogIndex = 13; refresh();
    assert(combatInput.json().find("\"interact\"") != std::string::npos && !combatInput.offer(aimed, 40));
    assert(combatInput.offer(packet(31), 40));

    // Hysteresis of the talk target (Character::FindDialogCharacter on Android): taken within
    // the enter reach, kept within the wider exit reach, so walking about the edge does not
    // flicker the button.
    const float yaw = 0.0f; // facing +z
    const auto at = [](uint64_t id, float x, float z) { return DialogCandidate{id, x, 0.0f, z, x * x + z * z, true}; };
    DialogCandidate near[] = {at(5, 0.0f, 3.0f)};
    assert(dialogTargetChoice(near, yaw, 0) == 5); // within 3.5 m, straight ahead
    DialogCandidate drifting[] = {at(5, 0.0f, 4.2f)};
    assert(dialogTargetChoice(drifting, yaw, 0) == 0 && dialogTargetChoice(drifting, yaw, 5) == 5); // 4.2 m: not taken, but kept
    DialogCandidate aside[] = {at(5, 1.6f, 3.0f)}; // about 28 degrees off: outside the 20 degree half-cone, inside 35
    assert(dialogTargetChoice(aside, yaw, 0) == 0 && dialogTargetChoice(aside, yaw, 5) == 5);
    DialogCandidate gone[] = {at(5, 0.0f, 4.8f)};
    assert(dialogTargetChoice(gone, yaw, 5) == 0); // beyond the exit reach: let go
    DialogCandidate crowd[] = {at(5, 0.0f, 4.0f), at(6, 0.0f, 2.0f)};
    assert(dialogTargetChoice(crowd, yaw, 5) == 5 && dialogTargetChoice(crowd, yaw, 0) == 6); // a closer passer-by does not steal a kept target
    DialogCandidate fighting[] = {DialogCandidate{5, 0.0f, 0.0f, 2.0f, 4.0f, false}};
    assert(dialogTargetChoice(fighting, yaw, 5) == 0);
    std::cout << "PASS production explore interact binding (dialog, boarding continuation), native frame edge, physical priority and mode/control/lifecycle guards, target change fence, talk target hysteresis\n";
}
