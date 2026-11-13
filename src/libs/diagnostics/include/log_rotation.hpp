#pragma once
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace storm::logging
{

// The suffix of the previous session's copy of a log: system.log -> system.prev.log.
inline constexpr char kPreviousSessionSuffix[] = ".prev.log";

// Keeps the previous process's logs: every <name>.log in `logs` becomes <name>.prev.log, replacing the
// generation before it, and a <name>.prev.log whose <name>.log the previous process did not write is removed
// (it belongs to an older session and would be read as the previous one). The loggers truncate their files
// when they open them (getOrCreateLogger), so a restart after a hang or a crash wiped the evidence; call this
// before anything opens a log. A missing directory is a first start. Returns one line per file it could not
// rotate or remove; the caller logs each as an error once its logger is open.
inline std::vector<std::string> rotatePreviousSessionLogs(const std::filesystem::path &logs)
{
    namespace fs = std::filesystem;
    std::vector<std::string> failures;
    std::error_code error;
    const auto status = fs::status(logs, error);
    if (status.type() == fs::file_type::not_found)
        return failures;
    if (error)
    {
        failures.push_back("cannot read " + logs.string() + ": " + error.message());
        return failures;
    }
    if (!fs::is_directory(status))
        return failures;
    const auto isPrevious = [](const std::string &name) {
        constexpr size_t suffix = sizeof(kPreviousSessionSuffix) - 1;
        return name.size() > suffix && name.compare(name.size() - suffix, suffix, kPreviousSessionSuffix) == 0;
    };
    std::vector<fs::path> current, previous;
    for (fs::directory_iterator it(logs, error), end; !error && it != end; it.increment(error))
    {
        std::error_code type;
        if (!it->is_regular_file(type) || it->path().extension() != ".log")
            continue;
        (isPrevious(it->path().filename().string()) ? previous : current).push_back(it->path());
    }
    if (error)
    {
        failures.push_back("cannot list " + logs.string() + ": " + error.message());
        return failures;
    }
    const auto previousOf = [](const fs::path &log) {
        return fs::path(log).replace_filename(log.stem().string() + kPreviousSessionSuffix);
    };
    for (const auto &old : previous)
    {
        bool rotated = false;
        for (const auto &log : current)
            rotated = rotated || previousOf(log) == old;
        if (!rotated && !fs::remove(old, error) && error)
            failures.push_back("cannot remove the older session's " + old.string() + ": " + error.message());
    }
    for (const auto &log : current)
    {
        fs::rename(log, previousOf(log), error);
        if (error)
            failures.push_back("cannot keep " + log.string() + " as " + previousOf(log).string() + ": " +
                               error.message() + "; its previous session is lost when the log opens");
    }
    return failures;
}

} // namespace storm::logging
