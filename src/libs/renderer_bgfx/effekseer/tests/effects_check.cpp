// Effekseer host checks against a game directory (never part of the repository).
//
//   storm_effekseer_check load   <game dir>
//       Loads every Resource/Effects/*.efkefc through the engine API on bgfx's Noop backend,
//       plays each one for two seconds and draws it. Fails when an effect does not load or
//       references a texture/model that cannot be read.
//   storm_effekseer_check render <game dir> <effect file name> <out.png> [camera distance, default 2]
//       Renders the effect offscreen on the real GLES backend (headless EGL) after one second,
//       writes the picture and fails when nothing was drawn.
//
// Effect textures use the engine texture cache in $STORM_TEXTURE_CACHE_DIR (no disk cache when
// unset): the first run decodes and transcodes, later runs load prebuilt ETC2 mips.
// Exit code 77 (CTest SKIP_RETURN_CODE) when the game directory does not exist.
#include "storm/render/effekseer.hpp"
#include "storm/render/effekseer_device.hpp"

#include "service.hpp"

#include <bgfx/bgfx.h>
#include <bimg/bimg.h>
#include <bx/file.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace efk = storm::render::effekseer;

namespace
{
constexpr int kSkip = 77;

// CPU time of the process: wall time is meaningless on a shared, overloaded build machine.
double cpu_ms()
{
    timespec ts{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

std::string lower(std::string text)
{
    for (char &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// Resolves game paths component by component, ignoring case, like the engine's file service.
class GameFiles
{
  public:
    explicit GameFiles(fs::path root) : root_(std::move(root))
    {
    }
    bool read(const std::string &path, std::vector<uint8_t> &data)
    {
        fs::path current = root_;
        size_t start = 0;
        while (start < path.size())
        {
            size_t end = path.find('/', start);
            if (end == std::string::npos)
                end = path.size();
            const std::string part = lower(path.substr(start, end - start));
            start = end + 1;
            if (part.empty() || part == ".")
                continue;
            bool found = false;
            std::error_code ec;
            for (const auto &entry : fs::directory_iterator(current, ec))
            {
                if (lower(entry.path().filename().string()) == part)
                {
                    current = entry.path();
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
        }
        std::ifstream file(current, std::ios::binary);
        if (!file)
            return false;
        data.assign(std::istreambuf_iterator<char>(file), {});
        ++reads;
        return true;
    }
    uint32_t reads = 0;

  private:
    fs::path root_;
};

struct Mat
{
    float m[16];
};

// D3D-style (left-handed, row-vector) camera, as the engine passes it.
Mat look_at_lh(float ex, float ey, float ez, float ax, float ay, float az)
{
    float zx = ax - ex, zy = ay - ey, zz = az - ez;
    const float zl = std::sqrt(zx * zx + zy * zy + zz * zz);
    zx /= zl, zy /= zl, zz /= zl;
    float xx = zz, xy = 0.0f, xz = -zx; // up (0,1,0) x z
    const float xl = std::sqrt(xx * xx + xz * xz);
    xx /= xl, xz /= xl;
    const float yx = zy * xz - zz * xy, yy = zz * xx - zx * xz, yz = zx * xy - zy * xx;
    return {{xx, yx, zx, 0, xy, yy, zy, 0, xz, yz, zz, 0, -(xx * ex + xy * ey + xz * ez),
             -(yx * ex + yy * ey + yz * ez), -(zx * ex + zy * ey + zz * ez), 1}};
}

Mat perspective_lh(float fov_y, float aspect, float zn, float zf)
{
    const float h = 1.0f / std::tan(fov_y * 0.5f);
    const float w = h / aspect;
    return {{w, 0, 0, 0, 0, h, 0, 0, 0, 0, zf / (zf - zn), 1, 0, 0, -zn * zf / (zf - zn), 0}};
}

std::vector<fs::path> effect_files(const fs::path &game)
{
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto &top : fs::directory_iterator(game, ec))
    {
        if (lower(top.path().filename().string()) != "resource")
            continue;
        for (const auto &sub : fs::directory_iterator(top.path(), ec))
        {
            if (lower(sub.path().filename().string()) != "effects")
                continue;
            for (const auto &file : fs::directory_iterator(sub.path(), ec))
                if (lower(file.path().extension().string()) == ".efkefc")
                    files.push_back(file.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

void print_line(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::printf("\n");
}

bool init_bgfx(bgfx::RendererType::Enum type)
{
    bgfx::Init init;
    init.type = type;
    // Headless (no window, 0x0 back buffer): an EGL pbuffer context on the GLES backend; every
    // picture goes to offscreen frame buffers.
    init.swapChain.width = 0;
    init.swapChain.height = 0;
    init.swapChain.nwh = nullptr;
    init.reset = BGFX_RESET_NONE;
    init.fallback = false;
    if (!bgfx::init(init))
        return false;
    const bgfx::Caps *caps = bgfx::getCaps();
    efk::AttachDevice({caps->originBottomLeft, caps->homogeneousDepth});
    storm::texture::Config cache;
    if (const char *dir = std::getenv("STORM_TEXTURE_CACHE_DIR"))
        cache.directory = dir;
    storm::texture::Service::instance().configure(cache, &print_line);
    std::printf("texture cache: %s\n", cache.directory.empty() ? "none (STORM_TEXTURE_CACHE_DIR unset)"
                                                               : cache.directory.string().c_str());
    return true;
}

// Lets the ETC2 transcodes finish (the swap happens in Update), so the disk cache is complete.
bool drain_transcodes()
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (efk::GetStats().textures_transcoding > 0)
    {
        if (std::chrono::steady_clock::now() > deadline)
        {
            std::printf("FAIL: %u texture transcodes still running after 120 s\n", efk::GetStats().textures_transcoding);
            return false;
        }
        efk::Update(0.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

void shutdown_bgfx()
{
    efk::DetachDevice();
    storm::texture::Service::instance().shutdown();
    bgfx::shutdown();
}

int load_all(const fs::path &game)
{
    const std::vector<fs::path> files = effect_files(game);
    if (files.empty())
    {
        std::printf("FAIL: no Resource/Effects/*.efkefc under %s\n", game.string().c_str());
        return 1;
    }
    if (!init_bgfx(bgfx::RendererType::Noop))
    {
        std::printf("FAIL: bgfx Noop init\n");
        return 1;
    }
    GameFiles reader(game);
    efk::Configure({[&](const std::string &path, std::vector<uint8_t> &data) { return reader.read(path, data); }});
    int loaded = 0, incomplete = 0;
    double load_total_ms = 0.0;
    efk::EffectInfo total;
    const Mat view = look_at_lh(0, 2, -8, 0, 1, 0);
    const Mat proj = perspective_lh(1.0f, 16.0f / 9.0f, 0.1f, 200.0f);
    const auto started = std::chrono::steady_clock::now();
    for (const fs::path &file : files)
    {
        // Upper/lower case differs from the disk on purpose: lookups must not depend on it.
        const std::string path = "RESOURCE/effects/" + lower(file.filename().string());
        const double load_start = cpu_ms();
        const efk::EffectId id = efk::LoadEffect(path);
        const double load_ms = cpu_ms() - load_start;
        load_total_ms += load_ms;
        efk::EffectInfo info;
        if (id == 0 || !efk::GetEffectInfo(id, info))
        {
            std::printf("FAIL  %s: not loaded\n", file.filename().string().c_str());
            continue;
        }
        ++loaded;
        const bool complete = info.missing_textures == 0 && info.missing_models == 0;
        incomplete += !complete;
        // Two seconds of simulation (20 steps of 0.1 s) and drawing exercise every node renderer
        // on the Noop backend.
        const efk::InstanceId instance = efk::Play(id, {0, 0, 0});
        int32_t peak_instances = 0, peak_draws = 0;
        for (int step = 0; step < 20; ++step)
        {
            efk::Update(0.1f);
            bgfx::touch(0);
            efk::DrawToView(0, view.m, proj.m);
            bgfx::frame();
            peak_instances = std::max(peak_instances, efk::GetStats().instances);
            peak_draws = std::max(peak_draws, efk::GetStats().draw_calls);
        }
        efk::Stop(instance);
        std::printf("%s  %-28s nodes=%d textures=%d missing=%d models=%d missing=%d sounds=%d distortion=%d "
                    "soft=%d material=%d peak_instances=%d peak_draws=%d load_cpu=%.1fms\n",
                    complete ? "OK  " : "FAIL", file.filename().string().c_str(), info.nodes, info.textures,
                    info.missing_textures, info.models, info.missing_models, info.sounds, info.distortion_nodes,
                    info.soft_particle_nodes, info.material_nodes, peak_instances, peak_draws, load_ms);
        total.nodes += info.nodes;
        total.textures += info.textures;
        total.models += info.models;
        total.sounds += info.sounds;
        total.distortion_nodes += info.distortion_nodes;
        total.soft_particle_nodes += info.soft_particle_nodes;
        total.material_nodes += info.material_nodes;
    }
    const double drain_start = cpu_ms();
    const bool drained = drain_transcodes();
    const double drain_ms = cpu_ms() - drain_start;
    const efk::Stats stats = efk::GetStats();
    std::printf("textures: %u from the ETC2 cache, %u decoded; transcode wait %.0f ms CPU\n",
                stats.texture_cache_hits, stats.textures_decoded, drain_ms);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("effects: %d/%zu loaded, %d with missing resources; nodes=%d textures=%d models=%d sounds=%d "
                "distortion_nodes=%d soft_particle_nodes=%d material_nodes=%d; %u files read; loading %.0f ms CPU; %.2f s\n",
                loaded, files.size(), incomplete, total.nodes, total.textures, total.models, total.sounds,
                total.distortion_nodes, total.soft_particle_nodes, total.material_nodes, reader.reads, load_total_ms,
                seconds);
    shutdown_bgfx();
    return loaded == static_cast<int>(files.size()) && incomplete == 0 && drained ? 0 : 1;
}

int render_one(const fs::path &game, const std::string &effect, const fs::path &out, float distance)
{
    constexpr uint16_t kWidth = 640, kHeight = 360;
    if (!init_bgfx(bgfx::RendererType::OpenGLES))
    {
        std::printf("FAIL: bgfx GLES init (headless EGL)\n");
        return 1;
    }
    std::printf("renderer: %s\n", bgfx::getRendererName(bgfx::getRendererType()));
    GameFiles reader(game);
    efk::Configure({[&](const std::string &path, std::vector<uint8_t> &data) { return reader.read(path, data); }});
    const efk::EffectId id = efk::LoadEffect("resource/effects/" + effect);
    if (id == 0)
    {
        shutdown_bgfx();
        std::printf("FAIL: %s not loaded\n", effect.c_str());
        return 1;
    }
    // The game scene target: gamma-encoded RGBA8 colour + depth, like the shim's back buffer.
    const bgfx::TextureHandle color =
        bgfx::createTexture2D(kWidth, kHeight, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
    const bgfx::TextureHandle depth =
        bgfx::createTexture2D(kWidth, kHeight, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY);
    const bgfx::TextureHandle attachments[] = {color, depth};
    const bgfx::FrameBufferHandle target = bgfx::createFrameBuffer(2, attachments, false);
    const bgfx::TextureHandle readback = bgfx::createTexture2D(kWidth, kHeight, false, 1, bgfx::TextureFormat::RGBA8,
                                                               BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    constexpr uint32_t kBackground = 0x203040ff;
    const Mat view = look_at_lh(0, 0.3f * distance, -distance, 0, 0.15f * distance, 0);
    const Mat proj = perspective_lh(1.0f, float(kWidth) / kHeight, 0.1f, 200.0f);
    efk::Play(id, {0, 0, 0});
    for (int frame = 0; frame < 60; ++frame)
        efk::Update(1.0f / 60.0f);
    if (!drain_transcodes()) // draw the ETC2 versions, the textures the game ends up with
    {
        shutdown_bgfx();
        return 1;
    }
    std::printf("textures: %u from the ETC2 cache, %u decoded\n", efk::GetStats().texture_cache_hits,
                efk::GetStats().textures_decoded);
    bgfx::setViewFrameBuffer(1, target);
    bgfx::setViewRect(1, 0, 0, kWidth, kHeight);
    bgfx::setViewClear(1, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, kBackground, 1.0f, 0);
    bgfx::setViewMode(1, bgfx::ViewMode::Sequential);
    bgfx::touch(1);
    efk::DrawToView(1, view.m, proj.m);
    const int32_t draws = efk::GetStats().draw_calls;
    const int32_t instances = efk::GetStats().instances;
    bgfx::TextureRegion src, dst;
    src.init(color);
    dst.init(readback);
    bgfx::blit(2, dst, src);
    std::vector<uint8_t> pixels(size_t(kWidth) * kHeight * 4);
    const uint32_t ready = bgfx::read(dst, pixels.data());
    for (uint32_t frame = bgfx::frame(); frame < ready;)
        frame = bgfx::frame();
    uint32_t changed = 0;
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        const int dr = std::abs(pixels[i] - 0x20), dg = std::abs(pixels[i + 1] - 0x30),
                  db = std::abs(pixels[i + 2] - 0x40);
        changed += dr + dg + db > 12;
    }
    bx::FileWriter writer;
    bx::Error error;
    if (bx::open(&writer, out.string().c_str(), false, &error))
    {
        bimg::imageWritePng(&writer, kWidth, kHeight, kWidth * 4, pixels.data(), bimg::TextureFormat::RGBA8, false,
                            &error);
        bx::close(&writer);
    }
    bgfx::destroy(target);
    bgfx::destroy(color);
    bgfx::destroy(depth);
    bgfx::destroy(readback);
    shutdown_bgfx();
    std::printf("%s: instances=%d draw_calls=%d changed_pixels=%u (%.2f%%) -> %s\n", effect.c_str(), instances,
                draws, changed, 100.0 * changed / (kWidth * kHeight), out.string().c_str());
    return draws > 0 && changed > 0 ? 0 : 1;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 3 || (std::strcmp(argv[1], "load") != 0 && std::strcmp(argv[1], "render") != 0) ||
        (std::strcmp(argv[1], "render") == 0 && argc < 5))
    {
        std::fprintf(stderr, "usage: %s load <game dir> | render <game dir> <effect.efkefc> <out.png> [distance]\n", argv[0]);
        return 2;
    }
    const fs::path game = argv[2];
    if (!fs::is_directory(game))
    {
        std::printf("SKIP: game directory %s does not exist\n", game.string().c_str());
        return kSkip;
    }
    if (std::strcmp(argv[1], "load") == 0)
        return load_all(game);
    return render_one(game, argv[3], argv[4], argc > 5 ? std::stof(argv[5]) : 2.0f);
}
