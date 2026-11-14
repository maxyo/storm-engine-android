#include "backend.hpp"

#include "relief.hpp"

#include <gpu.h>
#include <net.h>
#include <onnxruntime_c_api.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include <unistd.h>

namespace storm::relief
{
namespace
{
using Clock = std::chrono::steady_clock;

double Ms(Clock::time_point since)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
}

bool OrtOk(const OrtApi *api, OrtStatus *status, std::string &error)
{
    if (!status)
        return true;
    error = std::string("onnxruntime: ") + api->GetErrorMessage(status);
    api->ReleaseStatus(status);
    return false;
}

// Runs `image` (planar, width x height, the outer `halo` texels its padding) in tiles of `tile` inner texels (0 or a
// tile covering the inner area: one run), each with its halo; `infer` gets a tile's planes and their size and writes
// `scale` times as many. Their inner texels go into `planes` (the outer halo of the output stays unset). `tile` may be
// lowered by `infer`'s caller between tiles (the GPU's guard); `cancelled` stops between tiles.
bool RunTiles(const float *image, int width, int height, float *planes, int halo, int scale, int &tile,
              const std::atomic<bool> &cancelled, std::string &error,
              const std::function<bool(const float *, int, int, float *, double &)> &infer,
              const std::function<void(double)> &after = {})
{
    const int inner_w = width - 2 * halo, inner_h = height - 2 * halo;
    if (inner_w <= 0 || inner_h <= 0)
    {
        error = "image smaller than its halo";
        return false;
    }
    const size_t in_plane = static_cast<size_t>(width) * height;
    const int out_w = width * scale;
    const size_t out_plane = static_cast<size_t>(out_w) * height * scale;
    std::vector<float> in, out;
    for (int y0 = 0; y0 < inner_h;)
    {
        const int th = std::min(tile > 0 ? tile : inner_h, inner_h - y0);
        for (int x0 = 0; x0 < inner_w;)
        {
            if (cancelled)
            {
                error = "cancelled";
                return false;
            }
            const int tw = std::min(tile > 0 ? tile : inner_w, inner_w - x0);
            // The tile with its halo, from the padded image (the halo always lies inside it).
            const int iw = tw + 2 * halo, ih = th + 2 * halo;
            in.resize(static_cast<size_t>(iw) * ih * 3);
            out.resize(in.size() * scale * scale);
            for (int c = 0; c < 3; ++c)
                for (int y = 0; y < ih; ++y)
                    std::memcpy(in.data() + (static_cast<size_t>(c) * ih + y) * iw,
                                image + c * in_plane + static_cast<size_t>(y0 + y) * width + x0,
                                static_cast<size_t>(iw) * sizeof(float));
            double ms = 0.0;
            if (!infer(in.data(), iw, ih, out.data(), ms))
                return false;
            // The tile's inner texels into the output (its halo's are the neighbours' or the border's).
            const int sh = halo * scale, ow = iw * scale, oh = ih * scale;
            for (int c = 0; c < 3; ++c)
                for (int y = 0; y < th * scale; ++y)
                    std::memcpy(planes + c * out_plane + static_cast<size_t>((y0 + halo) * scale + y) * out_w +
                                    static_cast<size_t>((x0 + halo) * scale),
                                out.data() + (static_cast<size_t>(c) * oh + sh + y) * ow + sh,
                                static_cast<size_t>(tw * scale) * sizeof(float));
            x0 += tw;
            if (after)
                after(ms);
        }
        y0 += th;
    }
    return true;
}

// The CPU path's memory by the device's RAM: the inference in tiles (its activations grow with the texels: a whole
// 560² image held ~400 MB per network) and onnxruntime's arena capped, its memory returned after every run.
struct CpuLimits
{
    double ram_gb = 0.0;
    int tile = 0;     // inner texels of a tile; 0: the whole image at once
    size_t arena = 0; // bytes
};

const CpuLimits &Limits()
{
    static const CpuLimits limits = [] {
        CpuLimits l;
        const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
        l.ram_gb = pages > 0 && page > 0 ? static_cast<double>(pages) * static_cast<double>(page) / 1073741824.0 : 0.0;
        if (l.ram_gb <= 4.5)
        {
            l.tile = 128;
            l.arena = size_t{384} << 20;
        }
        else if (l.ram_gb <= 8.5)
        {
            l.tile = 256;
            l.arena = size_t{768} << 20;
        }
        else
        {
            l.tile = 0;
            l.arena = size_t{1536} << 20;
        }
        return l;
    }();
    return limits;
}

// One int8 model in onnxruntime's CPU provider (MLAS kernels, NEON dot products on arm64). Its XNNPACK provider was
// measured slower on a Galaxy S23 (1600 ms against 861 ms at 560², 4 threads); NNAPI is deprecated.
class CpuNetwork final : public Network
{
  public:
    CpuNetwork(const OrtApi *api, std::string describe, int halo, int scale, int tile)
        : api_(api), describe_(std::move(describe)), halo_(halo), scale_(scale), tile_(tile)
    {
    }
    ~CpuNetwork() override
    {
        if (session_)
            api_->ReleaseSession(session_);
        if (run_)
            api_->ReleaseRunOptions(run_);
        if (options_)
            api_->ReleaseSessionOptions(options_);
        if (memory_)
            api_->ReleaseMemoryInfo(memory_);
    }

    bool Open(OrtEnv *env, std::span<const uint8_t> model, std::string &error)
    {
        return OrtOk(api_, api_->CreateSessionOptions(&options_), error) &&
               OrtOk(api_, api_->SetSessionGraphOptimizationLevel(options_, ORT_ENABLE_ALL), error) &&
               OrtOk(api_, api_->DisablePerSessionThreads(options_), error) &&
               OrtOk(api_, api_->AddSessionConfigEntry(options_, "session.use_env_allocators", "1"), error) &&
               OrtOk(api_, api_->CreateSessionFromArray(env, model.data(), model.size(), options_, &session_), error) &&
               OrtOk(api_, api_->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory_), error) &&
               OrtOk(api_, api_->CreateRunOptions(&run_), error) &&
               // The arena's memory back to the system after every run: between textures only the weights stay.
               OrtOk(api_, api_->AddRunConfigEntry(run_, "memory.enable_memory_arena_shrinkage", "cpu:0"), error);
    }

    bool Infer(const float *image, int width, int height, float *planes, std::string &error) override
    {
        if (tile_ == 0 || (width - 2 * halo_ <= tile_ && height - 2 * halo_ <= tile_))
            return Run(image, width, height, planes, error);
        return RunTiles(image, width, height, planes, halo_, scale_, tile_, cancelled_, error,
                        [&](const float *in, int w, int h, float *out, double &) { return Run(in, w, h, out, error); });
    }

    // One whole image (its output `scale_` times the size).
    bool Run(const float *image, int width, int height, float *planes, std::string &error)
    {
        const int64_t shape[4] = {1, 3, height, width};
        const size_t texels = static_cast<size_t>(width) * height;
        OrtValue *input = nullptr, *output = nullptr;
        bool ok = OrtOk(api_,
                        api_->CreateTensorWithDataAsOrtValue(memory_, const_cast<float *>(image), texels * 3 * sizeof(float),
                                                             shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input),
                        error);
        const char *in_names[] = {"input"}, *out_names[] = {"output"};
        ok = ok && OrtOk(api_, api_->Run(session_, run_, in_names, &input, 1, out_names, 1, &output), error);
        float *data = nullptr;
        ok = ok && OrtOk(api_, api_->GetTensorMutableData(output, reinterpret_cast<void **>(&data)), error);
        if (ok)
            std::memcpy(planes, data, texels * 3 * scale_ * scale_ * sizeof(float));
        if (input)
            api_->ReleaseValue(input);
        if (output)
            api_->ReleaseValue(output);
        return ok;
    }

    void Cancel() override
    {
        cancelled_ = true;
        if (OrtStatus *status = api_->RunOptionsSetTerminate(run_))
            api_->ReleaseStatus(status);
    }

    std::string Describe() const override
    {
        return describe_ + (tile_ ? ", tiles of " + std::to_string(tile_) + " + " + std::to_string(halo_) + " texels" : "");
    }

  private:
    const OrtApi *api_;
    std::string describe_;
    int halo_, scale_, tile_;
    std::atomic<bool> cancelled_{false};
    OrtSessionOptions *options_ = nullptr;
    OrtSession *session_ = nullptr;
    OrtMemoryInfo *memory_ = nullptr;
    OrtRunOptions *run_ = nullptr;
};

class OrtRuntime final : public CpuRuntime
{
  public:
    explicit OrtRuntime(int tile) : tile_(tile) {}
    ~OrtRuntime() override
    {
        if (memory_)
            api_->ReleaseMemoryInfo(memory_);
        if (env_)
            api_->ReleaseEnv(env_);
    }

    bool Open(int threads, std::string &error)
    {
        threads_ = threads;
        api_ = OrtGetApiBase()->GetApi(ORT_API_VERSION);
        if (!api_)
        {
            error = "onnxruntime does not offer API version " + std::to_string(ORT_API_VERSION);
            return false;
        }
        // Waiting threads sleep instead of spinning: the game's threads share the cores, and spinning threads that
        // wait for each other on an oversubscribed CPU cost many times the work itself.
        OrtThreadingOptions *threading = nullptr;
        bool ok = OrtOk(api_, api_->CreateThreadingOptions(&threading), error) &&
                  OrtOk(api_, api_->SetGlobalIntraOpNumThreads(threading, threads), error) &&
                  OrtOk(api_, api_->SetGlobalInterOpNumThreads(threading, 1), error) &&
                  OrtOk(api_, api_->SetGlobalSpinControl(threading, 0), error) &&
                  OrtOk(api_, api_->CreateEnvWithGlobalThreadPools(ORT_LOGGING_LEVEL_WARNING, "storm-relief", threading, &env_),
                        error);
        if (threading)
            api_->ReleaseThreadingOptions(threading);
        // No usage telemetry (onnxruntime's Android build would try its 1DS client); one arena for every network,
        // capped by the RAM (Limits), growing only by what is asked.
        const CpuLimits &limits = Limits();
        const char *keys[] = {"max_mem", "arena_extend_strategy"};
        const size_t values[] = {limits.arena, 1}; // 1: kSameAsRequested
        OrtArenaCfg *arena = nullptr;
        ok = ok && OrtOk(api_, api_->DisableTelemetryEvents(env_), error) &&
             OrtOk(api_, api_->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory_), error) &&
             OrtOk(api_, api_->CreateArenaCfgV2(keys, values, 2, &arena), error) &&
             OrtOk(api_, api_->CreateAndRegisterAllocator(env_, memory_, arena), error);
        if (arena)
            api_->ReleaseArenaCfg(arena);
        if (ok)
            Log("relief CPU memory: %.1f GB of RAM: %s, the arena capped at %zu MB and emptied after every run",
                limits.ram_gb, tile_ ? ("tiles of " + std::to_string(tile_) + " texels").c_str() : "whole images",
                limits.arena >> 20);
        return ok;
    }

    std::unique_ptr<Network> Load(std::span<const uint8_t> onnx, int halo, int scale, std::string &error) override
    {
        auto network = std::make_unique<CpuNetwork>(
            api_, "CPU, " + std::to_string(threads_) + " threads of " + std::to_string(std::thread::hardware_concurrency()) +
                      " cores",
            halo, scale, tile_);
        if (!network->Open(env_, onnx, error))
            return nullptr;
        return network;
    }

  private:
    const OrtApi *api_ = nullptr;
    OrtEnv *env_ = nullptr;
    OrtMemoryInfo *memory_ = nullptr;
    int threads_ = 1;
    int tile_;
};

// The GPU: one submission per tile of the inner area, each at most kSubmitMs of GPU work (a frame of the game waits
// at most that long for a submission of ours). The tile size comes from a calibration of the network on this GPU
// (Calibrate): a submission's wall time is a fixed part (the CPU's recording, the upload and download, the fence
// wait: ~5 ms on a loaded host, whatever the size) plus the GPU's work, proportional to the texels. Only the second
// part holds the game's frames, so the tiles are as large as it allows, from kMinTile to kMaxTile.
constexpr int kMinTile = 32, kMaxTile = 512, kProbeSmall = 64, kProbeLarge = 256;
constexpr double kSubmitMs = 6.0;

struct Calibration
{
    double fixed_ms = 0.0, ms_per_texel = 0.0;
    int tile = kMinTile;
};

std::atomic<int> gpu_networks{0}; // alive: ReleaseGpu keeps the instance while any is

class GpuNetwork final : public Network
{
  public:
    GpuNetwork(int halo, int scale, std::string gpu) : halo_(halo), scale_(scale), gpu_(std::move(gpu))
    {
        ++gpu_networks;
        net_.opt.use_vulkan_compute = true;
        net_.opt.num_threads = 1;
        net_.opt.lightmode = true;
    }

    ~GpuNetwork() override { --gpu_networks; }

    bool Open(std::span<const uint8_t> param, std::span<const uint8_t> bin, std::string &error)
    {
        // ncnn reads the param text up to its end: a terminated copy.
        param_.assign(reinterpret_cast<const char *>(param.data()), param.size());
        if (net_.load_param_mem(param_.c_str()) != 0 || net_.load_model(bin.data()) <= 0)
        {
            error = "ncnn could not load the network";
            return false;
        }
        return Calibrate(error);
    }

    // The tile size for this network on this GPU, measured once per graph (the relief's three networks share one):
    // two tile sizes, each the faster of two runs after a warm-up (the GPU's allocations), fitted to fixed + per texel.
    bool Calibrate(std::string &error)
    {
        static std::mutex mutex;
        static std::vector<std::pair<std::string, Calibration>> known;
        const std::string key = param_ + "|" + std::to_string(halo_) + "|" + std::to_string(scale_);
        {
            std::lock_guard lock(mutex);
            for (const auto &[graph, calibration] : known)
                if (graph == key)
                {
                    calibration_ = calibration;
                    tile_ = calibration.tile;
                    return true;
                }
        }
        const auto run = [&](int inner, double &best) {
            const int size = inner + 2 * halo_;
            ncnn::Mat in(size, size, 3);
            in.fill(0.5f);
            best = 1e30;
            for (int i = 0; i < 3; ++i)
            {
                const Clock::time_point start = Clock::now();
                ncnn::Mat out;
                ncnn::Extractor ex = net_.create_extractor();
                if (ex.input("in0", in) != 0 || ex.extract("out0", out) != 0)
                {
                    error = "ncnn could not run the network";
                    return false;
                }
                if (i > 0)
                    best = std::min(best, Ms(start));
            }
            return true;
        };
        double small = 0.0, large = 0.0;
        if (!run(kProbeSmall, small) || !run(kProbeLarge, large))
            return false;
        const auto texels = [&](int inner) { return static_cast<double>(inner + 2 * halo_) * (inner + 2 * halo_); };
        Calibration c;
        c.ms_per_texel = std::max((large - small) / (texels(kProbeLarge) - texels(kProbeSmall)), 1e-9);
        c.fixed_ms = std::max(small - c.ms_per_texel * texels(kProbeSmall), 0.0);
        c.tile = kMinTile;
        for (int tile = kMinTile; tile <= kMaxTile; tile += 32)
            if (c.ms_per_texel * texels(tile) <= kSubmitMs)
                c.tile = tile;
        Log("relief GPU tiles: %d + %d texels (%.1f ms of GPU work a tile, %.1f ms fixed a submission; measured: %d px "
            "%.1f ms, %d px %.1f ms)",
            c.tile, halo_, c.ms_per_texel * texels(c.tile), c.fixed_ms, kProbeSmall, small, kProbeLarge, large);
        calibration_ = c;
        tile_ = c.tile;
        std::lock_guard lock(mutex);
        known.emplace_back(key, c);
        return true;
    }

    bool Infer(const float *image, int width, int height, float *planes, std::string &error) override
    {
        const auto infer = [&](const float *in, int iw, int ih, float *out, double &ms) {
            ncnn::Mat tile(iw, ih, 3);
            for (int c = 0; c < 3; ++c)
                for (int y = 0; y < ih; ++y)
                    std::memcpy(tile.channel(c).row(y), in + (static_cast<size_t>(c) * ih + y) * iw,
                                static_cast<size_t>(iw) * sizeof(float));
            const Clock::time_point start = Clock::now();
            ncnn::Mat result;
            ncnn::Extractor ex = net_.create_extractor();
            if (ex.input("in0", tile) != 0 || ex.extract("out0", result) != 0 || result.c != 3 ||
                result.w != iw * scale_ || result.h != ih * scale_)
            {
                error = "ncnn inference failed";
                return false;
            }
            ms = Ms(start);
            const int ow = iw * scale_, oh = ih * scale_;
            for (int c = 0; c < 3; ++c)
                for (int y = 0; y < oh; ++y)
                    std::memcpy(out + (static_cast<size_t>(c) * oh + y) * ow, result.channel(c).row(y),
                                static_cast<size_t>(ow) * sizeof(float));
            return true;
        };
        // The GPU slower than calibrated (another app, the GPU's clock down): the next tiles smaller.
        const auto guard = [&](double ms) {
            if (ms - calibration_.fixed_ms > 1.5 * kSubmitMs && tile_ > kMinTile)
                tile_ = std::max(kMinTile, tile_ / 2 / 32 * 32);
        };
        return RunTiles(image, width, height, planes, halo_, scale_, tile_, cancelled_, error, infer, guard);
    }

    void Cancel() override { cancelled_ = true; }

    std::string Describe() const override
    {
        return "GPU " + gpu_ + " (Vulkan, ncnn), tiles of " + std::to_string(tile_) + " + " +
               std::to_string(halo_) + " texels";
    }

  private:
    int halo_, scale_;
    std::string gpu_;
    int tile_ = kMinTile;
    Calibration calibration_;
    std::string param_;
    ncnn::Net net_;
    std::atomic<bool> cancelled_{false};
};

std::string CacheLine(const GpuDevice &gpu, std::span<const uint8_t> param, std::span<const uint8_t> bin, int threads)
{
    uint64_t digest = 0xcbf29ce484222325ull;
    for (const auto bytes : {param, bin})
        for (const uint8_t b : bytes)
            digest = (digest ^ b) * 0x100000001b3ull;
    char hex[24];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(digest));
    return gpu.name + " | " + gpu.driver + " | " + hex + " | " + std::to_string(threads) + " threads";
}
} // namespace

std::unique_ptr<CpuRuntime> CpuRuntime::Open(int threads, std::string &error, int tile)
{
    auto runtime = std::make_unique<OrtRuntime>(tile < 0 ? Limits().tile : tile);
    if (!runtime->Open(threads, error))
        return nullptr;
    return runtime;
}

namespace
{
std::mutex gpu_mutex;
GpuDevice gpu_device;
bool gpu_open = false;
} // namespace

const GpuDevice &Gpu()
{
    std::lock_guard lock(gpu_mutex);
    if (!gpu_open)
    {
        gpu_open = true;
        gpu_device = {};
        if (ncnn::create_gpu_instance() != 0 || ncnn::get_gpu_count() <= 0)
        {
            gpu_device.why = "no Vulkan compute device (ncnn found none: no Vulkan driver, or no queue with compute)";
            return gpu_device;
        }
        const ncnn::GpuInfo &info = ncnn::get_gpu_info(ncnn::get_default_gpu_index());
        gpu_device.available = true;
        gpu_device.name = info.device_name();
        const uint32_t v = info.driver_version();
        gpu_device.driver =
            std::to_string(v >> 22) + "." + std::to_string((v >> 12) & 0x3ff) + "." + std::to_string(v & 0xfff);
    }
    return gpu_device;
}

bool ReleaseGpu()
{
    std::lock_guard lock(gpu_mutex);
    if (!gpu_open || gpu_networks > 0)
        return false;
    if (gpu_device.available)
        ncnn::destroy_gpu_instance();
    gpu_open = false;
    return true;
}

std::unique_ptr<Network> LoadGpuNetwork(std::span<const uint8_t> param, std::span<const uint8_t> bin, int halo, int scale,
                                        std::string &error)
{
    if (!Gpu().available)
    {
        error = Gpu().why;
        return nullptr;
    }
    auto network = std::make_unique<GpuNetwork>(halo, scale, Gpu().name);
    if (!network->Open(param, bin, error))
        return nullptr;
    return network;
}

const char *DeviceName(Device device)
{
    return device == Device::Gpu ? "GPU" : "CPU";
}

namespace
{
std::filesystem::path MarkerFile(const std::filesystem::path &cache)
{
    return cache / "relief-gpu-running.txt";
}
std::filesystem::path CrashFile(const std::filesystem::path &cache)
{
    return cache / "relief-gpu-crashed.txt";
}
} // namespace

std::string GpuLabel(const GpuDevice &gpu)
{
    return gpu.name + ", driver " + gpu.driver;
}

void MarkGpuRunning(const std::filesystem::path &cache, const std::string &what)
{
    std::error_code ignored;
    std::filesystem::create_directories(cache, ignored);
    // Written through before the GPU work starts: a crash inside the driver leaves it behind.
    std::ofstream out(MarkerFile(cache), std::ios::trunc);
    out << what << "\n" << std::flush;
}

void ClearGpuRunning(const std::filesystem::path &cache)
{
    std::error_code ignored;
    std::filesystem::remove(MarkerFile(cache), ignored);
}

std::string GpuCrash(const std::filesystem::path &cache)
{
    std::ifstream in(CrashFile(cache));
    std::string line;
    std::getline(in, line);
    return line;
}

Device ChooseDevice(const std::filesystem::path &cache, std::span<const uint8_t> cpu_onnx, std::span<const uint8_t> gpu_param,
                    std::span<const uint8_t> gpu_bin, int halo, int threads)
{
    // The crash guard: the marker of GPU work (MarkGpuRunning) left from the last run means the process died in it.
    // That GPU and driver take the CPU from then on (relief-gpu-crashed.txt; «Очистить кэш» clears it); a crash before
    // the GPU was known keeps Vulkan closed altogether.
    std::error_code ignored;
    {
        std::ifstream in(MarkerFile(cache));
        std::string what;
        if (std::getline(in, what))
        {
            in.close();
            if (what == kGpuOpening)
                Log("ERROR: relief GPU inference crashed the previous run while Vulkan opened: using the CPU");
            else
                Log("ERROR: relief GPU inference crashed the previous run on %s: using the CPU", what.c_str());
            std::filesystem::create_directories(cache, ignored);
            std::ofstream(CrashFile(cache), std::ios::trunc) << what << "\n";
            ClearGpuRunning(cache);
        }
    }
    const std::string crash = GpuCrash(cache);
    if (crash == kGpuOpening)
    {
        Log("relief device: CPU (Vulkan crashed a previous run while it opened)");
        return Device::Cpu;
    }
    MarkGpuRunning(cache, kGpuOpening);
    const GpuDevice &gpu = Gpu();
    ClearGpuRunning(cache);
    if (!crash.empty())
    {
        if (crash == GpuLabel(gpu))
        {
            Log("relief device: CPU (GPU inference crashed a previous run on %s)", crash.c_str());
            return Device::Cpu;
        }
        // Another GPU or driver since: measured again.
        std::filesystem::remove(CrashFile(cache), ignored);
    }
    if (!gpu.available)
    {
        Log("relief device: CPU (%s)", gpu.why.c_str());
        return Device::Cpu;
    }
    const std::filesystem::path file = cache / "relief-device.txt";
    const std::string key = CacheLine(gpu, gpu_param, gpu_bin, threads);
    {
        std::ifstream in(file);
        std::string line, choice, numbers;
        if (std::getline(in, line) && line == key && std::getline(in, choice) && std::getline(in, numbers) &&
            (choice == "GPU" || choice == "CPU"))
        {
            Log("relief device: %s (measured before on %s: %s)", choice.c_str(), gpu.name.c_str(), numbers.c_str());
            return choice == "GPU" ? Device::Gpu : Device::Cpu;
        }
    }
    // A probe: a 128-texel tile and its halo, the same deterministic pattern on both, after a warm-up run (the GPU's
    // pipelines are made then).
    const int size = 128 + 2 * halo;
    std::vector<float> image(static_cast<size_t>(size) * size * 3), planes(image.size());
    for (size_t i = 0; i < image.size(); ++i)
        image[i] = static_cast<float>((i * 2654435761u) % 1000u) / 1000.0f;
    std::string error;
    const auto time = [&](Network &network, double &ms) {
        if (!network.Infer(image.data(), size, size, planes.data(), error))
            return false;
        const Clock::time_point start = Clock::now();
        if (!network.Infer(image.data(), size, size, planes.data(), error))
            return false;
        ms = Ms(start);
        return true;
    };
    double cpu_ms = 0.0, gpu_ms = 0.0;
    MarkGpuRunning(cache, GpuLabel(gpu));
    const Clock::time_point warm = Clock::now();
    std::unique_ptr<Network> probe = LoadGpuNetwork(gpu_param, gpu_bin, halo, 1, error);
    const bool gpu_ok = probe && time(*probe, gpu_ms);
    const double gpu_first_ms = Ms(warm) - gpu_ms;
    probe.reset();
    ClearGpuRunning(cache);
    if (!gpu_ok)
    {
        Log("relief device: CPU (the GPU %s failed the probe: %s)", gpu.name.c_str(), error.c_str());
        return Device::Cpu;
    }
    // The CPU's probe network on a runtime of its own, gone after the probe (onnxruntime is opened again only when
    // the CPU is chosen).
    const std::unique_ptr<CpuRuntime> runtime = CpuRuntime::Open(threads, error);
    const std::unique_ptr<Network> cpu = runtime ? runtime->Load(cpu_onnx, halo, 1, error) : nullptr;
    if (!cpu || !time(*cpu, cpu_ms))
    {
        Log("relief device: GPU %s (the CPU failed the probe: %s)", gpu.name.c_str(), error.c_str());
        return Device::Gpu;
    }
    const Device device = gpu_ms < cpu_ms ? Device::Gpu : Device::Cpu;
    char numbers[192];
    std::snprintf(numbers, sizeof(numbers), "%dx%d probe: GPU %.1f ms (first %.0f ms with its pipelines), CPU %.1f ms (%d threads)",
                  size, size, gpu_ms, gpu_first_ms, cpu_ms, threads);
    Log("relief device: %s (measured now on %s, driver %s: %s)", DeviceName(device), gpu.name.c_str(), gpu.driver.c_str(),
        numbers);
    std::filesystem::create_directories(cache, ignored);
    std::ofstream(file, std::ios::trunc) << key << "\n" << DeviceName(device) << "\n" << numbers << "\n";
    return device;
}

} // namespace storm::relief
