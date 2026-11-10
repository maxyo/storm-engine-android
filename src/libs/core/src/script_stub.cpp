#include "script_stub.hpp"

#include "core.h"

#include <mutex>
#include <set>
#include <string>

#include <spdlog/spdlog.h>

namespace storm
{
void reportScriptStub(std::string_view name, std::string_view behaviour)
{
    static std::mutex mutex;
    static std::set<std::string, std::less<>> reported;
    {
        std::lock_guard lock(mutex);
        if (reported.contains(name))
            return;
        reported.emplace(name);
    }
    spdlog::warn("script native '{}' is a stub: {}", name, behaviour);
}

void registerScriptFunction(const char *name, const char *return_type, uint32_t arguments, SIMPORTFUNC function)
{
    IFUNCINFO info;
    info.pFuncName = name;
    info.pReturnValueName = return_type;
    info.nArguments = arguments;
    info.pFuncAddress = function;
    core.SetScriptFunction(&info);
}
} // namespace storm
