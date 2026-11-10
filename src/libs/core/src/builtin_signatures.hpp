#pragma once

// Built-in script functions whose signature differs between To Each His Own and Caribbean Legend.
//
// CL declares every built-in it uses with `native` (Program/internal.c) in its own engine's
// signature, and the compiler checks those declarations against ours. Where one signature can
// serve both games the built-in accepts a range of argument counts (kBuiltinArgumentRanges,
// checked by the implementation). Where the return type differs, the built-in returns the type
// of the running game (kBuiltinReturnTypes, resolved by storm::gameProfile() when the compiler
// registers its built-ins); TEHO keeps its original types, so its bytecode does not change.

#include "game_profile.hpp"

#include <cstdint>
#include <string_view>

namespace storm::builtins
{
enum class ReturnKind
{
    Void,
    Int,
    Float,
    String,
};

struct ReturnTypeByProfile
{
    std::string_view name;
    ReturnKind teho;
    ReturnKind caribbean_legend;
};

inline constexpr ReturnTypeByProfile kBuiltinReturnTypes[] = {
    {"SaveEngineState", ReturnKind::Void, ReturnKind::Int}, // CL: bool, whether the state was saved
    {"GetDeltaTime", ReturnKind::Int, ReturnKind::Float},   // milliseconds of the last frame (scaled)
    {"findSubStr", ReturnKind::String, ReturnKind::Int},    // position or -1 (TEHO's table said string)
};

struct ArgumentRange
{
    std::string_view name;
    uint32_t min;
    uint32_t max;
};

inline constexpr ArgumentRange kBuiltinArgumentRanges[] = {
    {"fts", 1, 2},        // fts(float[, significant digits]); CL declares the 1-argument form
    {"MapControl", 2, 3}, // MapControl(control, key[, ?]); CL declares a third argument it never passes
    {"findSubStr", 3, 3}, // findSubStr(&string, substring, start); CL declares it without parameters
};

// The return kind of built-in `name` in `profile`, or `fallback` (the engine table's kind) when
// the built-in is not in kBuiltinReturnTypes.
constexpr ReturnKind returnKind(std::string_view name, GameProfile profile, ReturnKind fallback)
{
    for (const auto &entry : kBuiltinReturnTypes)
        if (entry.name == name)
            return profile == GameProfile::CaribbeanLegend ? entry.caribbean_legend : entry.teho;
    return fallback;
}

constexpr const ArgumentRange *argumentRange(std::string_view name)
{
    for (const auto &entry : kBuiltinArgumentRanges)
        if (entry.name == name)
            return &entry;
    return nullptr;
}
} // namespace storm::builtins
