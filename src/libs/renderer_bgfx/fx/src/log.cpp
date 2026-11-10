#include "storm/fx/log.hpp"

#include <cstdarg>
#include <cstdio>

namespace storm::fx
{
namespace
{
void DefaultSink(LogLevel level, const char *message)
{
    std::fprintf(stderr, "[fx] %s%s\n", level == LogLevel::Error ? "error: " : "", message);
}

LogSink g_sink = DefaultSink;
} // namespace

void SetLogSink(LogSink sink)
{
    g_sink = sink ? sink : DefaultSink;
}

void Log(LogLevel level, const char *format, ...)
{
    char message[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    g_sink(level, message);
}

} // namespace storm::fx
