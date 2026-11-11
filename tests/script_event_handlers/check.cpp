// COMPILER::HasActiveEventHandler (the engine's question whether the boarding continuation
// handler LAi_ActivateReload is armed) over the interpreter's own event table.
#include "s_eventtab.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#define INVALID_FUNC_CODE 0xffffffff

struct FuncTable
{
    std::vector<std::string> names{"LAi_ActivateReload", "Other"};
    uint32_t FindFunc(const char *name) const
    {
        for (uint32_t n = 0; n < names.size(); ++n)
            if (names[n] == name)
                return n;
        return INVALID_FUNC_CODE;
    }
};

struct COMPILER
{
    FuncTable FuncTab;
    S_EVENTTAB EventTab;
    bool HasActiveEventHandler(const char *event_name, const char *func_name);
};
#include "method.inc"

int main()
{
    COMPILER compiler;
    const char *event = "Control Activation";
    assert(!compiler.HasActiveEventHandler(event, "LAi_ActivateReload"));        // no such event yet
    compiler.EventTab.AddEventHandler(event, 1, 0, 0);
    compiler.EventTab.ProcessFrame();
    assert(!compiler.HasActiveEventHandler(event, "LAi_ActivateReload"));        // another handler only
    assert(!compiler.HasActiveEventHandler(event, "Unknown"));                   // unknown function
    assert(!compiler.HasActiveEventHandler(nullptr, "LAi_ActivateReload"));
    compiler.EventTab.AddEventHandler(event, 0, 0, 1);                           // SetEventHandler(..., 1)
    compiler.EventTab.ProcessFrame();
    assert(compiler.HasActiveEventHandler(event, "LAi_ActivateReload"));         // armed: ProcessEvent would run it
    compiler.EventTab.SetStatus(event, 0, FSTATUS_DELETED);                      // DelEventHandler
    assert(!compiler.HasActiveEventHandler(event, "LAi_ActivateReload"));        // deleted: skipped at once
    compiler.EventTab.ProcessFrame();
    assert(!compiler.HasActiveEventHandler(event, "LAi_ActivateReload"));
    assert(compiler.HasActiveEventHandler(event, "Other"));
    std::cout << "PASS HasActiveEventHandler: absent event, other handler, unknown function, armed, deleted\n";
}
