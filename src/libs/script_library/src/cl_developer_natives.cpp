// Developer switches Caribbean Legend 2.0 declares in Program/seadogs.c and queries at start-up:
// safe mode (CL's launcher can start the game without mods/overlays), skipping the intro videos
// and a developer quick load of a named save. This engine has no such launcher modes: all are off.
// They are fixed answers rather than stubs (nothing is left undone), so they do not log.
// Registered by SCRIPT_LIBRIARY_TEST (#libriary "script_libriary_test").

#include "cl_developer_natives.h"

#include "script_stub.hpp"
#include "v_s_stack.h"

namespace
{
uint32_t pushFalse(VS_STACK *s)
{
    auto *v = static_cast<VDATA *>(s->Push());
    if (!v)
        return IFUNCRESULT_FAILED;
    v->Set(static_cast<int32_t>(0));
    return IFUNCRESULT_OK;
}

uint32_t pushEmpty(VS_STACK *s)
{
    auto *v = static_cast<VDATA *>(s->Push());
    if (!v)
        return IFUNCRESULT_FAILED;
    v->Set("");
    return IFUNCRESULT_OK;
}
} // namespace

void registerClDeveloperNatives()
{
    storm::registerScriptFunction("IsInSafeMode", "int", 0, pushFalse);
    storm::registerScriptFunction("IsDevSkipIntro", "int", 0, pushFalse);
    storm::registerScriptFunction("IsDevQuickLoad", "int", 0, pushFalse);
    storm::registerScriptFunction("GetDevQuickLoadSave", "string", 0, pushEmpty);
}
