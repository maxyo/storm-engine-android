#pragma once

#include <mutex>

#include <spdlog/common.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/sink.h>

// TODO: write own helper or patch spdlog to retrieve fd (protected or getter)
// this is basically spdlog::details::file_helper with additional getfd method
// this may break down after spdlog update
#include "file_helper.hpp"

namespace storm::logging::sinks
{

class syncable_sink final : public spdlog::sinks::sink
{
  public:
    syncable_sink(const spdlog::filename_t &filename, bool truncate);

    syncable_sink(const syncable_sink &) = delete;
    syncable_sink(syncable_sink &&) = delete;

    syncable_sink &operator=(const syncable_sink &) = delete;
    syncable_sink &operator=(syncable_sink &&) = delete;

    void log(const spdlog::details::log_msg &msg) override;
    void flush() override;
    void set_pattern(const std::string &pattern) override;
    void set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter) override;

    void sync() const;
    void terminate_immediately();

  protected:
    // Several loggers write one file (every logger's errors go to error.log, logging.cpp) from any thread, and
    // the logging thread flushes meanwhile. sync() and terminate_immediately() do not lock: the crash handler
    // calls them, possibly while the crashed thread holds the lock.
    std::mutex mutex_;
    std::unique_ptr<spdlog::formatter> formatter_;
    details::file_helper file_helper_;
};

} // namespace storm::spdlog_sinks
