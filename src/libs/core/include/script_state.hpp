#pragma once

#include <cstdint>

namespace storm
{
// A count of the changes to the state scripts keep that native code reads: any attribute value,
// name or set of children (attributes.cpp: only writes that change something count) and any
// script segment loaded or unloaded (compiler.cpp). Equal counts mean no script state changed in
// between; native caches of values computed from script state (xinterface MobileCollectFacts)
// compare them instead of recomputing every frame.
uint64_t ScriptStateGeneration() noexcept;
void ScriptStateChanged() noexcept;
} // namespace storm
