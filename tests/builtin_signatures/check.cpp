// Built-ins whose signature differs between TEHO and Caribbean Legend resolve per game profile
// (src/libs/core/src/builtin_signatures.hpp): TEHO keeps its original return types.
#include "builtin_signatures.hpp"

#include <cstdio>
#include <cstdlib>

using storm::GameProfile;
using storm::builtins::ReturnKind;
using storm::builtins::argumentRange;
using storm::builtins::returnKind;

namespace
{
int failures = 0;

void expect(bool ok, const char *what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

bool range(const char *name, unsigned min, unsigned max)
{
    const auto *r = argumentRange(name);
    return r && r->min == min && r->max == max;
}
} // namespace

int main()
{
    expect(returnKind("SaveEngineState", GameProfile::TEHO, ReturnKind::Void) == ReturnKind::Void, "TEHO SaveEngineState void");
    expect(returnKind("SaveEngineState", GameProfile::CaribbeanLegend, ReturnKind::Void) == ReturnKind::Int,
           "CL SaveEngineState int");
    expect(returnKind("GetDeltaTime", GameProfile::TEHO, ReturnKind::Int) == ReturnKind::Int, "TEHO GetDeltaTime int");
    expect(returnKind("GetDeltaTime", GameProfile::CaribbeanLegend, ReturnKind::Int) == ReturnKind::Float,
           "CL GetDeltaTime float");
    expect(returnKind("findSubStr", GameProfile::TEHO, ReturnKind::String) == ReturnKind::String,
           "TEHO findSubStr string");
    expect(returnKind("findSubStr", GameProfile::CaribbeanLegend, ReturnKind::String) == ReturnKind::Int,
           "CL findSubStr int");
    expect(returnKind("sqrt", GameProfile::CaribbeanLegend, ReturnKind::Float) == ReturnKind::Float,
           "other built-ins keep the table type");
    expect(range("fts", 1, 2), "fts takes 1..2");
    expect(range("MapControl", 2, 3), "MapControl takes 2..3");
    expect(range("findSubStr", 3, 3), "findSubStr takes 3");
    expect(argumentRange("sqrt") == nullptr, "fixed-count built-ins have no range");
    if (failures)
        return EXIT_FAILURE;
    std::puts("builtin signatures: OK");
    return EXIT_SUCCESS;
}
