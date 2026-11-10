#pragma once

// Caribbean Legend localisation natives (natives_dlg.cpp), dispatched from natives_ext.cpp.

#include "natives_ext.h"

namespace storm::dlg
{
enum class DlgNative
{
    Dlgo,
    Convert,
    ConvertE,
    ReplaceAllMatches,
    StringFromKey,
    RefreshLocalization,
};

// Consumes the call's arguments; returns the pushed string result (nullptr for void natives).
DATA *callDlgNative(DlgNative native, natives::NativeCall &call, DATA *&result);
} // namespace storm::dlg
