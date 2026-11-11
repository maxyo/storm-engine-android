#pragma once

// Relation numbers between the sea AI and the game's scripts.
//
// The engine works with RELATION_* of shared/sea_ai/script_defines.h. The scripts use the
// numbers of the game's own RESOURCE/shared/sea_ai/Script_defines.h, which differ between
// games: To Each His Own 1.7.3 has FRIEND 0, NEUTRAL 1, ENEMY 2, UNKNOWN 3 (no ALLIANCE),
// Caribbean Legend has the engine's UNKNOWN 0, FRIEND 1, NEUTRAL 2, ENEMY 3, ALLIANCE 4.
// Every relation crossing the script boundary (GET_RELATION_EVENT results, SeaAI.Update
// relations, AI_MESSAGE_GET_RELATION) is translated with the numbers the loaded scripts define.

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

#include "shared/sea_ai/script_defines.h"

namespace storm::sea_ai
{
class ScriptRelations
{
  public:
    // Reads the scripts' RELATION_* defines; `define` returns the value of an integer #define.
    // FRIEND, NEUTRAL, ENEMY and UNKNOWN are required, ALLIANCE is optional.
    static ScriptRelations Read(const std::function<std::optional<int32_t>(const char *)> &define)
    {
        ScriptRelations relations;
        for (auto &entry : relations.entries_)
        {
            const std::optional<int32_t> value = define(entry.name);
            if (!value)
            {
                if (entry.engine == RELATION_ALLIANCE)
                    continue;
                throw std::runtime_error(std::string("sea AI: the scripts do not define ") + entry.name);
            }
            for (const auto &other : relations.entries_)
            {
                if (&other != &entry && other.script == *value)
                    throw std::runtime_error(std::string("sea AI: the scripts give ") + entry.name + " and " +
                                             other.name + " the same value " + std::to_string(*value));
            }
            entry.script = *value;
        }
        return relations;
    }

    // Script relation number to the engine's RELATION_*; throws for a number the scripts do not define.
    [[nodiscard]] uint32_t ToEngine(int32_t script) const
    {
        for (const auto &entry : entries_)
        {
            if (entry.script == script)
                return entry.engine;
        }
        throw std::runtime_error("sea AI: the scripts returned relation " + std::to_string(script) +
                                 ", which their RELATION_* defines do not name");
    }

    // The engine's RELATION_* to the scripts' number; throws for a relation the scripts do not define.
    [[nodiscard]] int32_t ToScript(uint32_t engine) const
    {
        for (const auto &entry : entries_)
        {
            if (entry.engine == engine && entry.script)
                return *entry.script;
        }
        throw std::runtime_error("sea AI: relation " + std::to_string(engine) + " has no script number");
    }

  private:
    struct Entry
    {
        const char *name;
        uint32_t engine;
        std::optional<int32_t> script;
    };
    Entry entries_[5] = {{"RELATION_UNKNOWN", RELATION_UNKNOWN, std::nullopt},
                         {"RELATION_FRIEND", RELATION_FRIEND, std::nullopt},
                         {"RELATION_NEUTRAL", RELATION_NEUTRAL, std::nullopt},
                         {"RELATION_ENEMY", RELATION_ENEMY, std::nullopt},
                         {"RELATION_ALLIANCE", RELATION_ALLIANCE, std::nullopt}};
};
} // namespace storm::sea_ai
