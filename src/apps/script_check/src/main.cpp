// script_check: compile a game's whole script tree on the host and report every diagnostic.
//
//   script_check check <game dir> [--program <dir>] [--main <file>] [--dump <dir>] [--warnings]
//                [--language <name>]   (for %language% in include paths, default english)
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
#include "game_profile.hpp"
#include "s_import_func.h"
#include "script_libriary.h"
#include "v_s_stack.h"
#include "vma.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// "script_check_test_library" for the script tests: one native the tests declare and one they do not
namespace
{
uint32_t ScriptCheckAddOne(VS_STACK *stack)
{
    auto *value = static_cast<VDATA *>(stack->Pop());
    const int32_t result = value->GetInt() + 1;
    static_cast<VDATA *>(stack->Push())->Set(result);
    return IFUNCRESULT_OK;
}

uint32_t ScriptCheckUnused(VS_STACK *stack)
{
    static_cast<VDATA *>(stack->Push())->Set(int32_t{0});
    return IFUNCRESULT_OK;
}
} // namespace

class script_check_test_library : public SCRIPT_LIBRIARY
{
  public:
    bool Init() override
    {
        IFUNCINFO info;
        info.pFuncName = "ScriptCheckAddOne";
        info.pReturnValueName = "int";
        info.pFuncAddress = ScriptCheckAddOne;
        info.nArguments = 1;
        core.SetScriptFunction(&info);
        info.pFuncName = "ScriptCheckUnused";
        info.pFuncAddress = ScriptCheckUnused;
        info.nArguments = 0;
        core.SetScriptFunction(&info);
        return true;
    }
};

CREATE_SCRIPTLIBRIARY(script_check_test_library)

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
                         "[--warnings] [--language <name>]\n       script_check test <script>...\n");
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
    std::string language = "english"; // %language% in include paths (CL)
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
        else if (arg == "--language" && i + 1 < argc)
            language = argv[++i];
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
    storm::initGameProfile(); // built-in signatures differ between the games
    std::vector<std::string> diagnostics;
    COMPILER compiler;
    core_internal.Compiler = &compiler;
    compiler.ConfigureCompileCheck(&diagnostics, false);
    compiler.SetScriptLanguage(language);
    compiler.SetProgramDirectory(program_name.c_str());

    const auto started = std::chrono::steady_clock::now();
    size_t errors = 0, failed = 0;
    std::set<std::string> missing_natives;
    bool dumping = true; // segments are dumped while they are loaded
    auto report = [&](const std::string &segment, bool ok) {
        for (const auto &line : diagnostics)
        {
            if (isError(line))
                ++errors;
            if (line.starts_with("MISSING NATIVE"))
                missing_natives.insert(line.substr(line.rfind(": ") + 2));
            if (warnings || isError(line) || line.starts_with("MISSING NATIVE"))
                std::printf("%s\n", line.c_str());
        }
        diagnostics.clear();
        if (!ok)
        {
            ++failed;
            std::printf("FAILED segment %s\n", segment.c_str());
        }
        if (!dump.empty() && ok && dumping)
        {
            uint32_t size = 0;
            if (const char *code = compiler.GetSegmentCode(segment.c_str(), size))
            {
                auto name = segment;
                std::ranges::replace(name, '\\', '_');
                fs::create_directories(dump);
                std::ofstream(dump / (lower(name) + ".b"), std::ios::binary).write(code, size);
                // attribute and function names of the ACCESS_WORD_CODE and CALL_FUNCTION tokens, in
                // order: their codes depend on the order of compilation
                std::ofstream words(dump / (lower(name) + ".aw"));
                for (uint32_t ip = 0; ip < size;)
                {
                    const auto type = static_cast<uint8_t>(code[ip++]);
                    uint32_t data_size = static_cast<uint8_t>(code[ip++]);
                    if (data_size == 0xff)
                    {
                        std::memcpy(&data_size, code + ip, sizeof(uint32_t));
                        ip += sizeof(uint32_t);
                    }
                    if ((type == ACCESS_WORD_CODE || type == CALL_FUNCTION) && data_size == sizeof(uint32_t))
                    {
                        uint32_t word;
                        std::memcpy(&word, code + ip, sizeof(uint32_t));
                        words << (type == ACCESS_WORD_CODE ? compiler.GetAccessWord(word)
                                                           : compiler.GetFunctionName(word))
                              << '\n';
                    }
                    ip += data_size;
                }
            }
        }
    };

    const bool main_ok = compiler.BC_LoadSegment(main_name.c_str());
    report(main_name, main_ok);

    // Every other segment on its own (the game loads and unloads them); its diagnostics are held
    // back while a retry may still explain them.
    std::map<std::string, std::string> provider; // lowercase symbol -> segment defining it
    std::map<std::string, std::vector<std::string>> held;
    std::vector<std::string> failed_segments;
    for (const auto &segment : segments)
    {
        diagnostics.clear();
        const bool ok = compiler.BC_LoadSegment(segment.c_str());
        if (ok && !dump.empty())
        {
            auto pending = diagnostics; // report() dumps and clears; keep the diagnostics for later
            diagnostics.clear();
            report(segment, true);
            diagnostics = std::move(pending);
        }
        for (const auto &name : compiler.GetSegmentSymbols(segment.c_str()))
            provider.emplace(lower(name), segment);
        compiler.UnloadSegment(segment.c_str());
        compiler.ProcessFrame(0);
        held[segment] = diagnostics;
        if (!ok)
            failed_segments.push_back(segment);
    }
    // A segment using names of another segment (loaded together in the game, e.g. the story frame
    // interface and its nodes) is compiled again with those segments loaded first.
    const std::regex unknown_name(R"re((?:Undeclared identifier: |unexpected ')([A-Za-z_]\w*))re");
    for (const auto &segment : failed_segments)
    {
        std::set<std::string> needed;
        for (const auto &line : held[segment])
            for (std::sregex_iterator it(line.begin(), line.end(), unknown_name), end; it != end; ++it)
                if (auto found = provider.find(lower((*it)[1].str())); found != provider.end() && found->second != segment)
                    needed.insert(found->second);
        if (needed.empty())
            continue;
        diagnostics.clear();
        for (const auto &with : needed)
            compiler.BC_LoadSegment(with.c_str());
        diagnostics.clear();
        compiler.BC_LoadSegment(segment.c_str());
        auto retried = diagnostics;
        compiler.UnloadSegment(segment.c_str());
        for (const auto &with : needed)
            compiler.UnloadSegment(with.c_str());
        compiler.ProcessFrame(0);
        std::string list;
        for (const auto &with : needed)
            list += (list.empty() ? "" : ", ") + with;
        retried.insert(retried.begin(), "NOTE: " + segment + " compiled with " + list + " loaded");
        held[segment] = std::move(retried);
    }
    dumping = false;
    for (const auto &segment : segments)
    {
        diagnostics = held[segment];
        const bool ok = !std::ranges::any_of(diagnostics, isError);
        if (warnings)
            for (const auto &line : diagnostics)
                if (line.starts_with("NOTE: "))
                    std::printf("%s\n", line.c_str());
        report(segment, ok);
    }
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    std::printf("script_check: %zu natives declared by the scripts are not implemented:", missing_natives.size());
    for (const auto &name : missing_natives)
        std::printf(" %s", name.c_str());
    std::printf("\nscript_check: %zu segments, %zu failed, %zu errors, %lld ms\n", segments.size() + 1, failed, errors,
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
        compiler.ConfigureCompileCheck(&diagnostics, true);
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
    for (auto *c : __STORM_CLASSES_REGISTRY) // as CoreImpl::Init: FindVMA looks classes up by hash
        c->SetHash(MakeHashValue(c->GetName()));
    const std::string mode = argv[1];
    if (mode == "check")
        return check(argc - 2, argv + 2);
    if (mode == "test")
        return test(argc - 2, argv + 2);
    return usage();
}
