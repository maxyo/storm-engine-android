// Host check of the engine's loggers (src/libs/diagnostics/src/logging.cpp): every logger's errors also land in
// error.log, with the logger that wrote them; lines below error stay in their own log.
#include "logging.hpp"

#include <SDL2/SDL.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

// fs::GetStashPath asks SDL for the per-user directory; the check gives it the work directory.
static std::string stash;
extern "C" char *SDL_GetPrefPath(const char *, const char *)
{
    return strdup(stash.c_str());
}
extern "C" const char *SDL_GetError()
{
    return "stub";
}
extern "C" void SDL_free(void *memory)
{
    std::free(memory);
}

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

std::string read(const std::string &file)
{
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

size_t count(const std::string &text, const std::string &part)
{
    size_t n = 0;
    for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1))
        ++n;
    return n;
}
} // namespace

int main(int argc, char **argv)
{
    check(argc == 2, "usage: check <empty work directory>");
    stash = std::string(argv[1]) + "/";
    using storm::logging::getOrCreateLogger;
    auto system = getOrCreateLogger("system");
    auto compile = getOrCreateLogger("compile");
    auto errors = getOrCreateLogger("error");
    system->info("system info line");
    system->error("system error line");
    compile->warn("compile warning line");
    compile->critical("compile critical line");
    errors->info("script error line");
    spdlog::apply_all([](const std::shared_ptr<spdlog::logger> &logger) { logger->flush(); });

    const std::string logs = stash + "Logs/";
    const auto systemLog = read(logs + "system.log"), compileLog = read(logs + "compile.log"),
               errorLog = read(logs + "error.log");
    check(count(systemLog, "system info line") == 1 && count(systemLog, "system error line") == 1, "system.log has its lines");
    check(count(compileLog, "compile critical line") == 1, "compile.log has its lines");
    check(count(errorLog, "[system] [error] system error line") == 1, "an error of another logger is in error.log, with its logger");
    check(count(errorLog, "[compile] [critical] compile critical line") == 1, "critical lines too");
    check(count(errorLog, "script error line") == 1, "error.log keeps its own lines once");
    check(errorLog.find("info line") == std::string::npos && errorLog.find("warning line") == std::string::npos,
          "lines below error stay in their own log");
    std::puts("PASS: every logger's errors are in error.log, once, with their logger");
    return 0;
}
