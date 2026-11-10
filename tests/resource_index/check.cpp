#include "platform/resource_index.hpp"

#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

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

// The original two walks: relative keys from the working directory, absolute keys below `root`.
static Map reference(const std::string &root)
{
    Map map;
    for (const auto &entry : fs::recursive_directory_iterator("."))
    {
        if (!indexable(entry))
            continue;
        auto path = entry.path().string();
        if (path.rfind("./", 0) == 0)
            path.erase(0, 2);
        add(map, path, "");
    }
    const auto prefix = lowered(root);
    for (const auto &entry : fs::recursive_directory_iterator(root))
        if (indexable(entry))
            add(map, entry.path().string(), prefix);
    return map;
}

template <typename Operation> static bool throws(Operation operation)
{
    try
    {
        operation();
    }
    catch (const std::runtime_error &)
    {
        return true;
    }
    return false;
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

    // The working directory is the game directory: one walk, identical index.
    fs::current_path(game);
    Map single;
    scan(single, root);
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
    scan(linked, linkRoot);
    assert(linked == reference(linkRoot));

    // Any other root is an error, never a partial index.
    const fs::path elsewhere = fs::path(argv[1]) / "elsewhere";
    touch(elsewhere / "resource/local.txt");
    fs::current_path(elsewhere);
    Map wrong;
    assert(throws([&] { scan(wrong, root); }));
    fs::current_path(game);
    assert(throws([&] { scan(wrong, game.string()); })); // no trailing separator

    if (argc >= 3)
    {
        const std::string real = std::string(argv[2]) + "/";
        fs::current_path(argv[2]);
        Map before, after;
        const double twoWalks = ms([&] { before = reference(real); });
        const double oneWalk = ms([&] { scan(after, real); });
        assert(before == after);
        std::cout << "real tree: " << after.size() << " keys, two walks " << twoWalks << " ms, one walk " << oneWalk
                  << " ms (warm cache)\n";
    }
    std::cout << "resource index: single walk equals two-walk reference PASS\n";
}
