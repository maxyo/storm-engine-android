// The sea AI must read the relations the game's scripts return in the scripts' own numbering.
// FindIntegerDefine comes verbatim from compiler.cpp and runs against the real S_DEFTAB.
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

#include "s_deftab.h"
#include "script_relations.hpp"
#include "token.h"

namespace
{
void require(bool condition, const std::string &what)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << what << '\n';
        std::exit(1);
    }
}

template <class F> void requireThrows(F &&f, const std::string &what)
{
    try
    {
        f();
    }
    catch (const std::runtime_error &)
    {
        return;
    }
    require(false, what + " did not throw");
}

struct DefineLookup
{
    S_DEFTAB DefTab;
#include "method.inc"
};

using Defines = std::map<std::string, int32_t>;

storm::sea_ai::ScriptRelations Read(const Defines &defines)
{
    return storm::sea_ai::ScriptRelations::Read([&](const char *name) -> std::optional<int32_t> {
        const auto found = defines.find(name);
        return found == defines.end() ? std::nullopt : std::optional<int32_t>(found->second);
    });
}

// The engine's decisions (fire, attack, colours) on the relation a script returned.
void requireNumbering(const char *game, const Defines &defines)
{
    const auto relations = Read(defines);
    const std::pair<const char *, uint32_t> expected[] = {{"RELATION_FRIEND", RELATION_FRIEND},
                                                          {"RELATION_NEUTRAL", RELATION_NEUTRAL},
                                                          {"RELATION_ENEMY", RELATION_ENEMY},
                                                          {"RELATION_UNKNOWN", RELATION_UNKNOWN}};
    for (const auto &[name, engine] : expected)
    {
        const int32_t script = defines.at(name);
        require(relations.ToEngine(script) == engine, std::string(game) + ": script " + name + " is not the engine's");
        require(relations.ToScript(engine) == script, std::string(game) + ": engine " + name + " is not the script's");
    }
}
} // namespace

int main()
{
    // To Each His Own 1.7.3, RESOURCE/shared/sea_ai/Script_defines.h: an enemy is 2, which is the
    // engine's NEUTRAL; read as is, no enemy ship ever fired at the player.
    const Defines teho = {{"RELATION_UNKNOWN", 3}, {"RELATION_FRIEND", 0}, {"RELATION_NEUTRAL", 1}, {"RELATION_ENEMY", 2}};
    requireNumbering("TEHO 1.7.3", teho);
    require(Read(teho).ToEngine(2) == RELATION_ENEMY, "TEHO enemy is not an enemy");
    requireThrows([&] { (void)Read(teho).ToScript(RELATION_ALLIANCE); }, "TEHO alliance without a script number");

    // Caribbean Legend: the engine's own numbering, with ALLIANCE.
    const Defines legend = {{"RELATION_UNKNOWN", 0}, {"RELATION_FRIEND", 1}, {"RELATION_NEUTRAL", 2},
                            {"RELATION_ENEMY", 3}, {"RELATION_ALLIANCE", 4}};
    requireNumbering("Caribbean Legend", legend);
    require(Read(legend).ToEngine(4) == RELATION_ALLIANCE, "Legend alliance");

    // Broken script headers fail loudly instead of guessing.
    Defines missing = teho;
    missing.erase("RELATION_ENEMY");
    requireThrows([&] { (void)Read(missing); }, "missing RELATION_ENEMY");
    Defines duplicate = teho;
    duplicate["RELATION_ENEMY"] = 1;
    requireThrows([&] { (void)Read(duplicate); }, "RELATION_ENEMY equal to RELATION_NEUTRAL");
    requireThrows([&] { (void)Read(teho).ToEngine(7); }, "a relation the scripts do not define");

    // The defines come from the loaded scripts' define table.
    DefineLookup lookup;
    char enemyName[] = "RELATION_ENEMY", floatName[] = "fGravity";
    int32_t enemy = 2;
    float gravity = 9.81f;
    DEFINFO di{};
    di.name = enemyName;
    di.segment_id = 1;
    di.deftype = NUMBER;
    std::memcpy(&di.data4b, &enemy, sizeof(enemy));
    lookup.DefTab.AddDef(di);
    di.name = floatName;
    di.deftype = FLOAT_NUMBER;
    di.data4b = 0;
    std::memcpy(&di.data4b, &gravity, sizeof(gravity));
    lookup.DefTab.AddDef(di);
    require(lookup.FindIntegerDefine("RELATION_ENEMY") == 2, "integer define");
    require(!lookup.FindIntegerDefine("fGravity"), "a float define is not an integer");
    require(!lookup.FindIntegerDefine("RELATION_ALLIANCE"), "an undefined name");
    lookup.DefTab.InvalidateBySegmentID(1);
    require(!lookup.FindIntegerDefine("RELATION_ENEMY"), "a define of an unloaded segment");

    std::cout << "sea AI relations: OK\n";
    return 0;
}
