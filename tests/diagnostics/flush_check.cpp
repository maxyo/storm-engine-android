// Host check: logger output reaches the file without waiting for the periodic flush
// (warnings and errors at once; everything on a fatal signal, std::terminate or shutdown).
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

#include "fs.h"
#include "lifecycle_diagnostics_service.hpp"
#include "logging.hpp"

namespace
{
std::string readLog(const char *name)
{
    std::ifstream file(fs::GetLogsPath() / (std::string(name) + ".log"));
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

void expect(const bool condition, const std::string &what)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        std::exit(1);
    }
    std::printf("PASS: %s\n", what.c_str());
}

// Runs the scenario in a child so that the parent can read the file the child left.
template <typename F> int inChild(F scenario)
{
    const pid_t pid = fork();
    if (pid == 0)
    {
        storm::diag::LifecycleDiagnosticsService service;
        const auto guard = service.initialize(false);
        scenario();
        std::_Exit(0); // no shutdown flush: only what the scenario itself guaranteed
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return status;
}
} // namespace

int main()
{
    // Warnings and errors are flushed as they are logged; the trace before them goes too.
    inChild([] {
        auto log = storm::logging::getOrCreateLogger("compile_warn");
        log->trace("trace line before the warning");
        log->warn("warning line");
        std::_Exit(0);
    });
    const auto warned = readLog("compile_warn");
    expect(warned.find("warning line") != std::string::npos, "warning written without a periodic flush");
    expect(warned.find("trace line before the warning") != std::string::npos, "earlier trace written with it");

    // A crash writes the buffered trace out and still dies from the signal.
    auto status = inChild([] {
        storm::logging::getOrCreateLogger("compile_segv")->trace("trace line before a crash");
        std::raise(SIGSEGV);
    });
    expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV, "fatal signal still terminates the process");
    expect(readLog("compile_segv").find("trace line before a crash") != std::string::npos,
           "trace written on a fatal signal");

    status = inChild([] {
        storm::logging::getOrCreateLogger("compile_fault")->trace("trace line before a fault");
        *static_cast<volatile int *>(nullptr) = 1;
    });
    expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV, "invalid access still terminates with SIGSEGV");
    expect(readLog("compile_fault").find("trace line before a fault") != std::string::npos,
           "trace written on an invalid memory access");

    status = inChild([] {
        storm::logging::getOrCreateLogger("compile_terminate")->trace("trace line before terminate");
        std::terminate();
    });
    expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT, "std::terminate still aborts");
    expect(readLog("compile_terminate").find("trace line before terminate") != std::string::npos,
           "trace written on std::terminate");

    // Orderly shutdown (the guard going out of scope) writes everything.
    inChild([] {
        storm::diag::LifecycleDiagnosticsService service;
        {
            const auto guard = service.initialize(false);
            storm::logging::getOrCreateLogger("compile_exit")->trace("trace line before shutdown");
        }
        std::_Exit(0);
    });
    expect(readLog("compile_exit").find("trace line before shutdown") != std::string::npos,
           "trace written at shutdown");
    return 0;
}
