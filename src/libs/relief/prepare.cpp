#include "relief.hpp"

#include "formats.hpp"
#include "storm/fx/relief_model.hpp"
#include "storm/fx/gloss_map.hpp"
#include "storm/fx/height_map.hpp"
#include "storm/fx/normal_map.hpp"
#include "storm/fx/relief_store.hpp"

#include <onnxruntime_c_api.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <sys/resource.h>

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace storm::relief
{

void Log(const char *format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "storm-relief", "%s", buffer);
#endif
    std::fprintf(stderr, "[storm-relief] %s\n", buffer); // the shell tool on a device too (nowhere in the app)
}

namespace
{
using Clock = std::chrono::steady_clock;
constexpr uint32_t kTxFlagPalette = 1, kTxFlagCubemap = 2;

double Seconds(Clock::time_point since)
{
    return std::chrono::duration<double>(Clock::now() - since).count();
}

// A game texture file: the header of the .tx format and its levels in the order the renderer reads them (each level a
// quarter of the one above). Only the levels from `first` on are read: a key needs the small probe level alone, and
// reading whole files made the game's first pass over 1376 textures take 88 s on an emulator.
struct TxFile
{
    uint32_t flags = 0, width = 0, height = 0, levels = 0, format = 0, top_bytes = 0;
    int first = 0;             // the first level in `data`
    std::vector<uint8_t> data; // levels first .. levels - 1

    // The header alone.
    bool Header(std::ifstream &in)
    {
        int32_t header[6];
        if (!in.read(reinterpret_cast<char *>(header), sizeof(header)))
            return false;
        flags = static_cast<uint32_t>(header[0]);
        width = static_cast<uint32_t>(header[1]);
        height = static_cast<uint32_t>(header[2]);
        levels = static_cast<uint32_t>(header[3]);
        format = static_cast<uint32_t>(header[4]);
        top_bytes = static_cast<uint32_t>(header[5]);
        return !(flags & (kTxFlagPalette | kTxFlagCubemap)) && texture::tx_format_known(format) && width > 0 && height > 0 &&
               width <= 8192 && height <= 8192 && levels > 0 && top_bytes == texture::d3d_level_bytes(format, width, height);
    }
    // The header and the levels from the one `choose` picks (given the header) on.
    template <typename Choose> bool Load(const std::filesystem::path &path, Choose &&choose)
    {
        std::ifstream in(path, std::ios::binary);
        if (!Header(in))
            return false;
        first = std::clamp(choose(*this), 0, static_cast<int>(levels) - 1);
        in.seekg(static_cast<std::streamoff>(24 + Offset(first)));
        data.assign(std::istreambuf_iterator<char>(in), {});
        return !data.empty();
    }
    // The levels from the probe level on (the key, the grey test).
    bool LoadProbe(const std::filesystem::path &path)
    {
        return Load(path, [](const TxFile &tx) { return std::max(ProbeLevel(tx), 0); });
    }
    static int ProbeLevel(const TxFile &tx)
    {
        return fx::ReliefProbeLevel(static_cast<int>(tx.width), static_cast<int>(tx.height), static_cast<int>(tx.levels));
    }
    size_t Offset(int level) const
    {
        size_t offset = 0, size = top_bytes;
        for (int l = 0; l < level; ++l)
        {
            offset += size;
            size /= 4;
        }
        return offset;
    }
    // Level `level` (at least `first`).
    std::span<const uint8_t> Level(int level) const
    {
        if (level < first)
            return {};
        const size_t offset = Offset(level) - Offset(first), size = static_cast<size_t>(top_bytes) >> (2 * level);
        if (offset + size > data.size())
            return {};
        return {data.data() + offset, size};
    }
};

struct Candidate
{
    std::filesystem::path file;
    std::string name; // normalized texture path
    fx::ReliefClass relief = fx::ReliefClass::None;
    fx::GlossClass gloss = fx::GlossClass::None;
};

std::string Lower(std::string text)
{
    for (char &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// A relief or gloss texture: the rules give it relief or gloss, or its name says "bump" (its pixels decide).
std::optional<Candidate> Classify(const std::filesystem::path &textures, const std::filesystem::path &file)
{
    std::error_code error;
    if (Lower(file.extension().string()) != ".tx")
        return std::nullopt;
    const std::string name = fx::NormalizeTexturePath(std::filesystem::relative(file, textures, error).generic_string());
    if (error)
        return std::nullopt;
    const fx::ReliefVerdict verdict = fx::ClassifyTextureName(name);
    const bool bump = fx::NameSuggestsHeightMap(name) && !(verdict.by_folder && verdict.relief == fx::ReliefClass::None);
    const fx::GlossClass gloss = fx::ClassifyGlossName(name).gloss;
    if (verdict.relief == fx::ReliefClass::None && !bump && gloss == fx::GlossClass::None)
        return std::nullopt;
    return Candidate{file, name, verdict.relief, gloss};
}

std::vector<Candidate> Candidates(const std::filesystem::path &textures)
{
    std::vector<Candidate> list;
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(textures, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
        if (it->is_regular_file(error))
            if (std::optional<Candidate> candidate = Classify(textures, it->path()))
                list.push_back(std::move(*candidate));
    return list;
}

// The texture's key under `method`, or nothing when it gets no prepared map (unknown format, too small for a probe
// level).
std::optional<uint64_t> Key(const Candidate &candidate, const TxFile &tx, uint64_t method, int map_size)
{
    const int probe = fx::ReliefProbeLevel(static_cast<int>(tx.width), static_cast<int>(tx.height), static_cast<int>(tx.levels));
    if (probe < 0)
        return std::nullopt;
    const std::span<const uint8_t> bytes = tx.Level(probe);
    const int pw = static_cast<int>(tx.width >> probe), ph = static_cast<int>(tx.height >> probe);
    if (bytes.size() != texture::d3d_level_bytes(tx.format, pw, ph))
        return std::nullopt;
    return fx::ReliefKey(candidate.name, bytes, pw, ph, method, map_size);
}

std::optional<uint64_t> Key(const Candidate &candidate, const TxFile &tx, int map_size)
{
    return candidate.relief == fx::ReliefClass::None ? std::nullopt
                                                     : Key(candidate, tx, fx::ReliefMethod(candidate.relief), map_size);
}

// The key of the texture's own gloss map: a texture of a gloss class whose relief class has no height (the others
// keep their gloss in their relief's material texture).
std::optional<uint64_t> GlossKey(const Candidate &candidate, const TxFile &tx)
{
    if (candidate.gloss == fx::GlossClass::None || fx::ReliefHasHeight(candidate.relief))
        return std::nullopt;
    return Key(candidate, tx, fx::GlossMethod(candidate.gloss), fx::kGlossMapSize);
}

// Box halvings of an RGBA8 image down to `tw` x `th` (a size NormalMapSize gives: the image halved, or itself).
std::vector<uint8_t> HalveTo(std::vector<uint8_t> rgba, int &w, int &h, int tw, int th)
{
    while (w > tw || h > th)
    {
        const int nw = w > tw ? w / 2 : w, nh = h > th ? h / 2 : h, sx = w / nw, sy = h / nh;
        std::vector<uint8_t> out(static_cast<size_t>(nw) * nh * 4);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
                for (int c = 0; c < 4; ++c)
                {
                    int sum = 0;
                    for (int dy = 0; dy < sy; ++dy)
                        for (int dx = 0; dx < sx; ++dx)
                            sum += rgba[(static_cast<size_t>(y * sy + dy) * w + x * sx + dx) * 4 + c];
                    out[(static_cast<size_t>(y) * nw + x) * 4 + c] = static_cast<uint8_t>((sum + sx * sy / 2) / (sx * sy));
                }
        rgba = std::move(out);
        w = nw;
        h = nh;
    }
    return rgba;
}

// The same for one float plane.
std::vector<float> HalveTo(std::vector<float> v, int w, int h, int tw, int th)
{
    while (w > tw || h > th)
    {
        const int nw = w > tw ? w / 2 : w, nh = h > th ? h / 2 : h, sx = w / nw, sy = h / nh;
        std::vector<float> out(static_cast<size_t>(nw) * nh);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
            {
                float sum = 0.0f;
                for (int dy = 0; dy < sy; ++dy)
                    for (int dx = 0; dx < sx; ++dx)
                        sum += v[static_cast<size_t>(y * sy + dy) * w + x * sx + dx];
                out[static_cast<size_t>(y) * nw + x] = sum / static_cast<float>(sx * sy);
            }
        v = std::move(out);
        w = nw;
        h = nh;
    }
    return v;
}

// A plane of `w` x `h` bilinearly stretched (wrapping: the textures tile) over `tw` x `th`, texel centres aligned.
std::vector<uint8_t> Stretch(std::span<const float> v, int w, int h, int tw, int th)
{
    std::vector<uint8_t> out(static_cast<size_t>(tw) * th);
    for (int y = 0; y < th; ++y)
        for (int x = 0; x < tw; ++x)
        {
            const float fx = (static_cast<float>(x) + 0.5f) * static_cast<float>(w) / static_cast<float>(tw) - 0.5f;
            const float fy = (static_cast<float>(y) + 0.5f) * static_cast<float>(h) / static_cast<float>(th) - 0.5f;
            const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
            const float ax = fx - static_cast<float>(x0), ay = fy - static_cast<float>(y0);
            const auto at = [&](int xi, int yi) {
                return v[static_cast<size_t>(((yi % h) + h) % h) * w + static_cast<size_t>(((xi % w) + w) % w)];
            };
            const float value = (at(x0, y0) * (1 - ax) + at(x0 + 1, y0) * ax) * (1 - ay) +
                                (at(x0, y0 + 1) * (1 - ax) + at(x0 + 1, y0 + 1) * ax) * ay;
            out[static_cast<size_t>(y) * tw + x] = static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        }
    return out;
}

// A grey "bump" texture: a height map the game derives itself (the runtime classifier decides from the same level).
bool GreyHeightMap(const Candidate &candidate, const TxFile &tx)
{
    if (!fx::NameSuggestsHeightMap(candidate.name))
        return false;
    const int probe = fx::ReliefProbeLevel(static_cast<int>(tx.width), static_cast<int>(tx.height), static_cast<int>(tx.levels));
    if (probe < 0)
        return false;
    const uint32_t pw = tx.width >> probe, ph = tx.height >> probe;
    std::vector<uint8_t> rgba(size_t(pw) * ph * 4);
    return texture::decode_rgba8(tx.format, tx.Level(probe).data(), pw, ph, rgba.data()) && fx::IsGrayscale(rgba);
}

// The key of a texture that gets a prepared map, with the class it is prepared as.
std::optional<uint64_t> PreparedKey(const Candidate &candidate, const TxFile &tx, int map_size, fx::ReliefClass &relief)
{
    const std::optional<uint64_t> key = Key(candidate, tx, map_size);
    relief = key && !GreyHeightMap(candidate, tx) ? candidate.relief : fx::ReliefClass::None;
    return relief == fx::ReliefClass::None ? std::nullopt : key;
}

bool OrtOk(const OrtApi *api, OrtStatus *status, std::string &error)
{
    if (!status)
        return true;
    error = std::string("onnxruntime: ") + api->GetErrorMessage(status);
    api->ReleaseStatus(status);
    return false;
}

// onnxruntime's environment for the three models: one thread pool and one memory arena for all of them (they run one
// after the other: their own pools and arenas held three times the threads and the peak memory, ~1.4 GB on a host).
class Runtime
{
  public:
    ~Runtime()
    {
        if (memory_)
            api_->ReleaseMemoryInfo(memory_);
        if (env_)
            api_->ReleaseEnv(env_);
    }

    bool Open(int threads, std::string &error)
    {
        api_ = OrtGetApiBase()->GetApi(ORT_API_VERSION);
        if (!api_)
        {
            error = "onnxruntime does not offer API version " + std::to_string(ORT_API_VERSION);
            return false;
        }
        // `threads` cores; waiting threads sleep instead of spinning: the game's threads share the cores, and spinning
        // threads that wait for each other on an oversubscribed CPU cost many times the work itself.
        OrtThreadingOptions *threading = nullptr;
        bool ok = OrtOk(api_, api_->CreateThreadingOptions(&threading), error) &&
                  OrtOk(api_, api_->SetGlobalIntraOpNumThreads(threading, threads), error) &&
                  OrtOk(api_, api_->SetGlobalInterOpNumThreads(threading, 1), error) &&
                  OrtOk(api_, api_->SetGlobalSpinControl(threading, 0), error) &&
                  OrtOk(api_, api_->CreateEnvWithGlobalThreadPools(ORT_LOGGING_LEVEL_WARNING, "storm-relief", threading, &env_),
                        error);
        if (threading)
            api_->ReleaseThreadingOptions(threading);
        // No usage telemetry (onnxruntime's Android build would try its 1DS client); the shared arena.
        return ok && OrtOk(api_, api_->DisableTelemetryEvents(env_), error) &&
               OrtOk(api_, api_->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory_), error) &&
               OrtOk(api_, api_->CreateAndRegisterAllocator(env_, memory_, nullptr), error);
    }

    const OrtApi *api() const { return api_; }
    OrtEnv *env() const { return env_; }

  private:
    const OrtApi *api_ = nullptr;
    OrtEnv *env_ = nullptr;
    OrtMemoryInfo *memory_ = nullptr;
};

// One of the relief models: one session on the runtime's threads, on each whole texture.
class Model
{
  public:
    ~Model()
    {
        if (!api_)
            return;
        if (session_)
            api_->ReleaseSession(session_);
        if (run_)
            api_->ReleaseRunOptions(run_);
        if (options_)
            api_->ReleaseSessionOptions(options_);
        if (memory_)
            api_->ReleaseMemoryInfo(memory_);
    }

    bool Open(const Runtime &runtime, std::span<const uint8_t> model, int threads, std::string &error)
    {
        api_ = runtime.api();
        // onnxruntime's CPU provider (MLAS kernels, NEON on arm64). Its XNNPACK provider aborts inside onnxruntime
        // 1.31.1 while the session is created (x86_64 emulator, src/libs/relief/README.md); NNAPI is deprecated.
        if (!Ok(api_->CreateSessionOptions(&options_), error) ||
            !Ok(api_->SetSessionGraphOptimizationLevel(options_, ORT_ENABLE_ALL), error) ||
            !Ok(api_->DisablePerSessionThreads(options_), error) ||
            !Ok(api_->AddSessionConfigEntry(options_, "session.use_env_allocators", "1"), error))
            return false;
        provider_ = "CPU, " + std::to_string(threads) + " threads of " + std::to_string(std::thread::hardware_concurrency()) +
                    " cores, no spinning";
        if (!Ok(api_->CreateSessionFromArray(runtime.env(), model.data(), model.size(), options_, &session_), error) ||
            !Ok(api_->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory_), error) ||
            !Ok(api_->CreateRunOptions(&run_), error))
            return false;
        return true;
    }

    // One padded image: three colour planes in, three planes out.
    bool Infer(const float *image, int width, int height, float *planes, std::string &error)
    {
        const int64_t shape[4] = {1, 3, height, width};
        const size_t texels = static_cast<size_t>(width) * height;
        OrtValue *input = nullptr, *output = nullptr;
        bool ok = Ok(api_->CreateTensorWithDataAsOrtValue(memory_, const_cast<float *>(image), texels * 3 * sizeof(float), shape, 4,
                                                          ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input),
                     error);
        const char *in_names[] = {"input"}, *out_names[] = {"output"};
        ok = ok && Ok(api_->Run(session_, run_, in_names, &input, 1, out_names, 1, &output), error);
        float *data = nullptr;
        ok = ok && Ok(api_->GetTensorMutableData(output, reinterpret_cast<void **>(&data)), error);
        if (ok)
            std::memcpy(planes, data, texels * 3 * sizeof(float));
        if (input)
            api_->ReleaseValue(input);
        if (output)
            api_->ReleaseValue(output);
        return ok;
    }

    // Any thread: the running inference stops with an error, and every later one.
    void Cancel()
    {
        if (OrtStatus *status = api_->RunOptionsSetTerminate(run_))
            api_->ReleaseStatus(status);
    }

    const std::string &provider() const { return provider_; }

  private:
    bool Ok(OrtStatus *status, std::string &error)
    {
        if (!status)
            return true;
        error = std::string("onnxruntime: ") + api_->GetErrorMessage(status);
        api_->ReleaseStatus(status);
        return false;
    }

    const OrtApi *api_ = nullptr;
    OrtSessionOptions *options_ = nullptr;
    OrtSession *session_ = nullptr;
    OrtMemoryInfo *memory_ = nullptr;
    OrtRunOptions *run_ = nullptr;
    std::string provider_;
};

} // namespace

long long PeakRssKb()
{
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss;
}

bool ReadTextureLevel(const std::filesystem::path &file, int max_size, std::vector<uint8_t> &rgba, int &width, int &height)
{
    // Only the header and the one level are read (a game file holds every level: up to 4/3 of its top level).
    std::ifstream in(file, std::ios::binary);
    int32_t header[6];
    if (!in.read(reinterpret_cast<char *>(header), sizeof(header)))
        return false;
    const uint32_t flags = static_cast<uint32_t>(header[0]), w = static_cast<uint32_t>(header[1]),
                   h = static_cast<uint32_t>(header[2]), levels = static_cast<uint32_t>(header[3]),
                   format = static_cast<uint32_t>(header[4]), top_bytes = static_cast<uint32_t>(header[5]);
    if ((flags & (kTxFlagPalette | kTxFlagCubemap)) || !texture::tx_format_known(format) || w == 0 || h == 0 || w > 8192 ||
        h > 8192 || levels == 0 || top_bytes != texture::d3d_level_bytes(format, w, h))
        return false;
    // The levels as TxFile::Level lays them out: each a quarter of the one above.
    uint32_t level = 0;
    size_t offset = 0, size = top_bytes;
    while (level + 1 < levels && std::max(w >> level, h >> level) > uint32_t(max_size))
    {
        offset += size;
        size /= 4;
        ++level;
    }
    const uint32_t lw = std::max(1u, w >> level), lh = std::max(1u, h >> level);
    if (size < texture::d3d_level_bytes(format, lw, lh))
        return false;
    std::vector<uint8_t> bytes(size);
    in.seekg(static_cast<std::streamoff>(sizeof(header) + offset));
    if (!in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size)))
        return false;
    rgba.assign(size_t(lw) * lh * 4, 0);
    if (!texture::decode_rgba8(format, bytes.data(), lw, lh, rgba.data()))
        return false;
    width = static_cast<int>(lw);
    height = static_cast<int>(lh);
    return true;
}

std::vector<std::filesystem::path> List(const std::filesystem::path &textures)
{
    std::vector<Candidate> candidates = Candidates(textures);
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.name < b.name; });
    std::vector<std::filesystem::path> files;
    for (const Candidate &candidate : candidates)
        files.push_back(candidate.file);
    return files;
}

struct Session::Impl
{
    Runtime runtime;                 // before the models: released after them
    Model normal, height, roughness; // PBRify's three networks
    Stats stats;
    std::atomic<bool> cancelled{false};
};

Session::Session(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{
}
Session::~Session() = default;

std::unique_ptr<Session> Session::Open(const Models &models, int threads, std::string &error)
{
    auto impl = std::make_unique<Impl>();
    threads = threads > 0 ? threads : static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    if (!impl->runtime.Open(threads, error) || !impl->normal.Open(impl->runtime, models.normal, threads, error) ||
        !impl->height.Open(impl->runtime, models.height, threads, error) ||
        !impl->roughness.Open(impl->runtime, models.roughness, threads, error))
    {
        Log("ERROR: %s", error.c_str());
        return nullptr;
    }
    impl->stats.provider = impl->normal.provider();
    return std::unique_ptr<Session>(new Session(std::move(impl)));
}

void Session::Cancel()
{
    impl_->cancelled = true;
    impl_->normal.Cancel();
    impl_->height.Cancel();
    impl_->roughness.Cancel();
}

const Session::Stats &Session::stats() const
{
    return impl_->stats;
}

Outcome Session::PrepareFile(const std::filesystem::path &textures, const std::filesystem::path &file,
                             const std::filesystem::path &output, int map_size, std::string &failure)
{
    // One decode, then PBRify's networks (the owner's choice): the normals and the height at the map size, the
    // roughness at the gloss map's. Outputs: the relief map (EAC RG11 normals, with the material texture of a class with
    // a height: R the height normalised per region, G the gloss) and, for a texture of a gloss class without a height,
    // its own gloss map (R the metal mask, G the gloss). Each stage is timed: the log line of the texture says where the
    // time went.
    const Clock::time_point start = Clock::now();
    Clock::time_point mark = start;
    const auto lap = [&mark] {
        const Clock::time_point now = Clock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - mark).count();
        mark = now;
        return ms;
    };
    const std::optional<Candidate> candidate = Classify(textures, file);
    TxFile tx;
    fx::ReliefClass relief = fx::ReliefClass::None;
    // From the level the models read (the first no larger than twice the map) on: it and the probe level.
    const auto source_level = [map_size](const TxFile &t) {
        int level = 0;
        while (level + 1 < static_cast<int>(t.levels) && std::max(t.width >> level, t.height >> level) > uint32_t(2 * map_size))
            ++level;
        return level;
    };
    // The keys from the probe level; the models' level only when a map is to be made.
    if (!candidate || !tx.LoadProbe(file))
        return Outcome::Skipped;
    const std::optional<uint64_t> key = PreparedKey(*candidate, tx, map_size, relief);
    const std::optional<uint64_t> gloss_key = GlossKey(*candidate, tx);
    if (!key && !gloss_key)
        return Outcome::Skipped;
    if ((!key || fx::HasReliefMap(output, *key)) && (!gloss_key || fx::HasGlossMap(output, *gloss_key)))
        return Outcome::Existing;
    if (!tx.Load(file, source_level))
        return Outcome::Skipped;
    const double read_ms = lap();
    // The source: the first level no larger than twice the map, which the models' inputs are box-filtered down from.
    const int level = tx.first;
    const uint32_t lw = std::max(1u, tx.width >> level), lh = std::max(1u, tx.height >> level);
    const int width = static_cast<int>(lw), height = static_cast<int>(lh);
    std::vector<uint8_t> rgba(size_t(lw) * lh * 4);
    const std::span<const uint8_t> bytes = tx.Level(level);
    if (bytes.size() < texture::d3d_level_bytes(tx.format, lw, lh) ||
        !texture::decode_rgba8(tx.format, bytes.data(), lw, lh, rgba.data()))
    {
        failure = "unreadable level " + std::to_string(level);
        Log("ERROR: no relief map for %s: %s", candidate->name.c_str(), failure.c_str());
        return Outcome::Failed;
    }
    const double decode_ms = lap();
    // The sizes: the relief map's (map_size) and the gloss's (fx::kGlossMapSize), both halvings of the texture.
    fx::NormalMapOptions options;
    options.max_size = map_size;
    int mw = 0, mh = 0;
    fx::NormalMapSize(width, height, options, mw, mh);
    options.max_size = fx::kGlossMapSize;
    int gw = 0, gh = 0;
    fx::NormalMapSize(width, height, options, gw, gh);
    double infer_ms = 0.0;
    std::string per_model; // each inference: the model, its padded size, its time
    const auto run = [&](Model &model, const std::vector<float> &input, int w, int h) {
        return fx::ReliefModelRun(input, w, h, [&](const float *image, int pw, int ph, float *out) {
            const Clock::time_point infer_start = Clock::now();
            const bool ok = model.Infer(image, pw, ph, out, failure);
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - infer_start).count();
            infer_ms += ms;
            char part[64];
            std::snprintf(part, sizeof(part), "%s%s %dx%d %.0f ms", per_model.empty() ? "" : ", ",
                          &model == &impl_->normal ? "normal" : &model == &impl_->height ? "height" : "roughness", pw, ph, ms);
            per_model += part;
            return ok;
        });
    };
    const bool height_class = key && fx::ReliefHasHeight(relief);
    const bool gloss_wanted = candidate->gloss != fx::GlossClass::None && (height_class || gloss_key);
    bool failed = false;
    std::vector<float> normal_planes, height_planes, gloss_height, roughness_planes;
    if (key)
    {
        const std::vector<float> input = fx::ReliefModelInput(rgba, width, height, mw, mh);
        normal_planes = run(impl_->normal, input, mw, mh);
        failed |= normal_planes.empty();
        if (height_class && !failed)
        {
            height_planes = run(impl_->height, input, mw, mh);
            failed |= height_planes.empty();
        }
    }
    if (gloss_wanted && !failed)
    {
        const std::vector<float> input = fx::ReliefModelInput(rgba, width, height, gw, gh);
        roughness_planes = run(impl_->roughness, input, gw, gh);
        failed |= roughness_planes.empty();
        // The gloss's grooves read the height at its own size: the map's halved, or the height network at the gloss's
        // size when the texture has no relief height.
        if (height_class && !failed)
            gloss_height = HalveTo(fx::ReliefModelHeight(height_planes, mw, mh), mw, mh, gw, gh);
        else if (!failed)
        {
            const std::vector<float> planes = run(impl_->height, input, gw, gh);
            failed |= planes.empty();
            if (!failed)
                gloss_height = fx::ReliefModelHeight(planes, gw, gh);
        }
    }
    impl_->stats.infer_seconds += infer_ms / 1000.0;
    const double model_ms = lap(); // the inputs, padding and crops around the inferences, and the inferences
    if (impl_->cancelled)
    {
        // onnxruntime's error for a stopped inference varies (and its output, if any, is not the model's).
        failure = "cancelled";
        Log("relief %s: cancelled", candidate->name.c_str());
        return Outcome::Failed;
    }
    if (failed)
    {
        Log("ERROR: no relief map for %s: %s", candidate->name.c_str(), failure.c_str());
        return Outcome::Failed;
    }
    fx::GlossField gloss;
    if (gloss_wanted)
    {
        int cw = width, ch = height;
        const std::vector<uint8_t> colour = HalveTo(rgba, cw, ch, gw, gh);
        gloss = fx::GlossOfRoughness(fx::ReliefModelHeight(roughness_planes, gw, gh), gloss_height, colour, gw, gh,
                                     candidate->gloss);
    }
    const double gloss_ms = lap();
    fx::NormalMap map;
    if (key)
    {
        fx::NormalMap rg8 = fx::NormalMapOfNormals(fx::ReliefModelNormals(normal_planes, mw, mh), mw, mh);
        if (height_class)
        {
            // The material's G: the gloss stretched over the map (empty: no gloss class, G 0).
            const std::vector<uint8_t> g = gloss.gloss.empty() ? std::vector<uint8_t>{} : Stretch(gloss.gloss, gw, gh, mw, mh);
            if (!fx::AddReliefMaterial(rg8, relief, fx::ReliefModelHeight(height_planes, mw, mh), g))
                Log("relief %s: flat, no height", candidate->name.c_str());
        }
        map = {rg8.width, rg8.height, rg8.mip_count, fx::NormalMapFormat::EacRg11,
               texture::encode_eac_chain(texture::EacFormat::Rg11, rg8.rg.data(), rg8.width, rg8.height, rg8.mip_count)};
        if (!rg8.material.empty())
        {
            map.material = texture::encode_eac_chain(texture::EacFormat::Rg11, rg8.material.data(), rg8.width, rg8.height,
                                                     rg8.mip_count);
            map.depth = rg8.depth;
        }
        if (!fx::WriteReliefMap(output, *key, map))
        {
            failure = "cannot write " + fx::ReliefFile(output, *key).string();
            Log("ERROR: no relief map for %s: %s", candidate->name.c_str(), failure.c_str());
            return Outcome::Failed;
        }
    }
    if (gloss_key)
    {
        const fx::NormalMap rg = fx::GlossMaterial(gloss);
        const fx::NormalMap stored{rg.width, rg.height, rg.mip_count, fx::NormalMapFormat::EacRg11,
                                   texture::encode_eac_chain(texture::EacFormat::Rg11, rg.rg.data(), rg.width, rg.height,
                                                             rg.mip_count)};
        if (rg.rg.empty() || !fx::WriteGlossMap(output, *gloss_key, stored))
        {
            failure = "cannot write " + fx::GlossFile(output, *gloss_key).string();
            Log("ERROR: no gloss map for %s: %s", candidate->name.c_str(), failure.c_str());
            return Outcome::Failed;
        }
    }
    const double write_ms = lap();
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    ++impl_->stats.prepared;
    impl_->stats.max_texture_ms = std::max(impl_->stats.max_texture_ms, ms);
    double mean_gloss = 0.0;
    for (const float g : gloss.gloss)
        mean_gloss += g;
    mean_gloss = gloss.gloss.empty() ? 0.0 : mean_gloss / static_cast<double>(gloss.gloss.size());
    Log("relief %s: %dx%d%s, gloss %s %dx%d (mean %.2f), %.0f ms (read %.0f, decode %.0f, models %.0f of which "
        "inference %.0f: %s; gloss %.0f, mips, material, encode and write %.0f; depth %.1f texels; %s)",
        candidate->name.c_str(), key ? mw : 0, key ? mh : 0, key ? "" : " (no relief)", fx::GlossClassName(candidate->gloss),
        gloss.width, gloss.height, mean_gloss, ms, read_ms, decode_ms, model_ms, infer_ms, per_model.c_str(), gloss_ms,
        write_ms, static_cast<double>(map.depth), impl_->stats.provider.c_str());
    return Outcome::Prepared;
}

int Prune(const std::filesystem::path &textures, const std::vector<std::filesystem::path> &files,
          const std::filesystem::path &output, int map_size)
{
    std::vector<std::filesystem::path> current;
    for (const std::filesystem::path &file : files)
    {
        const std::optional<Candidate> found = Classify(textures, file);
        if (!found)
            continue;
        const Candidate &candidate = *found;
        TxFile tx;
        if (!tx.LoadProbe(candidate.file))
            continue;
        fx::ReliefClass relief;
        // A texture's own gloss map (fx/gloss_map.hpp): a gloss class without a relief height.
        if (const std::optional<uint64_t> gloss_key = GlossKey(candidate, tx))
            current.push_back(fx::GlossFile(output, *gloss_key));
        if (const std::optional<uint64_t> key = PreparedKey(candidate, tx, map_size, relief))
            current.push_back(fx::ReliefFile(output, *key));
        else if (GreyHeightMap(candidate, tx))
        {
            // The game's own map of a grey "bump" texture, under the height maps' method: kept while it is current.
            Candidate height = candidate;
            height.relief = fx::ReliefClass::HeightMap;
            if (const std::optional<uint64_t> height_key = Key(height, tx, fx::kReliefMapSize))
                current.push_back(fx::ReliefFile(output, *height_key));
        }
    }
    std::sort(current.begin(), current.end());
    int removed = 0;
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(output, error))
        if ((entry.path().extension() == ".relief" || entry.path().extension() == ".gloss") &&
            !std::binary_search(current.begin(), current.end(), entry.path()))
        {
            std::filesystem::remove(entry.path(), error);
            ++removed;
        }
    return removed;
}

namespace
{
std::string CompleteMark(size_t candidates, int map_size)
{
    // Every class's method (model, options, versions): another build's maps never count as complete.
    uint64_t methods = 0;
    for (const fx::ReliefClass relief : {fx::ReliefClass::Stone, fx::ReliefClass::Wood, fx::ReliefClass::Building,
                                         fx::ReliefClass::Plaster, fx::ReliefClass::Metal, fx::ReliefClass::Prop,
                                         fx::ReliefClass::HeightMap})
        methods = (methods ^ fx::ReliefMethod(relief)) * 0x100000001b3ull;
    for (int gloss = 1; gloss <= static_cast<int>(fx::kLastGlossClass); ++gloss)
        methods = (methods ^ fx::GlossMethod(static_cast<fx::GlossClass>(gloss))) * 0x100000001b3ull;
    char hex[24];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(methods));
    return std::string("method ") + hex + " size " + std::to_string(map_size) + " textures " +
           std::to_string(candidates) + "\n";
}
std::filesystem::path CompleteFile(const std::filesystem::path &output)
{
    return output / "complete.txt";
}

int CountMaps(const std::filesystem::path &output)
{
    int maps = 0;
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(output, error))
        maps += entry.path().extension() == ".relief" || entry.path().extension() == ".gloss" ? 1 : 0;
    return maps;
}
} // namespace

void MarkComplete(const std::filesystem::path &output, int map_size, size_t candidates)
{
    std::error_code error;
    std::filesystem::create_directories(output, error);
    std::ofstream(CompleteFile(output), std::ios::trunc) << CompleteMark(candidates, map_size) << "maps " << CountMaps(output)
                                                         << "\n";
}

bool Complete(const std::filesystem::path &output, int map_size, size_t candidates, int *missing)
{
    if (missing)
        *missing = 0;
    std::ifstream in(CompleteFile(output));
    std::string method, maps;
    if (!std::getline(in, method) || !std::getline(in, maps) || method + "\n" != CompleteMark(candidates, map_size) ||
        maps.rfind("maps ", 0) != 0)
        return false;
    // The system may evict a cache: maps of a complete pass gone since are made again.
    const int marked = std::atoi(maps.c_str() + 5), present = CountMaps(output);
    if (present >= marked)
        return true;
    if (missing)
        *missing = marked - present;
    return false;
}

std::string Result::Json() const
{
    char text[640];
    std::snprintf(text, sizeof(text),
                  "{\"status\":%d,\"total\":%d,\"prepared\":%d,\"existing\":%d,\"failed\":%d,\"skipped\":%d,\"removed\":%d,"
                  "\"seconds\":%.1f,\"infer_seconds\":%.1f,\"max_texture_ms\":%.0f,\"peak_rss_kb\":%lld,\"provider\":\"%s\"}",
                  status, total, prepared, existing, failed, skipped, removed, seconds, infer_seconds, max_texture_ms, peak_rss_kb,
                  provider.c_str());
    std::string json = text;
    if (!error.empty())
    {
        json.pop_back();
        json += ",\"error\":\"";
        for (char c : error)
            json += (c == '"' || c == '\\') ? ' ' : c;
        json += "\"}";
    }
    return json;
}

Result Prepare(const Job &job, const std::function<bool(const Progress &)> &progress)
{
    const Clock::time_point start = Clock::now();
    Result result;
    std::unique_ptr<Session> session = Session::Open(job.models, job.threads, result.error);
    if (!session)
    {
        result.status = 2;
        return result;
    }
    result.provider = session->stats().provider;
    const std::vector<std::filesystem::path> files = List(job.textures);
    Progress state;
    state.total = result.total = static_cast<int>(files.size());
    Log("relief preparation: %d candidate textures in %s, maps up to %d px into %s, onnxruntime %s", state.total,
        job.textures.string().c_str(), job.map_size, job.output.string().c_str(), result.provider.c_str());
    if (progress && progress(state))
        result.status = 1;
    for (const std::filesystem::path &file : files)
    {
        if (result.status != 0)
            break;
        state.current = file.filename().string();
        std::string failure;
        switch (session->PrepareFile(job.textures, file, job.output, job.map_size, failure))
        {
        case Outcome::Prepared: ++result.prepared; break;
        case Outcome::Existing: ++result.existing; break;
        case Outcome::Skipped: ++result.skipped; break;
        case Outcome::Failed: ++result.failed; break;
        }
        ++state.done;
        if (progress && progress(state))
            result.status = 1;
    }
    if (result.status == 0)
    {
        result.removed = Prune(job.textures, files, job.output, job.map_size);
        MarkComplete(job.output, job.map_size, files.size());
    }
    result.seconds = Seconds(start);
    result.infer_seconds = session->stats().infer_seconds;
    result.max_texture_ms = session->stats().max_texture_ms;
    result.peak_rss_kb = PeakRssKb();
    Log("relief preparation %s: %d textures, %d prepared, %d already there, %d without relief, %d failed, %d stale maps "
        "removed; %.1f s (%.1f s inference, slowest texture %.0f ms), peak RSS %lld MB",
        result.status == 1 ? "cancelled" : "done", result.total, result.prepared, result.existing, result.skipped,
        result.failed, result.removed, result.seconds, result.infer_seconds, result.max_texture_ms, result.peak_rss_kb / 1024);
    return result;
}

} // namespace storm::relief
