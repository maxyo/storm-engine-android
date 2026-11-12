#include "platform/android_shortcuts.hpp"
#include <cassert>
#include <cstring>
#include <limits>

int main()
{
    using namespace storm::android;
    GameplayShortcuts queue;
    assert(!queue.offer("LaunchGameMenu", 1));
    assert(!queue.offer("pause", 1)); // Dialog pause belongs to SDL lifecycle, not a script event.
    assert(!queue.offer("mainmenu:injected", 1));
    assert(queue.offer("mainmenu", 100));
    assert(!queue.take(5000, false)); // Slow game / focus dialog: no timed key release.
    assert(queue.offer("mainmenu", 6000)); // Repeated tap coalesces.
    assert(!queue.offer("character", 6000)); // Conflicting commands cannot cascade menus.
    auto action = queue.take(10000, true);
    assert(action && std::strcmp(shortcutControl(*action), "MainMenu") == 0);
    assert(!queue.take(10001, true));
    assert(queue.offer("character", 10001));
    action = queue.take(10002, true);
    assert(action && std::strcmp(shortcutControl(*action), "Interface") == 0);
    assert(!queue.take(10002, true)); // Never retry after script refusal or menu open.
    assert(queue.offer("characterscreen", 10003));
    action = queue.take(10004, true);
    assert(action && !shortcutControl(*action) && std::strcmp(shortcutScreen(*action), "character") == 0);
    assert(queue.offer("shipscreen", 10005));
    action = queue.take(10006, true);
    assert(action && !shortcutControl(*action) && std::strcmp(shortcutScreen(*action), "ship") == 0);
    assert(queue.offer("journalscreen", 10007)); // an event line that leads to the journal
    action = queue.take(10008, true);
    assert(action && !shortcutControl(*action) && std::strcmp(shortcutScreen(*action), "journal") == 0);
    assert(queue.offer("itemsscreen", 10009)); // … to the inventory
    action = queue.take(10010, true);
    assert(action && !shortcutControl(*action) && std::strcmp(shortcutScreen(*action), "items") == 0);
    assert(!shortcutScreen(GameplayShortcut::MainMenu) && !shortcutScreen(GameplayShortcut::Character));

    queue.offer("character", 0);
    assert(!queue.take(GameplayShortcuts::lifetimeMs, true)); // No stale UI replay.
    queue.offer("mainmenu", 1);
    queue.clear(); // Activity background / native UI screen change.
    assert(!queue.take(100, true));
    const auto wrap = std::numeric_limits<uint32_t>::max();
    queue.offer("mainmenu", wrap - 100);
    assert(queue.take(100, true)); // Tick rollover is not expiry.
    queue.offer("character", wrap - 100);
    assert(!queue.take(GameplayShortcuts::lifetimeMs, true));
}
