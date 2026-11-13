// Host check of the relief preparation with the real PBRify models (storm_relief_core): a small synthetic game,
// prepared (a stone texture's relief with its material, a bottle's own gloss map), marked complete, keyed as the game
// keys it, resumed without work, stale maps pruned, cancelled.
#include "relief.hpp"
#include "storm/fx/gloss_map.hpp"
#include "storm/fx/height_map.hpp"
#include "storm/fx/relief_store.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
int failures = 0;
#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);                                        \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (false)

// An A8R8G8B8 .tx with all mips: bricks (light faces, dark mortar).
std::vector<uint8_t> WriteTx(const std::filesystem::path &path, int size)
{
    std::vector<uint8_t> file(24);
    const int32_t levels = static_cast<int32_t>(std::log2(size)) + 1;
    const int32_t header[6] = {0, size, size, levels, 21, size * size * 4};
    std::memcpy(file.data(), header, sizeof(header));
    std::vector<uint8_t> probe;
    for (int s = size, level = 0; level < levels; s /= 2, ++level)
    {
        const size_t start = file.size();
        for (int y = 0; y < s; ++y)
            for (int x = 0; x < s; ++x)
            {
                const int fx = x * size / s, fy = y * size / s;
                const bool mortar = fy % 16 < 2 || (fx + (fy / 16 % 2) * 16) % 32 < 2;
                const uint8_t v = mortar ? 50 : 190;
                file.insert(file.end(), {static_cast<uint8_t>(v * 0.7f), static_cast<uint8_t>(v * 0.8f), v, 255});
            }
        if (s <= storm::fx::kReliefProbeSize && probe.empty())
            probe.assign(file.begin() + static_cast<std::ptrdiff_t>(start), file.end());
    }
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
    return probe;
}
} // namespace

int main(int argc, char **argv)
{
    using namespace storm;
    if (argc < 2)
    {
        std::printf("usage: %s <model dir>\n", argv[0]);
        return 2;
    }
    const auto read = [&](const char *name) {
        std::ifstream in(std::string(argv[1]) + "/" + name, std::ios::binary);
        return std::vector<uint8_t>{std::istreambuf_iterator<char>(in), {}};
    };
    const std::vector<uint8_t> normal = read("pbrify-normal-int8.onnx"), height = read("pbrify-height-int8.onnx"),
                               roughness = read("pbrify-roughness-int8.onnx");
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "storm_relief_check";
    std::filesystem::remove_all(root);
    const std::filesystem::path textures = root / "RESOURCE/Textures", output = root / "relief";
    const std::vector<uint8_t> probe = WriteTx(textures / "Locations/StoneWall.tga.tx", 128);
    const std::vector<uint8_t> bottle_probe = WriteTx(textures / "Locations/Bottle.tga.tx", 64); // glass: gloss only
    WriteTx(textures / "Characters/Face.tga.tx", 64); // no relief, no gloss by the rules

    relief::Job job;
    job.textures = textures;
    job.output = output;
    job.models = {normal, height, roughness};
    job.map_size = 128;
    job.threads = 2;
    CHECK(!relief::Complete(textures, output, job.map_size));
    int calls = 0;
    relief::Result result = relief::Prepare(job, [&](const relief::Progress &) { return ++calls, false; });
    CHECK(result.status == 0 && result.prepared == 2 && result.failed == 0 && result.total == 2 && calls == 3);
    CHECK(result.provider.rfind("CPU", 0) == 0);
    // Marked complete (the game skips its background pass), until the game has another relief texture.
    CHECK(relief::Complete(textures, output, job.map_size) && !relief::Complete(textures, output, 256));
    // By name, only relief and gloss textures.
    {
        const std::filesystem::path game = root / "ordered";
        WriteTx(game / "RESOURCE/Textures/StoneB.tga.tx", 64);
        WriteTx(game / "RESOURCE/Textures/StoneA.tga.tx", 64);
        WriteTx(game / "RESOURCE/Textures/Characters/Face.tga.tx", 64);
        const std::vector<std::filesystem::path> order = relief::List(game / "RESOURCE/Textures");
        CHECK(order.size() == 2 && order[0].filename() == "StoneA.tga.tx" && order[1].filename() == "StoneB.tga.tx");
    }
    // The game's key: path, probe level (64x64 here), the stone class's method, size.
    const uint64_t key = fx::ReliefKey("locations/stonewall", probe, 64, 64, fx::ReliefMethod(fx::ReliefClass::Stone), job.map_size);
    const std::optional<fx::NormalMap> map = fx::ReadReliefMap(output, key);
    CHECK(map && map->width == 128 && map->mip_count == 8 && map->format == fx::NormalMapFormat::EacRg11);
    // A stone texture's map holds its material texture (EAC RG11: height, gloss; the same mips) and the stone's depth.
    CHECK(map && map->material.size() == map->rg.size() && map->depth == fx::ReliefDepthTexels(fx::ReliefClass::Stone) * 0.25f);
    // The bottle: no relief, its own gloss map (EAC RG11: metal mask, gloss) at its size, all mips.
    const uint64_t gloss_key = fx::ReliefKey("locations/bottle", bottle_probe, 64, 64, fx::GlossMethod(fx::GlossClass::Glass),
                                             fx::kGlossMapSize);
    const std::optional<fx::NormalMap> gloss = fx::ReadGlossMap(output, gloss_key);
    CHECK(gloss && gloss->width == 64 && gloss->mip_count == 7 && gloss->format == fx::NormalMapFormat::EacRg11);
    // Resumed: nothing to do; a stale map from another game is pruned after the complete run.
    fx::WriteReliefMap(output, 42, *map);
    result = relief::Prepare(job, nullptr);
    CHECK(result.status == 0 && result.prepared == 0 && result.existing == 2 && result.removed == 1 && !fx::HasReliefMap(output, 42));
    // The system evicts part of the cache: the pass is no longer complete, and says how many maps went.
    std::filesystem::remove(fx::ReliefFile(output, key));
    int missing = 0;
    CHECK(!relief::Complete(textures, output, job.map_size, &missing) && missing == 1);
    result = relief::Prepare(job, nullptr);
    CHECK(result.status == 0 && result.prepared == 1 && relief::Complete(textures, output, job.map_size, &missing) && missing == 0);
    // Cancelled at the start: nothing prepared, nothing pruned.
    std::filesystem::remove(fx::ReliefFile(output, key));
    fx::WriteReliefMap(output, 42, *map);
    result = relief::Prepare(job, [](const relief::Progress &) { return true; });
    CHECK(result.status == 1 && result.prepared == 0 && fx::HasReliefMap(output, 42));
    // A broken model is an error, not a crash.
    const std::vector<uint8_t> junk(100, 7);
    job.models = {normal, height, junk};
    CHECK(relief::Prepare(job, nullptr).status == 2);
    std::filesystem::remove_all(root);
    if (failures)
    {
        std::printf("%d relief check(s) failed\n", failures);
        return 1;
    }
    std::printf("relief checks passed\n");
    return 0;
}
