#include "logging.hpp"

#include <spdlog/spdlog.h>
#ifdef __ANDROID__
#include <spdlog/sinks/android_sink.h>
#endif

#include "spdlog_sinks/syncable_sink.hpp"
#include "fs.h"

namespace
{

constexpr auto kLogExtension = ".log";

}

namespace storm::logging
{

// TODO: loggers with periodic flush shall be thread safe; we should measure performance diff and decide what to do
logger_ptr getOrCreateLogger(const std::string &name, const spdlog::level::level_enum level,
                                                  const bool truncate)
{
    auto logger = spdlog::get(name);
    if (logger)
    {
        return logger;
    }

    auto path = fs::GetLogsPath() / name;
    path += kLogExtension;

    logger = spdlog::create<sinks::syncable_sink>(name, path.string(), truncate);
    logger->set_level(level);
#ifdef __ANDROID__
    logger->sinks().push_back(std::make_shared<spdlog::sinks::android_sink_mt>("StormEngine"));
#endif
    // Warnings and errors (script compile errors among them) reach the file at once.
    // Flushing on info wrote the file on every Trace() of the game thread; lower levels
    // are flushed periodically by LifecycleDiagnosticsService::notifyAfterRun, when the
    // app goes to the background, at shutdown, and on a fatal signal or std::terminate
    // (LoggingService in lifecycle_diagnostics_service.cpp).
    logger->flush_on(spdlog::level::warn);

    return logger;
}

} // namespace storm::logging