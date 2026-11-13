// Host check of the relief preparation with the real PBRify models (storm_relief_core): a small synthetic game,
// prepared (a stone texture's relief with its material, a window glass's own gloss map), marked complete, keyed as the game
// keys it, resumed without work, stale maps pruned, cancelled; the neural ×2 (the upscaler, the maps of a small texture
// made from its ×2 and the ×2 kept for the game, a character's ×2 alone).
#include "relief.hpp"

#include "backend.hpp"
#include "upscale.hpp"
#include "storm/fx/upscale.hpp"
#include "storm/fx/gloss_map.hpp"
#include "storm/fx/relief_model.hpp"
#include "storm/fx/height_map.hpp"
#include "storm/fx/relief_store.hpp"

#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
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
    std::vector<std::vector<uint8_t>> files;
    for (const char *name : relief::kModelFiles)
        files.push_back(read(name));
    const std::vector<uint8_t> &normal = files[0], &height = files[1], &roughness = files[2], &upscale = files[9];
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "storm_relief_check";
    std::filesystem::remove_all(root);
    const std::filesystem::path textures = root / "RESOURCE/Textures", output = root / "relief";
    const std::vector<uint8_t> probe = WriteTx(textures / "Locations/StoneWall.tga.tx", 128);
    const std::vector<uint8_t> bottle_probe = WriteTx(textures / "Locations/WindowGlass.tga.tx", 64); // glass: gloss only, no relief
    WriteTx(textures / "Characters/Face.tga.tx", 64); // no relief, no gloss by the rules

    relief::Job job;
    job.textures = textures;
    job.output = output;
    // The CPU (no ncnn graphs): the int8 models' maps; the GPU's are checked against them below.
    job.models = {.normal = normal, .height = height, .roughness = roughness, .upscale = upscale};
    job.map_size = 128;
    job.threads = 2;
    const size_t candidates = relief::List(textures).size();
    CHECK(candidates == 2 && !relief::Complete(output, job.map_size, candidates));
    int calls = 0;
    relief::Result result = relief::Prepare(job, [&](const relief::Progress &) { return ++calls, false; });
    CHECK(result.status == 0 && result.prepared == 2 && result.failed == 0 && result.total == 2 && calls == 3);
    CHECK(result.provider.rfind("CPU", 0) == 0);
    // Marked complete (the game skips its background pass), until the game has another relief texture.
    CHECK(relief::Complete(output, job.map_size, candidates) && !relief::Complete(output, 256, candidates) &&
          !relief::Complete(output, job.map_size, candidates + 1));
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
    // The window glass: no relief, its own gloss map (EAC RG11: metal mask, gloss) at its size, all mips.
    const uint64_t gloss_key = fx::ReliefKey("locations/windowglass", bottle_probe, 64, 64, fx::GlossMethod(fx::GlossClass::Glass),
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
    CHECK(!relief::Complete(output, job.map_size, candidates, &missing) && missing == 1);
    result = relief::Prepare(job, nullptr);
    CHECK(result.status == 0 && result.prepared == 1 && relief::Complete(output, job.map_size, candidates, &missing) && missing == 0);
    // Cancelled at the start: nothing prepared, nothing pruned.
    std::filesystem::remove(fx::ReliefFile(output, key));
    fx::WriteReliefMap(output, 42, *map);
    result = relief::Prepare(job, [](const relief::Progress &) { return true; });
    CHECK(result.status == 1 && result.prepared == 0 && fx::HasReliefMap(output, 42));
    // The neural ×2 (fx/upscale.hpp). The upscaler itself on the bricks: twice the size, the same colours when boxed back
    // (the network sharpens, it does not shift), and no seam where the wrapped texture tiles.
    {
        std::string error;
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        CHECK(relief::ReadTextureLevel(textures / "Locations/StoneWall.tga.tx", 1 << 16, rgba, w, h) && w == 128);
        std::vector<uint8_t> rolled(rgba.size());
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                std::memcpy(&rolled[(size_t(y) * w + (x + 37) % w) * 4], &rgba[(size_t(y) * w + x) * 4], 4);
        const auto cpu_runtime = relief::CpuRuntime::Open(2, error);
        std::vector<uint8_t> cpu_x2;
        // On the CPU (int8), and on the GPU (ncnn, fp16) where the host has a Vulkan compute device.
        for (int device = 0; device < (relief::Gpu().available ? 2 : 1); ++device)
        {
            std::unique_ptr<relief::Network> network =
                device == 0 ? (cpu_runtime ? cpu_runtime->Load(upscale, fx::kUpscalePad, 4, error) : nullptr)
                            : relief::LoadGpuNetwork(files[10], files[11], fx::kUpscalePad, 4, error);
            CHECK(network);
            if (!network)
                continue;
            const std::string describe = network->Describe();
            relief::Upscaler upscaler(std::move(network));
            std::vector<uint8_t> x2, x2_rolled;
            CHECK(upscaler.Run(rgba, w, h, true, x2, error) && x2.size() == size_t(4 * w * h * 4));
            if (x2.size() != size_t(4 * w * h * 4))
                continue;
            double diff = 0.0;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    for (int c = 0; c < 3; ++c)
                    {
                        int sum = 0;
                        for (int d = 0; d < 4; ++d)
                            sum += x2[((size_t(2 * y + d / 2) * 2 * w) + 2 * x + d % 2) * 4 + c];
                        diff += std::abs(sum / 4.0 - rgba[(size_t(y) * w + x) * 4 + c]);
                    }
            diff /= double(w) * h * 3;
            // No seam: the ×2 of the texture shifted by 37 texels is the ×2 shifted by 74 (wrapped padding beyond the
            // network's reach makes every texel see the same neighbours; on the GPU its own tiles must join too).
            int worst = 0;
            CHECK(upscaler.Run(rolled, w, h, true, x2_rolled, error) && x2_rolled.size() == x2.size());
            for (int y = 0; y < 2 * h && x2_rolled.size() == x2.size(); ++y)
                for (int x = 0; x < 2 * w; ++x)
                    for (int c = 0; c < 3; ++c)
                        worst = std::max(worst, std::abs(int(x2_rolled[(size_t(y) * 2 * w + (x + 74) % (2 * w)) * 4 + c]) -
                                                         int(x2[(size_t(y) * 2 * w + x) * 4 + c])));
            // The GPU's fp16 float graph against the CPU's int8: within the int8 model's own error (36.8 dB and up on
            // game textures against the float model).
            double psnr = 0.0;
            if (device == 0)
                cpu_x2 = x2;
            else
            {
                double squared = 0.0;
                for (size_t i = 0; i < x2.size(); ++i)
                    squared += (i % 4 == 3) ? 0.0 : double(x2[i] - cpu_x2[i]) * double(x2[i] - cpu_x2[i]);
                psnr = 10.0 * std::log10(255.0 * 255.0 / std::max(squared / (x2.size() * 0.75), 1e-9));
            }
            std::printf("x2 on %s: boxed back %.2f/255 from the source; shifted by 37 texels: at most %d/255 off%s\n",
                        describe.c_str(), diff, worst,
                        device ? (", against the CPU's int8: PSNR " + std::to_string(psnr) + " dB").c_str() : "");
            CHECK(diff < 8.0 && worst <= 3 && (device == 0 || psnr > 33.0));
        }
    }
    // With a display output the maps of a small texture come from its ×2, which is kept for the game under the game's key.
    {
        const std::filesystem::path x2_dir = root / "upscale";
        std::filesystem::remove(fx::ReliefFile(output, key));
        job.display = relief::UpscaleOutput{x2_dir, false};
        result = relief::Prepare(job, nullptr);
        const uint64_t x2_key = fx::ReliefKey("locations/stonewall", probe, 64, 64, fx::UpscaleMethod(false), 128);
        const std::optional<std::vector<uint8_t>> ktx = fx::ReadUpscaled(x2_dir, x2_key);
        CHECK(result.prepared == 1 && ktx && ktx->size() > 12 && std::memcmp(ktx->data() + 1, "KTX 11", 6) == 0);
        // KTX 1.1: pixelWidth at 36, mip levels at 56.
        uint32_t width = 0, levels = 0;
        if (ktx && ktx->size() > 60)
        {
            std::memcpy(&width, ktx->data() + 36, 4);
            std::memcpy(&levels, ktx->data() + 56, 4);
        }
        CHECK(width == 256 && levels == 9);
        job.display.reset();
        // The ×2 alone: a character's texture (no relief, no gloss) gets one, once; a texture without a target none.
        std::string error;
        const std::unique_ptr<relief::Session> session = relief::Session::Open({.normal = normal, .height = height, .roughness = roughness, .upscale = upscale}, 2,
                                                                               root / "device", error);
        CHECK(session);
        const relief::UpscaleOutput display{x2_dir, true};
        WriteTx(textures / "Weather/Sky/Sky1.tga.tx", 64);
        std::string failure;
        CHECK(session && session->Upscale(textures, textures / "Characters/Face.tga.tx", display, 64, failure) == relief::Outcome::Prepared);
        CHECK(session && session->Upscale(textures, textures / "Characters/Face.tga.tx", display, 64, failure) == relief::Outcome::Existing);
        CHECK(session && session->Upscale(textures, textures / "Weather/Sky/Sky1.tga.tx", display, 0, failure) == relief::Outcome::Skipped);
    }
    // A broken model is an error, not a crash.
    const std::vector<uint8_t> junk(100, 7);
    job.models = {.normal = normal, .height = height, .roughness = junk, .upscale = upscale};
    CHECK(relief::Prepare(job, nullptr).status == 2);
    // The GPU path (ncnn, fp16 graphs of the float models) against the CPU's int8 models, network by network, on a
    // brick pattern with the game's padding: the same maps within the int8 models' own error, and the tiles join.
    if (relief::Gpu().available)
    {
        std::string error;
        const auto cpu_runtime = relief::CpuRuntime::Open(2, error);
        const int size = 200 + 2 * fx::kReliefModelPad; // two tiles across, the second one partial
        std::vector<float> image(static_cast<size_t>(size) * size * 3);
        for (int c = 0; c < 3; ++c)
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                {
                    const bool mortar = y % 16 < 2 || (x + (y / 16 % 2) * 16) % 32 < 2;
                    image[(static_cast<size_t>(c) * size + y) * size + x] = (mortar ? 0.2f : 0.7f) * (0.8f + 0.1f * c);
                }
        for (int n = 0; n < 3; ++n)
        {
            const auto cpu = cpu_runtime ? cpu_runtime->Load(files[n], fx::kReliefModelPad, 1, error) : nullptr;
            const auto gpu = relief::LoadGpuNetwork(files[3 + 2 * n], files[4 + 2 * n], fx::kReliefModelPad, 1, error);
            std::vector<float> a(image.size()), b(image.size());
            CHECK(cpu && gpu && cpu->Infer(image.data(), size, size, a.data(), error) &&
                  gpu->Infer(image.data(), size, size, b.data(), error));
            double squared = 0.0;
            size_t count = 0;
            for (int c = 0; c < 3; ++c)
                for (int y = fx::kReliefModelPad; y < size - fx::kReliefModelPad; ++y)
                    for (int x = fx::kReliefModelPad; x < size - fx::kReliefModelPad; ++x)
                    {
                        const size_t i = (static_cast<size_t>(c) * size + y) * size + x;
                        const double d = std::clamp(a[i], 0.0f, 1.0f) - std::clamp(b[i], 0.0f, 1.0f);
                        squared += d * d;
                        ++count;
                    }
            const double psnr = 10.0 * std::log10(1.0 / std::max(squared / static_cast<double>(count), 1e-12));
            std::printf("%s: %s against the CPU's int8: PSNR %.1f dB\n", relief::kModelFiles[n], gpu ? gpu->Describe().c_str() : "-",
                        psnr);
            CHECK(psnr > 30.0);
        }
    }
    else
        std::printf("no GPU check: %s\n", relief::Gpu().why.c_str());
    // The CPU in tiles (a device with little RAM): the same output as the whole image, to the bit (the halo covers
    // what the network sees).
    {
        std::string error;
        const int size = 200 + 2 * fx::kReliefModelPad;
        std::vector<float> image(static_cast<size_t>(size) * size * 3);
        for (size_t i = 0; i < image.size(); ++i)
            image[i] = static_cast<float>((i * 2654435761u) % 1000u) / 1000.0f;
        const auto whole = relief::CpuRuntime::Open(2, error, 0), tiled = relief::CpuRuntime::Open(2, error, 64);
        const auto a = whole ? whole->Load(files[0], fx::kReliefModelPad, 1, error) : nullptr;
        const auto b = tiled ? tiled->Load(files[0], fx::kReliefModelPad, 1, error) : nullptr;
        std::vector<float> pa(image.size()), pb(image.size());
        CHECK(a && b && a->Infer(image.data(), size, size, pa.data(), error) && b->Infer(image.data(), size, size, pb.data(), error));
        bool same = true;
        for (int c = 0; c < 3; ++c)
            for (int y = fx::kReliefModelPad; y < size - fx::kReliefModelPad; ++y)
                for (int x = fx::kReliefModelPad; x < size - fx::kReliefModelPad; ++x)
                {
                    const size_t i = (static_cast<size_t>(c) * size + y) * size + x;
                    same &= pa[i] == pb[i];
                }
        std::printf("CPU tiles of 64 against the whole image: %s (%s)\n", same ? "the same" : "DIFFERENT",
                    b ? b->Describe().c_str() : "-");
        CHECK(same);
    }
    // The crash guard (backend.hpp MarkGpuRunning): a marker left behind by a run that died in the GPU's driver.
    {
        const relief::Models all{files[0], files[1], files[2], files[3], files[4],  files[5],
                                 files[6], files[7], files[8], files[9], files[10], files[11]};
        const std::filesystem::path cache = root / "crash";
        std::string error;
        std::string logged; // what the library logged (stderr) during open()
        const auto open = [&] {
            std::fflush(stderr);
            const int saved = dup(2);
            const std::filesystem::path log = root / "stderr.txt";
            std::freopen(log.string().c_str(), "w", stderr);
            const auto session = relief::Session::Open(all, 2, cache, error);
            std::string provider = session ? session->stats().provider : std::string("failed: ") + error;
            std::fflush(stderr);
            dup2(saved, 2);
            close(saved);
            std::ifstream in(log);
            logged.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return provider;
        };
        const auto running = [&] { return std::filesystem::exists(cache / "relief-gpu-running.txt"); };
        // Died while Vulkan opened: the CPU, Vulkan never touched again (until «Очистить кэш»).
        relief::MarkGpuRunning(cache, relief::kGpuOpening);
        std::string provider = open();
        CHECK(provider.rfind("CPU", 0) == 0 && relief::GpuCrash(cache) == relief::kGpuOpening && !running());
        CHECK(logged.find("ERROR: relief GPU inference crashed the previous run") != std::string::npos);
        CHECK(open().rfind("CPU", 0) == 0);
        std::filesystem::remove_all(cache);
        if (relief::Gpu().available)
        {
            const std::string gpu = relief::GpuLabel(relief::Gpu());
            // Died in this GPU's inference: the CPU for this GPU and driver, from then on.
            relief::MarkGpuRunning(cache, gpu);
            provider = open();
            std::printf("after a crash on %s: %s\n", gpu.c_str(), provider.c_str());
            CHECK(provider.rfind("CPU", 0) == 0 && relief::GpuCrash(cache) == gpu && !running());
            CHECK(logged.find("ERROR: relief GPU inference crashed the previous run on " + gpu + ": using the CPU") !=
                  std::string::npos);
            CHECK(open().rfind("CPU", 0) == 0);
            // Another driver since: measured again, and no marker is left by a clean run.
            std::ofstream(cache / "relief-gpu-crashed.txt", std::ios::trunc) << "Other GPU, driver 1.0.0\n";
            provider = open();
            CHECK(relief::GpuCrash(cache).empty() && !running());
            std::printf("after another GPU's crash: %s\n", provider.c_str());
        }
    }
    std::filesystem::remove_all(root);
    if (failures)
    {
        std::printf("%d relief check(s) failed\n", failures);
        return 1;
    }
    std::printf("relief checks passed\n");
    return 0;
}
