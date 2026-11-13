// Host check of the engine's session logs (src/libs/diagnostics/include/log_rotation.hpp): the last kKeptSessions
// sessions' logs are kept, each file with its session, and a session that ended without a clean exit is told.
#include "log_rotation.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace
{
void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}

void write(const fs::path &file, const std::string &text)
{
    std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
}

std::string read(const fs::path &file)
{
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
} // namespace

int main(int argc, char **argv)
{
    check(argc == 2, "usage: check <empty work directory>");
    const fs::path logs = fs::path(argv[1]) / "Logs";
    using namespace storm::logging;
    static_assert(kKeptSessions == 3);

    check(rotatePreviousSessionLogs(logs).empty(), "a missing directory is a first start");
    check(!fs::exists(logs), "nothing is created");
    fs::create_directories(logs);

    // Session 1 wrote system and error, session 2 only system, session 3 system and mimalloc, session 4 system.
    const auto session = [&logs](int n, std::initializer_list<const char *> bases) {
        for (const char *base : bases)
            write(logs / (std::string(base) + ".log"), "session " + std::to_string(n) + " " + base + "\n");
        check(rotatePreviousSessionLogs(logs).empty(), "a rotation");
    };
    write(logs / "ff_keys.json", "{}");
    session(1, {"system", "error"});
    check(read(logs / "system.prev1.log") == "session 1 system\n" && read(logs / "error.prev1.log") == "session 1 error\n",
          "the session before is .prev1");
    check(!fs::exists(logs / "system.log") && !fs::exists(logs / "error.log"), "the current names are free");
    session(2, {"system"});
    check(read(logs / "system.prev1.log") == "session 2 system\n" && read(logs / "system.prev2.log") == "session 1 system\n",
          "two sessions back is .prev2");
    check(!fs::exists(logs / "error.prev1.log") && read(logs / "error.prev2.log") == "session 1 error\n",
          "a log a session did not write leaves a gap: it is not passed off as that session's");
    session(3, {"system", "mimalloc"});
    check(read(logs / "system.prev3.log") == "session 1 system\n" && read(logs / "error.prev3.log") == "session 1 error\n" &&
              read(logs / "mimalloc.prev1.log") == "session 3 mimalloc\n",
          "three sessions are kept");
    session(4, {"system"});
    check(!fs::exists(logs / "system.prev4.log") && !fs::exists(logs / "error.prev3.log") &&
              !fs::exists(logs / "error.prev4.log"),
          "the session four back is removed, every file of it");
    check(read(logs / "system.prev1.log") == "session 4 system\n" && read(logs / "system.prev2.log") == "session 3 system\n" &&
              read(logs / "system.prev3.log") == "session 2 system\n" && read(logs / "mimalloc.prev2.log") == "session 3 mimalloc\n",
          "each file moved with its session");
    check(read(logs / "ff_keys.json") == "{}", "other files stay");

    // The one previous session engine 761488e0 kept (<name>.prev.log) is the session before.
    write(logs / "compile.prev.log", "legacy\n");
    write(logs / "compile.log", "current\n");
    check(rotatePreviousSessionLogs(logs).empty(), "a rotation with a legacy log");
    check(read(logs / "compile.prev1.log") == "current\n" && read(logs / "compile.prev2.log") == "legacy\n" &&
              !fs::exists(logs / "compile.prev.log"),
          "a legacy .prev.log is one session back");
    // Names that only look like a generation are logs of their own; directories are skipped.
    write(logs / "odd.prev0.log", "zero\n");
    fs::create_directories(logs / "dir.log");
    check(rotatePreviousSessionLogs(logs).empty(), "a rotation with odd names");
    check(read(logs / "odd.prev0.prev1.log") == "zero\n" && fs::is_directory(logs / "dir.log"), "odd names, directories");

    // A failure is reported, not dropped: the target name is taken by a non-empty directory.
    write(logs / "script_stack.log", "stack\n");
    fs::create_directories(logs / "script_stack.prev1.log" / "blocker");
    const auto failures = rotatePreviousSessionLogs(logs);
    check(failures.size() == 1 && failures[0].find("script_stack.log") != std::string::npos, "a failed rotation is reported");
    check(read(logs / "script_stack.log") == "stack\n", "a log that could not be rotated is untouched");

    // The running marker: a clean exit removes it; one found at the start is the previous session's unclean end.
    std::vector<std::string> problems;
    check(takeUncleanEnd(logs, problems).empty() && problems.empty(), "no marker: a clean exit");
    check(markSessionRunning(logs, "2026-11-13 18:30:01", 4242).empty(), "the marker is written");
    check(read(logs / kRunningMarker) == "started 2026-11-13 18:30:01, pid 4242\n", "the marker names the session");
    check(clearSessionRunning(logs).empty() && !fs::exists(logs / kRunningMarker), "a clean exit removes it");
    check(clearSessionRunning(logs).empty(), "removing it twice is fine (background, then exit)");
    check(markSessionRunning(logs, "2026-11-13 18:30:01", 4242).empty(), "marked again");
    write(logs / "system.log", "[18:30:01] start\n[18:41:07] [system] [info] last words\n\n");
    const auto unclean = takeUncleanEnd(logs, problems);
    check(unclean == "started 2026-11-13 18:30:01, pid 4242; its last log line: [18:41:07] [system] [info] last words",
          "the unclean end names the session and its last log line");
    check(problems.empty() && !fs::exists(logs / kRunningMarker), "the marker is consumed");
    check(read(logs / "system.log").find("last words") != std::string::npos, "the log is read before the rotation");

    std::puts("PASS: three sessions kept, files move with their session, failures reported, unclean ends told");
    return 0;
}
