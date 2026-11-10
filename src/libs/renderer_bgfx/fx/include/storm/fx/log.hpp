// Diagnostics of the fx package. Nothing in the package switches to a lesser
// path on its own: what the device cannot do is decided once, up front, by
// ConstrainSettings() (settings.hpp), and a call that still cannot be honoured
// is refused and reported here.
#pragma once

namespace storm::fx
{

enum class LogLevel
{
    Info,  // capability report, settings changed by ConstrainSettings
    Error, // a request that was refused or could not be honoured: a bug in the caller or a missing input
};

using LogSink = void (*)(LogLevel level, const char *message);

// Default sink writes to stderr. Pass nullptr to restore it.
void SetLogSink(LogSink sink);
void Log(LogLevel level, const char *format, ...);

// Reports a condition once per `reported` flag (a function-local or member bool), so per-frame calls do not
// flood the log.
#define STORM_FX_ERROR_ONCE(reported, ...)                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(reported))                                                                                               \
        {                                                                                                              \
            (reported) = true;                                                                                         \
            ::storm::fx::Log(::storm::fx::LogLevel::Error, __VA_ARGS__);                                               \
        }                                                                                                              \
    } while (false)

} // namespace storm::fx
