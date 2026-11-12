#pragma once

// common includes
#include "core.h"

class CorePrivate : public Core
{
public:
    virtual void Init() = 0;

    virtual void InitBase() = 0;
    virtual void ReleaseBase() = 0;

    virtual void CleanUp() = 0;

    virtual bool Run() = 0;
    [[nodiscard]] virtual bool initialized() const = 0;

    virtual void AppState(bool state) = 0;

    virtual void collectCrashInfo() const = 0;

    virtual void SetWindow(std::shared_ptr<storm::OSWindow> window) = 0;

    // Whether the script function `func_name` is an executable handler of `event_name` now (registered,
    // not deleted). Reads the interpreter's event table only; nothing is dispatched or registered.
    virtual bool HasActiveEventHandler(const char *event_name, const char *func_name) = 0;
    // The running call of the script function `func_name` nearest the top of the script call
    // stack: its depth (0 = innermost), -1 when it is not running; argument `index` of that call,
    // nullptr when there is none. Reads the interpreter's call stack only.
    virtual int ScriptCallDepth(const char *func_name) = 0;
    virtual VDATA *ScriptCallArgument(const char *func_name, uint32_t index) = 0;
};
