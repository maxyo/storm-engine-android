# Deck walk contract checks

Run `python3 tests/android_deck_walk/run.py` on the host (ASan/UBSan).

First person on deck the touch HUD's walk stick sends one vector,
`deckwalk:<gameplayRevision>\t<x>\t<y>` (x to the right, y forward, -100..100, at most 100 long),
after its dead zone and response curve (`src/libs/ui/src/hud/sea.cpp`). The game has no analog
deck walk and no deck strafe (its `DeckCamera_Left` / `DeckCamera_Right` turn the view and are
unbound), so `DeckWalk` (`platform/android_deck_walk.hpp`) is read by the deck camera beside its
`DeckCamera_Forward` / `DeckCamera_Backward` keys; the way and the pace of a frame are
`src/libs/sea_cameras/src/deck_walk.hpp`, checked here too. A walk is taken in the current gameplay
revision and lasts until `0\t0`, a cancelled input or the guard failing (no deck walk bound,
the player cannot act). The walker goes in real time: the game's time acceleration does not
speed him up.
