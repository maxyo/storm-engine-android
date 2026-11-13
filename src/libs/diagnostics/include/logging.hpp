#pragma once
#include <chrono>
#include <filesystem>
#include <string>

#include <spdlog/logger.h>

namespace storm::logging
{

using logger_ptr = std::shared_ptr<spdlog::logger>;

// The logger writing Logs/<name>.log (opened truncated: log_rotation.hpp keeps the previous session's). On
// Android its lines also go to logcat unless `logcat` is false (a source that logs there itself).
logger_ptr getOrCreateLogger(const std::string &name, spdlog::level::level_enum level = spdlog::level::trace,
                             bool logcat = true);
// Lines below warn reach the files at most this long after they are written (warnings and errors at once on
// Android, logging.cpp).
inline constexpr std::chrono::seconds kFlushPeriod{2};

// Flushes every logger every kFlushPeriod from spdlog's own thread: a hung game thread keeps its last lines.
void startPeriodicFlush();
} // namespace storm::logging
