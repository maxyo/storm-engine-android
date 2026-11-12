#pragma once
#ifdef __ANDROID__
// The running script's calls for platform/android_game_message_target.hpp: the interpreter's
// call stack and the arguments of its calls. Game thread only.
#include "core_private.h"
#include "platform/android_game_message_target.hpp"
#include "v_data.h"

namespace storm::android
{
inline std::string gameMessageTarget()
{
    auto &scripts = static_cast<CorePrivate &>(core);
    ScriptCalls calls;
    calls.depth = [&](const char *function) { return scripts.ScriptCallDepth(function); };
    calls.text = [&](const char *function, unsigned index) -> std::optional<std::string> {
        const char *value = nullptr;
        auto *data = scripts.ScriptCallArgument(function, index);
        return data && data->Get(value) && value ? std::optional<std::string>(value) : std::nullopt;
    };
    calls.number = [&](const char *function, unsigned index) -> std::optional<int> {
        int32_t value = 0;
        auto *data = scripts.ScriptCallArgument(function, index);
        return data && data->Get(value) ? std::optional<int>(value) : std::nullopt;
    };
    calls.attribute = [&](const char *function, unsigned index, const char *name) -> std::optional<std::string> {
        auto *data = scripts.ScriptCallArgument(function, index);
        ATTRIBUTES *object = data ? data->GetAClass() : nullptr;
        const char *value = object ? object->GetAttribute(name) : nullptr;
        return value ? std::optional<std::string>(value) : std::nullopt;
    };
    return gameMessageTarget(calls);
}
} // namespace storm::android
#endif
