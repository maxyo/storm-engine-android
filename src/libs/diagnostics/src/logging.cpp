#include "logging.hpp"

#include <spdlog/sinks/dist_sink.h>
#include <spdlog/spdlog.h>
#ifdef __ANDROID__
#include <spdlog/sinks/android_sink.h>
#endif

#include "spdlog_sinks/syncable_sink.hpp"
#include "fs.h"

namespace
{

constexpr auto kLogExtension = ".log";
constexpr auto kErrorLoggerName = "error";

}

namespace storm::logging
{

logger_ptr getOrCreateLogger(const std::string &name, const spdlog::level::level_enum level, const bool logcat)
{
    auto logger = spdlog::get(name);
    if (logger)
    {
        return logger;
    }

    auto path = fs::GetLogsPath() / name;
    path += kLogExtension;

    logger = spdlog::create<sinks::syncable_sink>(name, path.string(), true);
    logger->set_level(level);
    if (name != kErrorLoggerName)
    {
        // Every logger's errors also go to error.log (only its file: logcat has them from this logger already), so
        // the one file a player is asked for holds every error, with the logger that wrote it.
        auto errors = std::make_shared<spdlog::sinks::dist_sink_st>(
            std::vector<spdlog::sink_ptr>{getOrCreateLogger(kErrorLoggerName)->sinks().front()});
        errors->set_level(spdlog::level::err);
        logger->sinks().push_back(std::move(errors));
    }
#ifdef __ANDROID__
    if (logcat)
        logger->sinks().push_back(std::make_shared<spdlog::sinks::android_sink_mt>("StormEngine"));
    // Flushing on info wrote the file on every Trace() of the game thread.
    // Warnings and errors are still flushed immediately; everything else every
    // kFlushPeriod (startPeriodicFlush), when the app goes to the background, on
    // a fatal error and by the crash handler.
    logger->flush_on(spdlog::level::warn);
#else
    (void)logcat; // stdout/stderr carry nothing of these loggers off Android
#endif

    return logger;
}

void startPeriodicFlush()
{
    // spdlog's periodic worker: its own thread, not the game thread, which a hang stops.
    spdlog::flush_every(kFlushPeriod);
}

} // namespace storm::logging