#include "lifecycle_diagnostics_service.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

#include <spdlog/spdlog.h>

#include "fs.h"
#include "v_file_service.h"
#include "spdlog_sinks/syncable_sink.hpp"
#include "watermark.hpp"

#ifdef STORM_ENABLE_CRASH_REPORTS

namespace
{
auto& getExecutableDir()
{
    static const auto executableDir = std::filesystem::path{std::filesystem::u8path(fio->_GetExecutableDirectory())};
    return executableDir;
}
auto &getLogsArchive()
{
    static const auto logsArchive = fs::GetLogsPath().replace_extension(".7z");
    return logsArchive;
}

void log_sentry(sentry_level_t level, const char *message, va_list args, void *)
{
    // TODO:
    /* spdlog::level::level_enum log_level = spdlog::level::critical;
    switch (level)
    {
    case SENTRY_LEVEL_DEBUG:
        log_level = spdlog::level::debug;
        break;
    case SENTRY_LEVEL_INFO:
        log_level = spdlog::level::info;
        break;
    case SENTRY_LEVEL_WARNING:
        log_level = spdlog::level::warn;
        break;
    case SENTRY_LEVEL_ERROR:
        log_level = spdlog::level::err;
        break;
    case SENTRY_LEVEL_FATAL:
        log_level = spdlog::level::critical;
        break;
    }

    char text[128];
    if (vsnprintf(text, std::size(text), message, args) > 0)
    {
        spdlog::log(log_level, text);
    }*/
}

}

#endif // STORM_ENABLE_CRASH_REPORTS

namespace storm::diag
{

class LoggingService final
{
  public:
    ~LoggingService()
    {
        terminate();
    }

    void initialize()
    {
        if (terminate_)
        {
            terminate_ = false;

            static auto terminate_handler = std::get_terminate();
            std::set_terminate([] {
                terminate_handler();
            });

            create_directories(fs::GetLogsPath());

            std::thread worker{[this] { loggingThread(); }};
            worker.detach();
        }
    }

    void terminate()
    {
        using namespace std::chrono_literals;

        if (!terminate_)
        {
            terminate_ = true;
            {
                std::lock_guard lock(mtx_);
                flushRequested_ = true;
            }
            cv_.notify_one();

            std::unique_lock lock(mtx_);
            cv_.wait_for(lock, 5s, [this] { return !flushRequested_; });
            spdlog::shutdown();
        }
    }

  private:
    std::mutex mtx_;
    std::condition_variable cv_;
    bool flushRequested_{false};
    std::atomic_bool terminate_{true};

    void flushAll(const bool terminate) const
    {
        spdlog::apply_all([terminate](std::shared_ptr<spdlog::logger> l) {
            l->flush();

            if (terminate)
            {
                for (auto &sink : l->sinks())
                {
                    if (const auto syncable_sink = std::dynamic_pointer_cast<logging::sinks::syncable_sink>(sink))
                    {
                        syncable_sink->terminate_immediately();
                    }
                }
            }
        });
    }

    void loggingThread()
    {
        while (!terminate_)
        {
            std::unique_lock lock(mtx_);
            cv_.wait(lock, [this] { return flushRequested_; });

            flushAll(terminate_);

            flushRequested_ = false;
            cv_.notify_one();
        }
    }
};

LifecycleDiagnosticsService::LifecycleDiagnosticsService() : loggingService_(std::make_unique<LoggingService>())
{
}

LifecycleDiagnosticsService::~LifecycleDiagnosticsService()
{
    terminate();
}

LifecycleDiagnosticsService::Guard LifecycleDiagnosticsService::initialize(const bool enableCrashReports)
{
    loggingService_->initialize();

#ifdef STORM_ENABLE_CRASH_REPORTS
    if (!initialized_)
    {
        // TODO: make this crossplatform
        auto *options = sentry_options_new();
#ifdef _DEBUG
        sentry_options_set_debug(options, true);
#endif
        sentry_options_set_logger(options, log_sentry, nullptr);
        sentry_options_set_dsn(options, "https://1798a1bcfb654cbd8ce157b381964525@o572138.ingest.sentry.io/5721165");
        sentry_options_set_release(options, STORM_BUILD_WATERMARK_STRING);
        sentry_options_set_database_path(options, (fs::GetStashPath() / "sentry-db").c_str());
        sentry_options_set_handler_path(options, (getExecutableDir() / "crashpad_handler").c_str());
        sentry_options_add_attachment(options, getLogsArchive().c_str());
        sentry_options_set_on_crash(options, beforeCrash, this);
        sentry_options_set_system_crash_reporter_enabled(options, enableCrashReports);

        initialized_ = sentry_init(options) == 0;
    }

#else
    // Logging remains available without linking a crash-reporting SDK.
    (void)enableCrashReports;
    initialized_ = true;
#endif

    return Guard(*this);
}

void LifecycleDiagnosticsService::terminate() const
{
    loggingService_->terminate();

#ifdef STORM_ENABLE_CRASH_REPORTS
    if (initialized_)
    {
        sentry_close();
    }
#endif
}

void LifecycleDiagnosticsService::setCrashInfoCollector(crash_info_collector f)
{
    collectCrashInfo_ = std::move(f);
}

#ifdef STORM_ENABLE_CRASH_REPORTS
sentry_value_t LifecycleDiagnosticsService::beforeCrash(const sentry_ucontext_t *uctx, sentry_value_t event,
                                                        void *closure)
{
    const auto *self = static_cast<LifecycleDiagnosticsService *>(closure);

    // collect engine data
    if (self->collectCrashInfo_)
    {
        self->collectCrashInfo_();
    }

    // terminate logging
    self->loggingService_->terminate();

    return event;
}

#endif // STORM_ENABLE_CRASH_REPORTS

} // namespace storm::diag
