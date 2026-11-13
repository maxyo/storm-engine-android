#pragma once
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

// The engine's logs across game processes (sessions): the logs of the last sessions are kept, and a session that
// ended without a clean exit (a hang the player swiped away, a crash, a kill in the foreground) is told at the next
// start. The app's diagnostics export reads both (DiagnosticsReport).
namespace storm::logging
{

// Sessions kept besides the running one: <name>.prev1.log is the session before, <name>.prev<k>.log k sessions back.
inline constexpr int kKeptSessions = 3;
// Left in the logs directory while a session runs in the foreground (see markSessionRunning).
inline constexpr char kRunningMarker[] = "session-running.txt";

// "<base>.log" of the session `generation` sessions back (0: the running one).
inline std::string sessionLogName(const std::string &base, int generation)
{
    return generation == 0 ? base + ".log" : base + ".prev" + std::to_string(generation) + ".log";
}

namespace detail
{
// {base, generation} of a log file name, or nothing for other files. "<base>.prev.log" is generation 1: the one
// session engine 761488e0 kept.
inline std::optional<std::pair<std::string, int>> parseLogName(const std::string &name)
{
    constexpr char extension[] = ".log";
    constexpr size_t extensionSize = sizeof(extension) - 1;
    if (name.size() <= extensionSize || name.compare(name.size() - extensionSize, extensionSize, extension) != 0)
        return std::nullopt;
    const std::string stem = name.substr(0, name.size() - extensionSize);
    const auto dot = stem.rfind(".prev");
    if (dot == std::string::npos || dot == 0)
        return std::pair{stem, 0};
    const std::string number = stem.substr(dot + 5);
    if (number.empty())
        return std::pair{stem.substr(0, dot), 1};
    if (number.size() > 3 || !std::all_of(number.begin(), number.end(), [](char c) { return c >= '0' && c <= '9'; }) ||
        number[0] == '0')
        return std::pair{stem, 0};
    return std::pair{stem.substr(0, dot), std::stoi(number)};
}

// The last non-empty line of a text file, "" when it has none or cannot be read.
inline std::string lastLine(const std::filesystem::path &file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return {};
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    const std::streamoff count = std::min<std::streamoff>(size, 8192);
    std::string tail(static_cast<size_t>(count), '\0');
    in.seekg(size - count);
    in.read(tail.data(), count);
    while (!tail.empty() && (tail.back() == '\n' || tail.back() == '\r'))
        tail.pop_back();
    const auto start = tail.rfind('\n');
    return start == std::string::npos ? tail : tail.substr(start + 1);
}
} // namespace detail

// Shifts every session's logs one session back: <name>.log becomes <name>.prev1.log, .prev1 becomes .prev2 and so
// on; the files of the session kKeptSessions back are removed. Each file moves with its session, so a log a session
// did not write leaves a gap instead of being read as that session's. The loggers truncate their files when they
// open them (getOrCreateLogger): call this before anything opens a log. A missing directory is a first start.
// Returns one line per file it could not move or remove; the caller logs each as an error once its logger is open.
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
    std::map<std::string, std::vector<std::pair<int, fs::path>>> sessions; // base -> {generation, file}
    for (fs::directory_iterator it(logs, error), end; !error && it != end; it.increment(error))
    {
        std::error_code type;
        if (!it->is_regular_file(type))
            continue;
        if (const auto name = detail::parseLogName(it->path().filename().string()))
            sessions[name->first].emplace_back(name->second, it->path());
    }
    if (error)
    {
        failures.push_back("cannot list " + logs.string() + ": " + error.message());
        return failures;
    }
    for (auto &[base, files] : sessions)
    {
        // Oldest first, so that no file is moved onto one that has not moved yet.
        std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        for (const auto &[generation, file] : files)
        {
            if (generation >= kKeptSessions)
            {
                if (!fs::remove(file, error) && error)
                    failures.push_back("cannot remove the oldest session's " + file.string() + ": " + error.message());
                continue;
            }
            const auto target = logs / sessionLogName(base, generation + 1);
            fs::rename(file, target, error);
            if (error)
                failures.push_back("cannot keep " + file.string() + " as " + target.string() + ": " +
                                   error.message() + (generation == 0 ? "; this session is lost when the log opens" : ""));
        }
    }
    return failures;
}

// Local time as the engine's log lines show it ("2026-11-13 18:49:54").
inline std::string logTime(std::time_t time)
{
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char text[32];
    return std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local) ? text : "?";
}

// The running session's marker (kRunningMarker): which session runs, when its game thread last showed it was alive
// (heartbeat) and whether the app went to the background. At the next start it tells a clean end from an unclean
// one (uncleanEnd). The game thread beats; the platform's lifecycle events (the SDL event watch, on the Java thread
// on Android) mark the background and the foreground without waiting for the game thread, which may be stuck.
// Every write replaces the file whole (a temporary renamed over it). Thread-safe.
inline constexpr std::time_t kHeartbeatSeconds = 2;
// A heartbeat older than this when the app went to the background: the game thread was stuck already.
inline constexpr std::time_t kStuckSeconds = 15;

class SessionMarker
{
  public:
    SessionMarker(std::filesystem::path logs, std::string started, long pid)
        : logs_(std::move(logs)), started_(std::move(started)), pid_(pid)
    {
    }

    // Each returns "" or what failed.
    std::string Start(std::time_t now)
    {
        std::lock_guard lock(mutex_);
        active_ = true;
        heartbeat_ = now;
        background_ = 0;
        return Write();
    }
    // The game thread, every frame: written every kHeartbeatSeconds.
    std::string Beat(std::time_t now)
    {
        std::lock_guard lock(mutex_);
        if (!active_ || now - heartbeat_ < kHeartbeatSeconds)
            return {};
        heartbeat_ = now;
        return Write();
    }
    // The app goes to the background or the OS ends it: a kill from here on is a regular stop.
    std::string Background(std::time_t now)
    {
        std::lock_guard lock(mutex_);
        if (!active_)
            return {};
        background_ = now;
        return Write();
    }
    // Back in the foreground: a kill is unclean again.
    std::string Foreground(std::time_t now)
    {
        std::lock_guard lock(mutex_);
        if (!active_)
            return {};
        background_ = 0;
        heartbeat_ = now;
        return Write();
    }
    // A clean exit.
    std::string Clear()
    {
        std::lock_guard lock(mutex_);
        active_ = false;
        std::error_code error;
        if (!std::filesystem::remove(logs_ / kRunningMarker, error) && error)
            return "cannot remove " + (logs_ / kRunningMarker).string() + ": " + error.message();
        return {};
    }

  private:
    std::string Write() const
    {
        std::error_code error;
        std::filesystem::create_directories(logs_, error);
        const auto path = logs_ / kRunningMarker, temporary = logs_ / (std::string(kRunningMarker) + ".new");
        {
            std::ofstream marker(temporary, std::ios::binary | std::ios::trunc);
            marker << "started " << started_ << ", pid " << pid_ << "\nheartbeat " << heartbeat_ << '\n';
            if (background_)
                marker << "background " << background_ << '\n';
            marker.flush();
            if (!marker)
                return "cannot write " + temporary.string();
        }
        std::filesystem::rename(temporary, path, error);
        return error ? "cannot replace " + path.string() + ": " + error.message() : std::string();
    }

    std::mutex mutex_;
    const std::filesystem::path logs_;
    const std::string started_;
    const long pid_;
    bool active_ = false;
    std::time_t heartbeat_ = 0, background_ = 0;
};

// The decision for a marker the previous session left ("" when there was none): "" for a clean end (it went to the
// background with its game thread alive: the system or the player ended it there), else how it ended uncleanly.
inline std::string uncleanEnd(const std::string &marker)
{
    if (marker.empty())
        return {};
    std::string session;
    long long heartbeat = -1, background = -1;
    std::istringstream lines(marker);
    for (std::string line; std::getline(lines, line);)
    {
        if (line.rfind("started ", 0) == 0)
            session = line;
        else if (line.rfind("heartbeat ", 0) == 0)
            heartbeat = std::atoll(line.c_str() + 10);
        else if (line.rfind("background ", 0) == 0)
            background = std::atoll(line.c_str() + 11);
    }
    if (session.empty())
        session = "an unreadable marker";
    const auto time = [](long long at) { return logTime(static_cast<std::time_t>(at)); };
    if (background < 0)
        return session + "; ended in the foreground (a hang, a crash or a kill)" +
               (heartbeat >= 0 ? "; game thread last alive " + time(heartbeat) : std::string());
    if (heartbeat < 0 || background - heartbeat > kStuckSeconds)
        return session + "; its game thread was stuck when it went to the background at " + time(background) +
               (heartbeat >= 0 ? " (last alive " + time(heartbeat) + ")" : std::string(" (never alive)"));
    return {};
}

// At start, before the rotation: the previous session's marker, decided (uncleanEnd) and consumed. Unclean: the
// record with the last line of its system.log; "" for a clean end or none.
inline std::string takeUncleanEnd(const std::filesystem::path &logs, std::vector<std::string> &failures)
{
    const auto path = logs / kRunningMarker;
    std::string marker;
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return {};
        marker.assign(std::istreambuf_iterator<char>(in), {});
    }
    std::error_code error;
    if (!std::filesystem::remove(path, error) && error)
        failures.push_back("cannot remove " + path.string() + ": " + error.message());
    if (marker.empty())
        marker = "started ?, an empty marker";
    const std::string unclean = uncleanEnd(marker);
    if (unclean.empty())
        return {};
    const std::string line = detail::lastLine(logs / sessionLogName("system", 0));
    return unclean + "; its last log line: " + (line.empty() ? "(none)" : line);
}

} // namespace storm::logging
