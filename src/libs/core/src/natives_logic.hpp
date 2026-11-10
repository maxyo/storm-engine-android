#pragma once

// Engine-independent parts of the script natives in natives_ext.cpp (tested by tests/natives_ext).

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace storm::natives
{
// hash(string): 32-bit FNV-1a reinterpreted as a signed int. CL scripts only use it as a
// deterministic source of randomness (Program/RandomUtilities.c maps it to [0, 1)).
int32_t hash32(std::string_view text);

// hrand(n[, tag]) returns a number in [0, n] that stays the same while the global seed (set by
// SetGlobalSeed, once per game day) stays the same. Calls are independent per call site unless
// the tag starts with '&': then every call with that tag gives the same number
// (documented in the header comment of Program/RandomUtilities.c).
int32_t hashRandom(int32_t n, std::string_view global_seed, std::string_view call_site, std::string_view tag);

// Sort keys of SmartSort / SmartSortTable: the attribute value with `offset` leading characters
// skipped, compared as "int", "float", "string" (case-insensitive) or "date".
enum class SortType
{
    Int,
    Float,
    String,
    Date,
};
// Returns false for an unknown type name.
bool parseSortType(std::string_view name, SortType &type);

struct SortKey
{
    double number = 0.0;          // Int, Float
    std::string text;             // String: lower-cased
    std::vector<int32_t> date;    // Date: year, month, day, then the remaining numbers (time)
};
SortKey makeSortKey(std::string_view value, int32_t offset, SortType type);
// Strict weak order of keys of the same type.
bool sortKeyLess(const SortKey &lhs, const SortKey &rhs, SortType type);

// Sprintf(format, ...): C printf conversions d i u x X o c f F e E g G s and %%, with flags,
// width and precision ('*' is not supported). A conversion without an argument or an unknown
// conversion is kept verbatim and reported through `error`.
using FormatArg = std::variant<int32_t, float, std::string>;
std::string formatScript(std::string_view fmt, const std::vector<FormatArg> &args, std::string &error);
} // namespace storm::natives
