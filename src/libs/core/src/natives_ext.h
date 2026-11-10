#pragma once

// Internal script functions added for Caribbean Legend 2.0; the list is natives_ext_list.h.
// COMPILER::BC_CallIntFunction hands every id from FUNC_NATIVE_EXT_BASE on to callNativeExt with
// the VM state the functions need (the members are private to COMPILER).

#include <cstdint>
#include <string_view>
#include <tuple>

#include "ringbuffer_stack.hpp"

class COMPILER;
class DATA;
class S_STACK;
class POSTEVENTS_LIST;

namespace storm::natives
{
using CallStack = storm::ringbuffer_stack<std::tuple<const char *, size_t, const char *>, size_t{64}>;

struct NativeCall
{
    COMPILER &compiler;
    S_STACK &stack;          // the arguments are its last `arguments` entries
    POSTEVENTS_LIST &posts;  // pending PostEvent messages
    const CallStack &calls;  // innermost last; its top is the native itself
    std::string_view caller; // name of the script function that called the native
    uint32_t instruction;    // code offset of the call, identifies the call site in `caller`
    uint32_t arguments;
};

// `index` = function code - FUNC_NATIVE_EXT_BASE. Consumes the arguments; returns the pushed
// result (also stored in `result`) or nullptr for void functions.
DATA *callNativeExt(uint32_t index, NativeCall &call, DATA *&result);
bool isNativeExtVarArgs(uint32_t index);
} // namespace storm::natives
