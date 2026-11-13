#include "syncable_sink.hpp"

#include <cstdio>

#include <unistd.h>

#include <spdlog/common.h>
#include <spdlog/pattern_formatter.h>

storm::logging::sinks::syncable_sink::syncable_sink(const spdlog::filename_t &filename, bool truncate)
{
    file_helper_.open(filename, truncate);
}

void storm::logging::sinks::syncable_sink::log(const spdlog::details::log_msg &msg)
{
    std::lock_guard lock(mutex_);
    spdlog::memory_buf_t formatted;
    formatter_->format(msg, formatted);
    file_helper_.write(formatted);
}

void storm::logging::sinks::syncable_sink::flush()
{
    std::lock_guard lock(mutex_);
    file_helper_.flush();
}

void storm::logging::sinks::syncable_sink::set_pattern(const std::string &pattern)
{
    std::lock_guard lock(mutex_);
    formatter_ = spdlog::details::make_unique<spdlog::pattern_formatter>(pattern);
}

void storm::logging::sinks::syncable_sink::set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter)
{
    std::lock_guard lock(mutex_);
    formatter_ = std::move(sink_formatter);
}

void storm::logging::sinks::syncable_sink::sync() const
{
    fsync(fileno(file_helper_.getfd()));
}

void storm::logging::sinks::syncable_sink::terminate_immediately()
{
    sync();
    file_helper_.close();
}
