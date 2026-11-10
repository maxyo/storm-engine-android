// Host tool for script patch layers (src/libs/util/include/platform/script_patches.hpp),
// with the engine's own parser and applier, so a layer is checked by the code
// that loads it.
//
//   script_patch_tool check <layer>
//       Parses every set; exit 1 if the layer is damaged.
//   script_patch_tool apply <layer> <game dir> <out dir>
//       Applies the layer to a game directory as the engine does at start-up,
//       writing derived files below <out dir>. Prints the report. Exit 0 when
//       every fix of the matching set applied, 2 when some did not or no set
//       identifies the game, 1 if the layer is damaged.
//
// Build: c++ -std=c++20 -I src/libs/util/include tools/script_patches/script_patch_tool.cpp
#include "platform/script_patches.hpp"

#include <cstdio>
#include <iostream>

namespace fs = std::filesystem;
using namespace storm::script_patches;

int main(int argc, char **argv)
{
    const std::string command = argc > 1 ? argv[1] : "";
    try
    {
        if (command == "check" && argc == 3)
        {
            for (const auto &set : loadLayer(argv[2]))
                std::cout << "set " << set.id << " (" << set.game << " " << set.version << "): "
                          << set.targets.size() << " target(s), " << set.fixes.size() << " fix(es)\n";
            return 0;
        }
        if (command == "apply" && argc == 5)
        {
            const fs::path game(argv[3]);
            std::unordered_map<std::string, std::string> paths;
            for (const auto &entry : fs::recursive_directory_iterator(game))
                if (entry.is_regular_file())
                    paths[storm::android::resourceKey(entry.path().lexically_relative(game))] = entry.path().string();
            const auto report = applyLayer(argv[2], argv[4], paths);
            for (const auto &line : report.info)
                std::cout << line << "\n";
            for (const auto &line : report.errors)
                std::cout << "ERROR " << line << "\n";
            for (const auto &fix : report.unapplied)
                for (const auto &reason : fix.reasons)
                    std::cout << "UNAPPLIED " << fix.set << "/" << fix.fix << ": " << reason << "\n";
            return report.set.empty() || !report.unapplied.empty() ? 2 : 0;
        }
    }
    catch (const LayerError &error)
    {
        std::cerr << "damaged script patch layer: " << error.what() << "\n";
        return 1;
    }
    std::cerr << "usage: script_patch_tool check <layer> | apply <layer> <game dir> <out dir>\n";
    return 64;
}
