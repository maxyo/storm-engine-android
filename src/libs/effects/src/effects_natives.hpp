// The EFFECTS_LIBRARY natives (effects_library.cpp), declared for the host check.
#pragma once

#include <cstdint>

class VS_STACK;

namespace storm::effects::natives
{
uint32_t CheckAndCreateEffect(VS_STACK *stack);
uint32_t ReloadEffect(VS_STACK *stack);
uint32_t ReloadAllEffects(VS_STACK *stack);
uint32_t UnloadEffect(VS_STACK *stack);
uint32_t UnloadAllEffects(VS_STACK *stack);
} // namespace storm::effects::natives
