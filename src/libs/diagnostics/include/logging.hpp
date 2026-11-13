#pragma once
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
} // namespace storm::logging
