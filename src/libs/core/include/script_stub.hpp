#pragma once

// Helpers for script natives that a game declares but the engine does not (fully) implement.
//
// A stub is explicit: its behaviour is documented next to its registration and the first call
// of each stubbed native logs one warning naming it and what it does instead (a desktop-only
// feature, a platform service we do not have, a part of another workstream not wired yet).

#include <cstdint>
#include <string_view>

#include "s_import_func.h"

namespace storm
{
// Logs "script native '<name>' is a stub: <behaviour>" once per name (thread-safe).
void reportScriptStub(std::string_view name, std::string_view behaviour);

// Registers a script library function (see SCRIPT_LIBRIARY::Init). The scripts declare it with
// `native`; a registered native the running game does not declare is simply unused.
void registerScriptFunction(const char *name, const char *return_type, uint32_t arguments, SIMPORTFUNC function);
} // namespace storm
