#pragma once
// attributes.cpp only needs its portable fixed-buffer formatting overload.
#include <cstdio>
template <size_t N, typename... Args>
int sprintf_s(char (&out)[N], const char *format, Args... args)
{
    return std::snprintf(out, N, format, args...);
}
