// Relief and gloss switched on while textures are loaded (effects::NormalMaps): a texture of a relief class loaded while relief
// is off (a location entered on «Низкая») is looked up when relief is switched on (the preset raised in the same
// location), exactly like one loaded with relief on: a colour texture without a map is requested from the generator,
// a grey height texture gets its map derived into the relief directory. Before the fix none was queued until the
// location was loaded again. Gloss maps (Settings::gloss) the same: an entry at the load, requested from the
// generator (PBRify's roughness) when gloss is switched on; a texture whose relief class has a height (stone) keeps its gloss
// in its relief's material texture (fx::AddReliefMaterial) and has no gloss map of its own.
//
// No GPU: the textures are shim stores filled the way Renderer::TextureLoad fills them, their game files are written
// into a temporary game; the generator has no models (no PBRify), so a requested map ends up "missing" (counted).
#include "effects/normal_maps.hpp"
#include "effects/relief_generator.hpp"
#include "shim.hpp"
#include "storm/fx/lighting.hpp"
#include "storm/fx/gloss_map.hpp"
#include "storm/fx/relief_store.hpp"
#include "storm/render/effects.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
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
            pending |= (store->normal_map && store->normal_map->state == effects::NormalMapEntry::Pending) ||
                       (store->gloss_map && store->gloss_map->state == effects::NormalMapEntry::Pending);
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
    WriteTx(textures / "cannon_x.tga.tx", colour);
    WriteTx(textures / "windowglass_x.tga.tx", colour);
    effects::ConfigureRelief({relief, textures, nullptr}); // no model: requested maps become "missing"
    // A map an older build made for the stone texture (its key without the model and the options: method 3 alone),
    // as the owner's phone kept them: never taken for the current one.
    const std::vector<uint8_t> probe_level = colour[1];
    const uint64_t old_key = fx::ReliefKey("stone_wall", probe_level, 64, 64, 3, fx::kReliefMapSize);
    const fx::NormalMap old_map{8, 8, 4, fx::NormalMapFormat::EacRg11,
                                std::vector<uint8_t>(fx::NormalMapBytes(8, 8, fx::NormalMapFormat::EacRg11), 9)};
    fx::WriteReliefMap(relief, old_key, old_map);

    effects::NormalMaps maps;
    maps.configure(false, 512, false); // «Низкая»: relief and gloss off
    maps.configure_gloss(false, 256);
    auto stone = Store(colour), bump = Store(height), brick = Store(colour), cannon = Store(colour), glass = Store(colour);
    maps.on_texture_loaded(*stone, "resource\\textures\\stone_wall.tga.tx", textures / "stone_wall.tga.tx");
    maps.on_texture_loaded(*bump, "resource\\textures\\bump_wall.tga.tx", textures / "bump_wall.tga.tx");
    maps.on_texture_loaded(*cannon, "resource\\textures\\cannon_x.tga.tx", textures / "cannon_x.tga.tx");
    maps.on_texture_loaded(*glass, "resource\\textures\\windowglass_x.tga.tx", textures / "windowglass_x.tga.tx");
    const std::vector<shim::TexStore *> loaded{stone.get(), bump.get(), cannon.get(), glass.get()};
    effects::NormalMaps::Stats off = Settle(maps, loaded);
    expect(stone->normal_map && bump->normal_map, "textures loaded while relief is off get their entries");
    expect(!stone->gloss_map && stone->normal_map->material_gloss != fx::GlossClass::None && !cannon->gloss_map &&
               cannon->normal_map && cannon->normal_map->material_gloss == fx::GlossClass::Metal &&
               cannon->normal_map->relief == fx::ReliefClass::Metal && !glass->normal_map && glass->gloss_map &&
               glass->gloss_map->gloss == fx::GlossClass::Glass && !bump->gloss_map,
           "a gloss class without relief (glass) gets a gloss entry, one with relief (stone, metal) its relief's material, "
           "height maps none");
    expect(off.gloss_unqueued == 1 && off.gloss_waiting + off.gloss_missing == 0, "no gloss map is looked up while gloss is off");
    expect(off.unqueued == 3 && off.waiting == 0 && off.missing == 0 && off.derived == 0,
           "nothing is looked up while relief is off");

    maps.configure(true, 512, true); // the preset raised in the same location
    const effects::NormalMaps::Stats on = Settle(maps, loaded);
    std::printf("after the switch: unqueued %u, waiting %u, missing %u, derived %u\n", on.unqueued, on.waiting, on.missing,
                on.derived);
    expect(on.unqueued == 0, "switching relief on looks up every texture loaded while it was off");
    expect(on.waiting + on.missing == 2, "the colour textures are requested from the generator");
    expect(stone->normal_map->key != old_key && fx::HasReliefMap(relief, old_key),
           "the older build's map of the stone texture is not taken: its map is requested");
    expect(on.derived == 1 && fx::HasReliefMap(relief, bump->normal_map->key),
           "the height texture's map is derived from its game file into the relief directory");

    maps.configure_gloss(true, 256);
    const effects::NormalMaps::Stats glossy = Settle(maps, loaded);
    bool glossy_settled = true;
    for (shim::TexStore *store : loaded)
        glossy_settled &= !store->gloss_map || store->gloss_map->state != effects::NormalMapEntry::Pending;
    std::printf("after the gloss switch: unqueued %u, waiting %u, missing %u\n", glossy.gloss_unqueued, glossy.gloss_waiting,
                glossy.gloss_missing);
    expect(glossy_settled && glossy.gloss_unqueued == 0 && glossy.gloss_waiting + glossy.gloss_missing == 1,
           "switching gloss on requests the gloss maps of the textures loaded while it was off from the generator");

    // A texture loaded with relief on is looked up at once (unchanged).
    maps.on_texture_loaded(*brick, "resource\\textures\\brick_arch.tga.tx", textures / "brick_arch.tga.tx");
    const effects::NormalMaps::Stats later = Settle(maps, {stone.get(), bump.get(), brick.get()});
    expect(later.unqueued == 0 && later.waiting + later.missing == 3, "a texture loaded with relief on is requested at once");

    // Off and on again: nothing is requested twice, nothing is left unqueued.
    maps.configure(false, 512, false);
    maps.configure(true, 512, true);
    const effects::NormalMaps::Stats again = Settle(maps, {stone.get(), bump.get(), brick.get()});
    expect(again.unqueued == 0 && again.derived == 1, "off and on again derives nothing twice");

    // A map evicted from the cache while its texture stays loaded (the system frees storage, «Очистить кэш»): the next
    // draw finds it gone and makes it again, instead of failing.
    {
        fx::Lighting lighting;
        const uint32_t derived_before = maps.stats().derived;
        std::filesystem::remove(fx::ReliefFile(relief, bump->normal_map->key));
        maps.handle(*bump, lighting); // Stored: reads it, finds none
        Settle(maps, {bump.get()});
        maps.handle(*bump, lighting); // looked up again: derived
        const effects::NormalMaps::Stats evicted = Settle(maps, {bump.get()});
        expect(evicted.derived == derived_before + 1 && fx::HasReliefMap(relief, bump->normal_map->key) &&
                   bump->normal_map->state != effects::NormalMapEntry::Failed,
               "a map evicted while its texture is loaded is made again");
    }
    // The stores of earlier builds go at start (the files directory's relief, the old "NormalMaps" cache).
    const std::filesystem::path files = root / "files", cache = root / "cache";
    std::filesystem::create_directories(cache / "NormalMaps" / "ab");
    std::ofstream(cache / "NormalMaps" / "ab" / "0123.bin") << "old";
    std::filesystem::create_directories(files / "relief");
    std::ofstream(files / "relief" / "0123456789abcdef.relief") << "old";
    render::RemoveOldReliefStores({files / "relief", cache / "NormalMaps", root / "absent"}, relief);
    expect(!std::filesystem::exists(cache / "NormalMaps") && !std::filesystem::exists(files / "relief") &&
               std::filesystem::exists(cache) && std::filesystem::exists(files),
           "the relief stores of earlier builds are removed");
    // The store in use is never one of them: not itself (also spelt with a link or dots), nothing that holds it,
    // nothing inside it.
    const size_t maps_before = static_cast<size_t>(std::distance(std::filesystem::directory_iterator(relief), {}));
    std::filesystem::create_directories(relief / "inner");
    std::filesystem::create_symlink(relief, root / "relief-link");
    render::RemoveOldReliefStores({relief, root / "relief-link", relief / ".." / "relief", root, relief / "inner"}, relief);
    expect(std::filesystem::exists(relief / "inner") && std::filesystem::exists(root / "relief-link") &&
               static_cast<size_t>(std::distance(std::filesystem::directory_iterator(relief), {})) == maps_before + 1,
           "the relief directory in use, what holds it and what it holds are never removed as old stores");

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
