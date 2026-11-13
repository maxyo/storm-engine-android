// Host check of the grass vertices (src/libs/location/src/grass_pack.hpp) on synthetic grass maps, no game data:
// a map wider and deeper than 1023 blocks (the 15-bit places of a vertex count from the window, not the map), maps at
// the window's edges and a map with a corrupt minimap (refused like Grass::LoadData refuses it). The maps are written
// as .grs files and checked by tools/game_checks/grass_pack/grass_maps.hpp, the check of every map of a game.
#include "grass_maps.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>

using namespace grass_maps;

namespace
{

void expect(bool condition, const char *what)
{
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition)
        std::exit(1);
}

// A map of miniX x miniZ blocks with `blades` blades in every third block (the steps, heights and data vary).
fs::path write_map(const fs::path &directory, const char *name, int32_t miniX, int32_t miniZ, int32_t blades,
                   bool corrupt = false)
{
    std::vector<MiniMapElement> map(static_cast<size_t>(miniX) * miniZ);
    std::vector<MapElement> elements;
    for (size_t i = 0; i < map.size(); ++i)
    {
        const int32_t count = i % 3 == 0 ? blades : 0;
        map[i] = {static_cast<int32_t>(elements.size()), {count, count * 2 / 3, count / 2, count / 4}, 0.0f, 1.0f};
        for (int32_t b = 0; b < count; ++b)
        {
            MapElement e{};
            e.x = static_cast<unsigned char>((i * 7 + b * 5) % 32);
            e.z = static_cast<unsigned char>((i * 3 + b * 11) % 32);
            e.w = b % 16, e.h = (b * 3) % 16, e.frame = (b * 5) % 16, e.ang = (b * 7) % 16;
            e.y = -12.5f + static_cast<float>((i * 13 + b) % 1000) * 0.0371f;
            elements.push_back(e);
        }
    }
    if (corrupt)
        map[map.size() / 2].start += 1;
    const Header hdr{kGrassId, kGrassVer, miniX, miniZ, -646.58f, -208.51f, static_cast<int32_t>(elements.size())};
    const fs::path file = directory / name;
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char *>(&hdr), sizeof(hdr));
    out.write(reinterpret_cast<const char *>(map.data()), static_cast<std::streamsize>(map.size() * sizeof(map[0])));
    out.write(reinterpret_cast<const char *>(elements.data()),
              static_cast<std::streamsize>(elements.size() * sizeof(elements[0])));
    return file;
}

} // namespace

int main()
{
    const fs::path directory = fs::temp_directory_path() / "storm-grass-pack-check";
    fs::create_directories(directory);
    Result result;
    expect(check_map(write_map(directory, "wide.grs", 1300, 40, 24), result) && failures == 0,
           "a map 1300 blocks wide packs (the places count from the window)");
    expect(check_map(write_map(directory, "deep.grs", 30, 1500, 24), result) && failures == 0,
           "a map 1500 blocks deep packs");
    expect(check_map(write_map(directory, "small.grs", 5, 7, 3), result) && failures == 0,
           "a map smaller than a window packs");
    expect(!check_map(write_map(directory, "corrupt.grs", 50, 50, 4, true), result),
           "a corrupt minimap is refused as LoadData refuses it");
    // Windows: clipped to the map, never wider than the 15-bit places allow
    const Window edge = around(0, 1499, kReach, kWindowMargin, 30, 1500);
    expect(edge.x0 == 0 && edge.x1 == 30 && edge.z1 == 1500 && edge.z0 == 1499 - kReach - kWindowMargin,
           "a window at the map's edge is clipped to it");
    const Window widest = around(5000, 5000, kMaxReach, kWindowMargin, 10000, 10000);
    expect(widest.width() <= kMaxWindowBlocks && widest.z1 - widest.z0 <= kMaxWindowBlocks,
           "the widest window fits the 15-bit places");
    const Window need = around(600, 20, kReach, 0, 1300, 40);
    expect(around(600 + kWindowMargin, 20, kReach, kWindowMargin, 1300, 40).contains(need) &&
               !around(600 + kWindowMargin + 1, 20, kReach, kWindowMargin, 1300, 40).contains(need),
           "the camera moves kWindowMargin blocks before the window is rebuilt");
    fs::remove_all(directory);
    std::printf("PASS: grass vertices on synthetic maps\n");
    return 0;
}
