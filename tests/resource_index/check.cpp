#include "platform/resource_index.hpp"

#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace storm::resource_index;

static void touch(const fs::path &path)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path) << "x";
}

template <typename Operation> static double ms(Operation operation)
{
    const auto begin = std::chrono::steady_clock::now();
    operation();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
}

static Map reference(const std::string &root)
{
    Map map;
    scanRelative(map);
    scanAbsolute(map, root);
    return map;
}

int main(int argc, char **argv)
{
    assert(argc >= 2);
    const fs::path game = fs::path(argv[1]) / "Game Dir";
    touch(game / "RESOURCE/Ini/Interfaces/Main.INI");
    touch(game / "RESOURCE/Textures/Ships/Sail.TX");
    touch(game / "Program/Seadogs.c");
    touch(game / "SAVE/Player/slot");
    touch(game / "engine.ini");
    touch(game / "Modules/readme.txt");      // not indexed
    touch(game / "Other/Deep/Options.Ini");  // indexed by extension
    touch(game / "resource-notes/Файл.txt"); // prefix match, non-ASCII bytes preserved
    fs::create_directories(game / "RESOURCE/Empty");
    const std::string root = game.string() + "/";

    // Working directory == game directory (Android): one walk, identical index.
    fs::current_path(game);
    Map single;
    assert(scan(single, root) == 1);
    const Map expected = reference(root);
    assert(single == expected);
    assert(single.at("resource/ini/interfaces/main.ini") == "RESOURCE/Ini/Interfaces/Main.INI");
    assert(single.at(lowered(root) + "resource/ini/interfaces/main.ini") == root + "RESOURCE/Ini/Interfaces/Main.INI");
    assert(single.at("resource/empty") == "RESOURCE/Empty");
    assert(single.at("other/deep/options.ini") == "Other/Deep/Options.Ini");
    assert(single.count("modules/readme.txt") == 0 && single.count("modules") == 0);
    assert(single.count("save/player/slot") == 1 && single.count("engine.ini") == 1);
    // A symlinked spelling of the same directory still takes the single walk.
    fs::create_directory_symlink(game, fs::path(argv[1]) / "link");
    Map linked;
    const std::string linkRoot = (fs::path(argv[1]) / "link").string() + "/";
    assert(scan(linked, linkRoot) == 1 && linked == reference(linkRoot));

    // Different working directory (desktop launch from elsewhere): two walks, as before.
    const fs::path elsewhere = fs::path(argv[1]) / "elsewhere";
    touch(elsewhere / "resource/local.txt");
    fs::current_path(elsewhere);
    Map twice;
    assert(scan(twice, root) == 2);
    assert(twice == reference(root));
    assert(twice.count("resource/local.txt") == 1 && twice.count("program/seadogs.c") == 0);
    // Root without a trailing separator is not eligible for the shortcut.
    fs::current_path(game);
    Map plain;
    assert(scan(plain, game.string()) == 2 && plain == reference(game.string()));

    if (argc >= 3)
    {
        const std::string real = std::string(argv[2]) + "/";
        fs::current_path(argv[2]);
        Map before, after;
        const double twoWalks = ms([&] { before = reference(real); });
        const double oneWalk = ms([&] { assert(scan(after, real) == 1); });
        assert(before == after);
        std::cout << "real tree: " << after.size() << " keys, two walks " << twoWalks << " ms, one walk " << oneWalk
                  << " ms (warm cache)\n";
    }
    std::cout << "resource index: single walk equals two-walk reference PASS\n";
}
