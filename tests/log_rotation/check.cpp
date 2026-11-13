// Host check of storm::logging::rotatePreviousSessionLogs (src/libs/diagnostics/include/log_rotation.hpp).
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
    using storm::logging::rotatePreviousSessionLogs;

    check(rotatePreviousSessionLogs(logs).empty(), "a missing directory is a first start");
    check(!fs::exists(logs), "nothing is created");

    fs::create_directories(logs);
    // Session 1 wrote these; session 2 starts.
    write(logs / "system.log", "session 1 system\n");
    write(logs / "error.log", "session 1 error\n");
    write(logs / "ff_keys.json", "{}");
    check(rotatePreviousSessionLogs(logs).empty(), "first rotation");
    check(read(logs / "system.prev.log") == "session 1 system\n", "system.log kept as system.prev.log");
    check(read(logs / "error.prev.log") == "session 1 error\n", "error.log kept as error.prev.log");
    check(!fs::exists(logs / "system.log") && !fs::exists(logs / "error.log"), "the current names are free");
    check(read(logs / "ff_keys.json") == "{}", "other files stay");

    // Session 2 wrote system.log and mimalloc.log, no error.log; session 3 starts.
    write(logs / "system.log", "session 2 system\n");
    write(logs / "mimalloc.log", "session 2 mimalloc\n");
    check(rotatePreviousSessionLogs(logs).empty(), "second rotation");
    check(read(logs / "system.prev.log") == "session 2 system\n", "the run before's .prev is replaced");
    check(read(logs / "mimalloc.prev.log") == "session 2 mimalloc\n", "a new log is kept");
    check(!fs::exists(logs / "error.prev.log"), "session 1's error.prev.log is not passed off as session 2's");
    check(!fs::exists(logs / "system.log") && !fs::exists(logs / "mimalloc.log"), "nothing current left");

    // A directory named like a log is not a log; a start without any log keeps nothing stale.
    fs::create_directories(logs / "odd.log");
    check(rotatePreviousSessionLogs(logs).empty(), "third rotation");
    check(fs::is_directory(logs / "odd.log") && !fs::exists(logs / "odd.prev.log"), "directories are skipped");
    check(!fs::exists(logs / "system.prev.log") && !fs::exists(logs / "mimalloc.prev.log"),
          "a session that wrote no logs leaves no older ones behind");

    // A failure is reported, not dropped: the target name is taken by a non-empty directory.
    write(logs / "compile.log", "session 4 compile\n");
    fs::create_directories(logs / "compile.prev.log" / "blocker");
    const auto failures = rotatePreviousSessionLogs(logs);
    check(failures.size() == 1 && failures[0].find("compile.log") != std::string::npos, "a failed rotation is reported");
    check(read(logs / "compile.log") == "session 4 compile\n", "a log that could not be rotated is untouched");

    std::puts("PASS: previous session logs rotated, older ones replaced, missing logs fine, failures reported");
    return 0;
}
