#pragma once
#include <cstdint>
namespace storm::android
{
// Game-thread monotonic error serial. A synchronous optional adapter can reject
// a partially executed script event even when the interpreter logs and continues.
inline uint64_t scriptErrorEpoch = 0;
}
