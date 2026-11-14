#include "backend.hpp"

#include "relief.hpp"

#include <gpu.h>
#include <net.h>
#include <onnxruntime_c_api.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

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

// One int8 model in onnxruntime's CPU provider (MLAS kernels, NEON dot products on arm64). Its XNNPACK provider was
// measured slower on a Galaxy S23 (1600 ms against 861 ms at 560², 4 threads); NNAPI is deprecated.
class CpuNetwork final : public Network
{
  public:
    CpuNetwork(const OrtApi *api, std::string describe) : api_(api), describe_(std::move(describe)) {}
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
               OrtOk(api_, api_->CreateRunOptions(&run_), error);
    }

    bool Infer(const float *image, int width, int height, float *planes, std::string &error) override
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
            std::memcpy(planes, data, texels * 3 * sizeof(float));
        if (input)
            api_->ReleaseValue(input);
        if (output)
            api_->ReleaseValue(output);
        return ok;
    }

    void Cancel() override
    {
        if (OrtStatus *status = api_->RunOptionsSetTerminate(run_))
            api_->ReleaseStatus(status);
    }

    std::string Describe() const override { return describe_; }

  private:
    const OrtApi *api_;
    std::string describe_;
    OrtSessionOptions *options_ = nullptr;
    OrtSession *session_ = nullptr;
    OrtMemoryInfo *memory_ = nullptr;
    OrtRunOptions *run_ = nullptr;
};

class OrtRuntime final : public CpuRuntime
{
  public:
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
        // No usage telemetry (onnxruntime's Android build would try its 1DS client); one arena for every network.
        return ok && OrtOk(api_, api_->DisableTelemetryEvents(env_), error) &&
               OrtOk(api_, api_->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory_), error) &&
               OrtOk(api_, api_->CreateAndRegisterAllocator(env_, memory_, nullptr), error);
    }

    std::unique_ptr<Network> Load(std::span<const uint8_t> onnx, std::string &error) override
    {
        auto network = std::make_unique<CpuNetwork>(
            api_, "CPU, " + std::to_string(threads_) + " threads of " + std::to_string(std::thread::hardware_concurrency()) +
                      " cores");
        if (!network->Open(env_, onnx, error))
            return nullptr;
        return network;
    }

  private:
    const OrtApi *api_ = nullptr;
    OrtEnv *env_ = nullptr;
    OrtMemoryInfo *memory_ = nullptr;
    int threads_ = 1;
};

// The GPU: tiles of kTile texels of the inner area at first, halved while one takes longer than kSubmitMs (a frame
// of the game waits at most that long for a submission of ours), down to kMinTile.
constexpr int kTile = 128, kMinTile = 32;
constexpr double kSubmitMs = 6.0;

class GpuNetwork final : public Network
{
  public:
    GpuNetwork(int halo, int scale) : halo_(halo), scale_(scale)
    {
        net_.opt.use_vulkan_compute = true;
        net_.opt.num_threads = 1;
        net_.opt.lightmode = true;
    }

    bool Open(std::span<const uint8_t> param, std::span<const uint8_t> bin, std::string &error)
    {
        // ncnn reads the param text up to its end: a terminated copy.
        param_.assign(reinterpret_cast<const char *>(param.data()), param.size());
        if (net_.load_param_mem(param_.c_str()) != 0 || net_.load_model(bin.data()) <= 0)
        {
            error = "ncnn could not load the network";
            return false;
        }
        // One tile untimed: the GPU's allocations are made then, not in the first texture's first tile (which would
        // make the tiles smaller for nothing).
        const int size = kTile + 2 * halo_;
        ncnn::Mat in(size, size, 3), out;
        in.fill(0.5f);
        ncnn::Extractor ex = net_.create_extractor();
        if (ex.input("in0", in) != 0 || ex.extract("out0", out) != 0)
        {
            error = "ncnn could not run the network";
            return false;
        }
        return true;
    }

    bool Infer(const float *image, int width, int height, float *planes, std::string &error) override
    {
        const int inner_w = width - 2 * halo_, inner_h = height - 2 * halo_;
        if (inner_w <= 0 || inner_h <= 0)
        {
            error = "image smaller than its halo";
            return false;
        }
        const size_t in_plane = static_cast<size_t>(width) * height;
        const int out_w = width * scale_;
        const size_t out_plane = static_cast<size_t>(out_w) * height * scale_;
        for (int y0 = 0; y0 < inner_h;)
        {
            const int th = std::min(tile_, inner_h - y0);
            for (int x0 = 0; x0 < inner_w;)
            {
                if (cancelled_)
                {
                    error = "cancelled";
                    return false;
                }
                const int tw = std::min(tile_, inner_w - x0);
                // The tile with its halo, from the padded image (the halo always lies inside it).
                const int iw = tw + 2 * halo_, ih = std::min(th, inner_h - y0) + 2 * halo_;
                ncnn::Mat in(iw, ih, 3);
                for (int c = 0; c < 3; ++c)
                    for (int y = 0; y < ih; ++y)
                        std::memcpy(in.channel(c).row(y), image + c * in_plane + static_cast<size_t>(y0 + y) * width + x0,
                                    static_cast<size_t>(iw) * sizeof(float));
                const Clock::time_point start = Clock::now();
                ncnn::Mat out;
                ncnn::Extractor ex = net_.create_extractor();
                if (ex.input("in0", in) != 0 || ex.extract("out0", out) != 0 || out.c != 3 || out.w != iw * scale_ ||
                    out.h != ih * scale_)
                {
                    error = "ncnn inference failed";
                    return false;
                }
                const double ms = Ms(start);
                // The tile's inner texels into the output (its halo's are the neighbours' or the border's).
                const int sh = halo_ * scale_;
                for (int c = 0; c < 3; ++c)
                    for (int y = 0; y < (ih - 2 * halo_) * scale_; ++y)
                        std::memcpy(planes + c * out_plane + static_cast<size_t>((y0 + halo_) * scale_ + y) * out_w +
                                        static_cast<size_t>((x0 + halo_) * scale_),
                                    out.channel(c).row(sh + y) + sh, static_cast<size_t>(tw * scale_) * sizeof(float));
                x0 += tw;
                if (ms > kSubmitMs && tile_ > kMinTile)
                    tile_ /= 2; // the next tiles shorter (this row continues at the new size)
            }
            y0 += th;
        }
        return true;
    }

    void Cancel() override { cancelled_ = true; }

    std::string Describe() const override
    {
        return "GPU " + Gpu().name + " (Vulkan, ncnn), tiles of " + std::to_string(tile_) + " + " +
               std::to_string(halo_) + " texels";
    }

  private:
    int halo_, scale_;
    int tile_ = kTile;
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

std::unique_ptr<CpuRuntime> CpuRuntime::Open(int threads, std::string &error)
{
    auto runtime = std::make_unique<OrtRuntime>();
    if (!runtime->Open(threads, error))
        return nullptr;
    return runtime;
}

const GpuDevice &Gpu()
{
    static GpuDevice device;
    static std::once_flag once;
    std::call_once(once, [] {
        if (ncnn::create_gpu_instance() != 0 || ncnn::get_gpu_count() <= 0)
        {
            device.why = "no Vulkan compute device (ncnn found none: no Vulkan driver, or no queue with compute)";
            return;
        }
        const ncnn::GpuInfo &info = ncnn::get_gpu_info(ncnn::get_default_gpu_index());
        device.available = true;
        device.name = info.device_name();
        const uint32_t v = info.driver_version();
        device.driver = std::to_string(v >> 22) + "." + std::to_string((v >> 12) & 0x3ff) + "." + std::to_string(v & 0xfff);
    });
    return device;
}

std::unique_ptr<Network> LoadGpuNetwork(std::span<const uint8_t> param, std::span<const uint8_t> bin, int halo, int scale,
                                        std::string &error)
{
    if (!Gpu().available)
    {
        error = Gpu().why;
        return nullptr;
    }
    auto network = std::make_unique<GpuNetwork>(halo, scale);
    if (!network->Open(param, bin, error))
        return nullptr;
    return network;
}

const char *DeviceName(Device device)
{
    return device == Device::Gpu ? "GPU" : "CPU";
}

Device ChooseDevice(const std::filesystem::path &cache, Network &cpu, std::span<const uint8_t> gpu_param,
                    std::span<const uint8_t> gpu_bin, int halo, int threads)
{
    const GpuDevice &gpu = Gpu();
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
    // A probe: one GPU tile's worth (kTile + the halo), the same deterministic pattern on both, after a warm-up run
    // (the GPU's pipelines are made then).
    const int size = kTile + 2 * halo;
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
    const Clock::time_point warm = Clock::now();
    std::unique_ptr<Network> probe = LoadGpuNetwork(gpu_param, gpu_bin, halo, 1, error);
    const bool gpu_ok = probe && time(*probe, gpu_ms);
    const double gpu_first_ms = Ms(warm) - gpu_ms;
    if (!gpu_ok)
    {
        Log("relief device: CPU (the GPU %s failed the probe: %s)", gpu.name.c_str(), error.c_str());
        return Device::Cpu;
    }
    if (!time(cpu, cpu_ms))
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
    std::error_code ignored;
    std::filesystem::create_directories(cache, ignored);
    std::ofstream(file, std::ios::trunc) << key << "\n" << DeviceName(device) << "\n" << numbers << "\n";
    return device;
}

} // namespace storm::relief
