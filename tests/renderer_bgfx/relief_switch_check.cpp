// Relief switched on while textures are loaded (effects::NormalMaps): a texture of a relief class loaded while relief
// is off (a location entered on «Низкая») is looked up when relief is switched on (the preset raised in the same
// location), exactly like one loaded with relief on: a colour texture without a map is requested from the generator,
// a grey height texture gets its map derived into the relief directory. Before the fix none was queued until the
// location was loaded again.
//
// No GPU: the textures are shim stores filled the way Renderer::TextureLoad fills them, their game files are written
// into a temporary game; the generator has no model (no DeepBump), so a requested map ends up "missing" (counted).
#include "effects/normal_maps.hpp"
#include "effects/relief_generator.hpp"
#include "shim.hpp"
#include "storm/fx/relief_store.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

using namespace storm;

namespace
{
constexpr uint32_t kSize = 128, kLevels = 8;
int failures = 0;

void expect(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    failures += ok ? 0 : 1;
}

// A8R8G8B8 levels of a texture: grey (r = g = b) or coloured.
std::vector<std::vector<uint8_t>> Levels(bool grey)
{
    std::vector<std::vector<uint8_t>> levels;
    for (uint32_t l = 0, size = kSize; l < kLevels; ++l, size /= 2)
    {
        std::vector<uint8_t> bgra(size_t(size) * size * 4);
        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x)
            {
                uint8_t *p = &bgra[(size_t(y) * size + x) * 4];
                const uint8_t v = static_cast<uint8_t>(((x * 8 / size + y * 8 / size) & 1) ? 200 : 60);
                p[0] = v;
                p[1] = v;
                p[2] = grey ? v : static_cast<uint8_t>(255 - v);
                p[3] = 255;
            }
        levels.push_back(std::move(bgra));
    }
    return levels;
}

// The game file (.tx: flags, width, height, levels, format, top level bytes, then the levels).
void WriteTx(const std::filesystem::path &file, const std::vector<std::vector<uint8_t>> &levels)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    const int32_t header[6] = {0, kSize, kSize, kLevels, D3DFMT_A8R8G8B8, static_cast<int32_t>(levels[0].size())};
    out.write(reinterpret_cast<const char *>(header), sizeof(header));
    for (const auto &level : levels)
        out.write(reinterpret_cast<const char *>(level.data()), static_cast<std::streamsize>(level.size()));
}

// The loaded texture as Renderer::TextureLoad leaves it.
std::unique_ptr<shim::TexStore> Store(const std::vector<std::vector<uint8_t>> &levels)
{
    auto store = std::make_unique<shim::TexStore>();
    store->kind = shim::TexKind::Tex2D;
    store->width = store->height = kSize;
    store->levels = kLevels;
    store->format = D3DFMT_A8R8G8B8;
    store->subs.resize(kLevels);
    for (uint32_t l = 0; l < kLevels; ++l)
        store->subs[l].data = levels[l];
    return store;
}

// Waits until the entries leave the worker (Pending), at most 10 s.
effects::NormalMaps::Stats Settle(effects::NormalMaps &maps, const std::vector<shim::TexStore *> &stores)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;)
    {
        bool pending = false;
        for (shim::TexStore *store : stores)
            pending |= store->normal_map && store->normal_map->state == effects::NormalMapEntry::Pending;
        if (!pending || std::chrono::steady_clock::now() > until)
            return maps.stats();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
} // namespace

int main()
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "storm_relief_switch_check";
    std::filesystem::remove_all(root);
    const std::filesystem::path textures = root / "game" / "RESOURCE" / "Textures", relief = root / "relief";
    const auto colour = Levels(false), height = Levels(true);
    WriteTx(textures / "stone_wall.tga.tx", colour);
    WriteTx(textures / "bump_wall.tga.tx", height);
    WriteTx(textures / "brick_arch.tga.tx", colour);
    effects::ConfigureRelief({relief, textures, nullptr}); // no model: requested maps become "missing"

    effects::NormalMaps maps;
    maps.configure(false, 512); // «Низкая»: relief off
    auto stone = Store(colour), bump = Store(height), brick = Store(colour);
    maps.on_texture_loaded(*stone, "resource\\textures\\stone_wall.tga.tx", textures / "stone_wall.tga.tx");
    maps.on_texture_loaded(*bump, "resource\\textures\\bump_wall.tga.tx", textures / "bump_wall.tga.tx");
    const std::vector<shim::TexStore *> loaded{stone.get(), bump.get()};
    effects::NormalMaps::Stats off = Settle(maps, loaded);
    expect(stone->normal_map && bump->normal_map, "textures loaded while relief is off get their entries");
    expect(off.unqueued == 2 && off.waiting == 0 && off.missing == 0 && off.derived == 0,
           "nothing is looked up while relief is off");

    maps.configure(true, 512); // the preset raised in the same location
    const effects::NormalMaps::Stats on = Settle(maps, loaded);
    std::printf("after the switch: unqueued %u, waiting %u, missing %u, derived %u\n", on.unqueued, on.waiting, on.missing,
                on.derived);
    expect(on.unqueued == 0, "switching relief on looks up every texture loaded while it was off");
    expect(on.waiting + on.missing == 1, "the colour texture is requested from the generator");
    expect(on.derived == 1 && fx::HasReliefMap(relief, bump->normal_map->key),
           "the height texture's map is derived from its game file into the relief directory");

    // A texture loaded with relief on is looked up at once (unchanged).
    maps.on_texture_loaded(*brick, "resource\\textures\\brick_arch.tga.tx", textures / "brick_arch.tga.tx");
    const effects::NormalMaps::Stats later = Settle(maps, {stone.get(), bump.get(), brick.get()});
    expect(later.unqueued == 0 && later.waiting + later.missing == 2, "a texture loaded with relief on is requested at once");

    // Off and on again: nothing is requested twice, nothing is left unqueued.
    maps.configure(false, 512);
    maps.configure(true, 512);
    const effects::NormalMaps::Stats again = Settle(maps, {stone.get(), bump.get(), brick.get()});
    expect(again.unqueued == 0 && again.derived == 1, "off and on again derives nothing twice");

    effects::ReliefGenerator::instance().shutdown();
    std::filesystem::remove_all(root);
    if (failures)
    {
        std::printf("FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: relief_switch_check\n");
    return 0;
}
