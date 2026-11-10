// script_check: compile a game's whole script tree on the host and report every diagnostic.
//
//   script_check check <game dir> [--program <dir>] [--main <file>] [--dump <dir>] [--warnings]
//   script_check test <script>...
//
// check: compiles the main segment (seadogs.c) and then every other .c file under the program
// directory that no script includes, each as its own segment the way LoadSegment loads it, and
// unloads it again (dialog files all define the same functions). Prints every error with
// file(line) and a summary; exit code 1 if any error. --dump writes each segment's bytecode,
// used to prove that a compiler change leaves an existing game's bytecode unchanged.
//
// test: compiles each script as the main segment and runs every function whose name starts with
// "test_"; it must return 1 (int) without a runtime error. A script whose first line is
// "// expect-error: <text>" must instead fail to compile with an error containing <text>.
#include "compiler.h"
#include "core_impl.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
std::string lower(std::string s)
{
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// game paths use backslashes and any case
std::string key(std::string s)
{
    std::ranges::replace(s, '/', '\\');
    return lower(s);
}

std::string readText(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool isError(const std::string &line)
{
    return line.find("ERROR") != std::string::npos;
}

int usage()
{
    std::fprintf(stderr, "usage: script_check check <game dir> [--program <dir>] [--main <file>] [--dump <dir>] "
                         "[--warnings]\n       script_check test <script>...\n");
    return 2;
}

int check(int argc, char **argv)
{
    if (argc < 1)
        return usage();
    const fs::path game = argv[0];
    std::string program_name, main_name = "seadogs.c";
    fs::path dump;
    bool warnings = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--program" && i + 1 < argc)
            program_name = argv[++i];
        else if (arg == "--main" && i + 1 < argc)
            main_name = argv[++i];
        else if (arg == "--dump" && i + 1 < argc)
            dump = argv[++i];
        else if (arg == "--warnings")
            warnings = true;
        else
            return usage();
    }
    if (program_name.empty())
    {
        for (const auto &entry : fs::directory_iterator(game))
            if (entry.is_directory() && lower(entry.path().filename().string()) == "program")
                program_name = entry.path().filename().string();
        if (program_name.empty())
        {
            std::fprintf(stderr, "no Program directory in %s\n", game.string().c_str());
            return 2;
        }
    }
    const fs::path program = game / program_name;

    // segments: every .c file that no script includes
    std::set<std::string> included;
    std::map<std::string, std::string> sources; // key -> path relative to the program directory
    const std::regex include(R"re(^\s*#include\s+"([^"]+)")re", std::regex::multiline);
    for (const auto &entry : fs::recursive_directory_iterator(program))
    {
        if (!entry.is_regular_file())
            continue;
        const auto ext = lower(entry.path().extension().string());
        if (ext != ".c" && ext != ".h")
            continue;
        const auto text = readText(entry.path());
        for (std::sregex_iterator it(text.begin(), text.end(), include), end; it != end; ++it)
            included.insert(key((*it)[1].str()));
        if (ext == ".c")
        {
            auto relative = fs::relative(entry.path(), program).string();
            std::ranges::replace(relative, '/', '\\');
            sources.emplace(key(relative), relative);
        }
    }
    std::vector<std::string> segments;
    for (const auto &[k, relative] : sources)
        if (!included.contains(k) && k != key(main_name))
            segments.push_back(relative);

    fs::current_path(game);
    std::printf("script_check: indexing %s without the engine-shaders overlay (scripts only)\n",
                fs::current_path().string().c_str());
    fio->ScanResourcePaths(false);
    std::vector<std::string> diagnostics;
    COMPILER compiler;
    core_internal.Compiler = &compiler;
    compiler.ConfigureCompileCheck(&diagnostics);
    compiler.SetProgramDirectory(program_name.c_str());

    const auto started = std::chrono::steady_clock::now();
    size_t errors = 0, failed = 0;
    auto report = [&](const std::string &segment, bool ok) {
        for (const auto &line : diagnostics)
        {
            if (isError(line))
                ++errors;
            if (warnings || isError(line))
                std::printf("%s\n", line.c_str());
        }
        diagnostics.clear();
        if (!ok)
        {
            ++failed;
            std::printf("FAILED segment %s\n", segment.c_str());
        }
        if (!dump.empty() && ok)
        {
            uint32_t size = 0;
            if (const char *code = compiler.GetSegmentCode(segment.c_str(), size))
            {
                auto name = segment;
                std::ranges::replace(name, '\\', '_');
                fs::create_directories(dump);
                std::ofstream(dump / (lower(name) + ".b"), std::ios::binary).write(code, size);
            }
        }
    };

    const bool main_ok = compiler.BC_LoadSegment(main_name.c_str());
    report(main_name, main_ok);
    for (const auto &segment : segments)
    {
        const bool ok = compiler.BC_LoadSegment(segment.c_str());
        report(segment, ok);
        compiler.UnloadSegment(segment.c_str());
        compiler.ProcessFrame(0);
    }
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    std::printf("script_check: %zu segments, %zu failed, %zu errors, %lld ms\n", segments.size() + 1, failed, errors,
                static_cast<long long>(ms));
    std::fflush(stdout);
    compiler.Release();
    core_internal.Compiler = nullptr;
    return errors ? 1 : 0;
}

int test(int argc, char **argv)
{
    if (argc < 1)
        return usage();
    int failures = 0;
    for (int i = 0; i < argc; ++i)
    {
        const fs::path script = fs::absolute(argv[i]);
        const auto text = readText(script);
        std::string expected;
        if (text.starts_with("// expect-error:"))
        {
            expected = text.substr(16, text.find('\n') - 16);
            expected.erase(0, expected.find_first_not_of(' '));
            expected.erase(expected.find_last_not_of(" \r") + 1);
        }

        std::vector<std::string> diagnostics;
        COMPILER compiler;
        core_internal.Compiler = &compiler;
        compiler.ConfigureCompileCheck(&diagnostics);
        fs::current_path(script.parent_path());
        fio->ScanResourcePaths(false); // scripts only, no engine-shaders overlay
        compiler.SetProgramDirectory(".");
        const auto name = script.filename().string();
        const bool compiled = compiler.BC_LoadSegment(name.c_str());

        auto dump = [&] {
            for (const auto &line : diagnostics)
                std::printf("    %s\n", line.c_str());
        };
        if (!expected.empty())
        {
            const bool found = std::ranges::any_of(
                diagnostics, [&](const auto &line) { return isError(line) && line.find(expected) != std::string::npos; });
            if (compiled || !found)
            {
                ++failures;
                std::printf("FAIL %s: expected compile error '%s'\n", name.c_str(), expected.c_str());
                dump();
            }
            else
                std::printf("PASS %s (compile error)\n", name.c_str());
        }
        else if (!compiled)
        {
            ++failures;
            std::printf("FAIL %s: does not compile\n", name.c_str());
            dump();
        }
        else
        {
            const std::regex test_function(R"(^\s*int\s+(test_\w+)\s*\(\s*\))", std::regex::multiline);
            int count = 0;
            for (std::sregex_iterator it(text.begin(), text.end(), test_function), end; it != end; ++it)
            {
                const auto function = (*it)[1].str();
                diagnostics.clear();
                std::string value;
                const bool ran = compiler.RunScriptFunction(function.c_str(), value);
                const bool errors = std::ranges::any_of(diagnostics, isError);
                ++count;
                if (!ran || errors || value != "1")
                {
                    ++failures;
                    std::printf("FAIL %s: %s returned '%s'\n", name.c_str(), function.c_str(), value.c_str());
                    dump();
                }
            }
            if (count == 0)
            {
                ++failures;
                std::printf("FAIL %s: no test_ functions\n", name.c_str());
            }
            else
                std::printf("PASS %s (%d functions)\n", name.c_str(), count);
        }
        compiler.Release();
        core_internal.Compiler = nullptr;
    }
    std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    std::fflush(stdout);
    return failures ? 1 : 0;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
        return usage();
    const std::string mode = argv[1];
    if (mode == "check")
        return check(argc - 2, argv + 2);
    if (mode == "test")
        return test(argc - 2, argv + 2);
    return usage();
}
