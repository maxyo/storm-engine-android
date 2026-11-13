// Every grass map of a game packs into the grass vertices (grass_maps.hpp): exit 1 on a map that does not.
//
//   check <game or resource directory>...
#include "grass_maps.hpp"

using namespace grass_maps;

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s <game or resource directory>...\n", argv[0]);
        return 2;
    }
    std::vector<fs::path> files;
    for (int i = 1; i < argc; ++i)
        for (const auto &entry : fs::recursive_directory_iterator(argv[i]))
            if (entry.is_regular_file() && is_grs(entry.path()))
                files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    long maps = 0, refused = 0, peak = 0, largest = 0;
    std::string peakMap;
    for (const fs::path &file : files)
    {
        Result result;
        if (!check_map(file, result))
        {
            ++refused;
            std::printf("     refused by LoadData (no grass in the game): %s\n", file.string().c_str());
            continue;
        }
        ++maps;
        largest = std::max(largest, result.blades);
        if (result.peak > peak)
            peak = result.peak, peakMap = file.filename().string();
    }
    std::printf("     %ld grass maps (%ld refused files): largest %ld blades (%.1f MB whole); peak window %ld blades "
                "= %.1f MB (%s)\n",
                maps, refused, largest, largest * 4.0 * sizeof(Vertex) / 1e6, peak, peak * 4.0 * sizeof(Vertex) / 1e6,
                peakMap.c_str());
    if (maps == 0)
    {
        std::printf("FAIL: no grass maps under the given directories\n");
        return 1;
    }
    std::printf("%s: every grass map packs into the grass vertices\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
