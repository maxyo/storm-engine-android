#pragma once

#include <cstdint>

namespace storm
{
// A count of the changes to the script state the native caches of the application's interface
// adapters depend on (xinterface MobileCollectFacts, MobileNationFlags: values computed by
// scripts from attributes).
//
// Counted: every script segment loaded or unloaded (compiler.cpp), and every attribute write
// that changes something (a value, a name, the set of children; attributes.cpp) in an attribute
// tree an adapter read since the last ScriptStateForget (ScriptStateReads). Writes to other trees
// do not count: the game rewrites some every frame (weather.c Whr_OnWindChange moves Weather.Wind
// on every "frame" event, an open interface included) and no adapter reads them. Writes made while
// an adapter runs do not count either: they are its own output (GameInterface.AndroidFacts), never
// game state (the adapters change no game rules). Before the first ScriptStateReads every change
// counts. Attributes are read and written on the game thread only.
uint64_t ScriptStateGeneration() noexcept;
void ScriptStateChanged() noexcept;

// While one exists on the game thread (an adapter runs), every attribute read notes the tree
// (root object) it reads, and changes are not counted; when it ends, the trees it noted are added
// to the ones whose changes count.
class ScriptStateReads
{
  public:
    ScriptStateReads();
    ~ScriptStateReads();
    ScriptStateReads(const ScriptStateReads &) = delete;
    ScriptStateReads &operator=(const ScriptStateReads &) = delete;
};

// A new interface opened: the trees read so far stop counting until an adapter reads them again.
void ScriptStateForget();
} // namespace storm
