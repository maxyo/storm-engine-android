#pragma once

// Internal script functions added for Caribbean Legend 2.0; the list is natives_ext_list.h.
// COMPILER::BC_CallIntFunction hands every id from FUNC_NATIVE_EXT_BASE on to callNativeExt with
// the VM state the functions need (the members are private to COMPILER).

#include <cstdint>
#include <functional>
#include <string_view>
#include <tuple>

#include "ringbuffer_stack.hpp"

class COMPILER;
class DATA;
class S_STACK;
class POSTEVENTS_LIST;
class FuncTable;
class VarTable;
class S_DEFTAB;

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
    // for natives that read globals and defines or call script functions (natives_dlg.cpp)
    const FuncTable &functions;
    const VarTable &variables;
    S_DEFTAB &defines;
    // COMPILER::BC_Execute: runs a script function whose arguments are on `stack`
    std::function<bool(uint32_t function, DATA *&result)> execute;
};

// `index` = function code - FUNC_NATIVE_EXT_BASE. Consumes the arguments; returns the pushed
// result (also stored in `result`) or nullptr for void functions.
DATA *callNativeExt(uint32_t index, NativeCall &call, DATA *&result);
bool isNativeExtVarArgs(uint32_t index);
} // namespace storm::natives
