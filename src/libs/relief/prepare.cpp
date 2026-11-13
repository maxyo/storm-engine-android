#include "relief.hpp"

#include "formats.hpp"
#include "storm/fx/deepbump.hpp"
#include "storm/fx/normal_map.hpp"
#include "storm/fx/relief_store.hpp"

#include <onnxruntime_c_api.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
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

// A game texture file: the header of the .tx format and its levels in the order the renderer reads them.
struct TxFile
{
    uint32_t flags = 0, width = 0, height = 0, levels = 0, format = 0, top_bytes = 0;
    std::vector<uint8_t> data; // all levels

    bool Load(const std::filesystem::path &path)
    {
        std::ifstream in(path, std::ios::binary);
        int32_t header[6];
        if (!in.read(reinterpret_cast<char *>(header), sizeof(header)))
            return false;
        flags = static_cast<uint32_t>(header[0]);
        width = static_cast<uint32_t>(header[1]);
        height = static_cast<uint32_t>(header[2]);
        levels = static_cast<uint32_t>(header[3]);
        format = static_cast<uint32_t>(header[4]);
        top_bytes = static_cast<uint32_t>(header[5]);
        if ((flags & (kTxFlagPalette | kTxFlagCubemap)) || !texture::tx_format_known(format) || width == 0 || height == 0 ||
            width > 8192 || height > 8192 || levels == 0 || top_bytes != texture::d3d_level_bytes(format, width, height))
            return false;
        data.assign(std::istreambuf_iterator<char>(in), {});
        return data.size() >= top_bytes;
    }
    // Level `level` as the renderer reads it (each level a quarter of the one above).
    std::span<const uint8_t> Level(int level) const
    {
        size_t offset = 0, size = top_bytes;
        for (int l = 0; l < level; ++l)
        {
            offset += size;
            size /= 4;
        }
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
};

std::string Lower(std::string text)
{
    for (char &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// A relief texture: the rules give it relief, or its name says "bump" (its pixels decide).
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
    if (verdict.relief == fx::ReliefClass::None && !bump)
        return std::nullopt;
    return Candidate{file, name, verdict.relief};
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

// The texture's key, or nothing when it gets no prepared map (unknown format, too small for a probe level).
std::optional<uint64_t> Key(const Candidate &candidate, const TxFile &tx, int map_size)
{
    const int probe = fx::ReliefProbeLevel(static_cast<int>(tx.width), static_cast<int>(tx.height), static_cast<int>(tx.levels));
    if (probe < 0)
        return std::nullopt;
    const std::span<const uint8_t> bytes = tx.Level(probe);
    const int pw = static_cast<int>(tx.width >> probe), ph = static_cast<int>(tx.height >> probe);
    if (bytes.size() != texture::d3d_level_bytes(tx.format, pw, ph))
        return std::nullopt;
    return fx::ReliefKey(candidate.name, bytes, pw, ph, fx::ReliefMethod(candidate.relief), map_size);
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

// onnxruntime with the DeepBump model: one session, its threads on each whole texture.
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
        if (env_)
            api_->ReleaseEnv(env_);
    }

    bool Open(std::span<const uint8_t> model, int threads, std::string &error)
    {
        api_ = OrtGetApiBase()->GetApi(ORT_API_VERSION);
        if (!api_)
            return Fail("onnxruntime does not offer API version " + std::to_string(ORT_API_VERSION), error);
        // No usage telemetry (onnxruntime's Android build would try its 1DS client).
        if (!Ok(api_->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "storm-relief", &env_), error) ||
            !Ok(api_->DisableTelemetryEvents(env_), error) ||
            !Ok(api_->CreateSessionOptions(&options_), error) ||
            !Ok(api_->SetSessionGraphOptimizationLevel(options_, ORT_ENABLE_ALL), error))
            return false;
        // onnxruntime's CPU provider (MLAS kernels, NEON on arm64) on `threads` cores. Its XNNPACK provider aborts inside
        // onnxruntime 1.31.1 while the session is created (x86_64 emulator, src/libs/relief/README.md); NNAPI is
        // deprecated.
        // Waiting threads sleep instead of spinning: the game's threads share the cores, and spinning threads that
        // wait for each other on an oversubscribed CPU cost many times the work itself.
        if (!Ok(api_->SetIntraOpNumThreads(options_, threads), error) ||
            !Ok(api_->AddSessionConfigEntry(options_, "session.intra_op.allow_spinning", "0"), error))
            return false;
        provider_ = "CPU, " + std::to_string(threads) + " threads of " + std::to_string(std::thread::hardware_concurrency()) +
                    " cores, no spinning";
        if (!Ok(api_->CreateSessionFromArray(env_, model.data(), model.size(), options_, &session_), error) ||
            !Ok(api_->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory_), error) ||
            !Ok(api_->CreateRunOptions(&run_), error))
            return false;
        return true;
    }

    // One padded image: grey in, planar normals out.
    bool Infer(const float *image, int width, int height, float *normals, std::string &error)
    {
        const int64_t shape[4] = {1, 1, height, width};
        const size_t texels = static_cast<size_t>(width) * height;
        OrtValue *input = nullptr, *output = nullptr;
        bool ok = Ok(api_->CreateTensorWithDataAsOrtValue(memory_, const_cast<float *>(image), texels * sizeof(float), shape, 4,
                                                          ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input),
                     error);
        const char *in_names[] = {"input"}, *out_names[] = {"output"};
        ok = ok && Ok(api_->Run(session_, run_, in_names, &input, 1, out_names, 1, &output), error);
        float *data = nullptr;
        ok = ok && Ok(api_->GetTensorMutableData(output, reinterpret_cast<void **>(&data)), error);
        if (ok)
            std::memcpy(normals, data, texels * 3 * sizeof(float));
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
    static bool Fail(const std::string &message, std::string &error)
    {
        error = message;
        return false;
    }

    const OrtApi *api_ = nullptr;
    OrtEnv *env_ = nullptr;
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
    TxFile tx;
    if (!tx.Load(file))
        return false;
    int level = 0;
    while (level + 1 < static_cast<int>(tx.levels) && std::max(tx.width >> level, tx.height >> level) > uint32_t(max_size))
        ++level;
    const uint32_t lw = std::max(1u, tx.width >> level), lh = std::max(1u, tx.height >> level);
    const std::span<const uint8_t> bytes = tx.Level(level);
    rgba.assign(size_t(lw) * lh * 4, 0);
    if (bytes.size() < texture::d3d_level_bytes(tx.format, lw, lh) ||
        !texture::decode_rgba8(tx.format, bytes.data(), lw, lh, rgba.data()))
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
    Model model;
    Stats stats;
    std::atomic<bool> cancelled{false};
};

Session::Session(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{
}
Session::~Session() = default;

std::unique_ptr<Session> Session::Open(std::span<const uint8_t> model, int threads, std::string &error)
{
    auto impl = std::make_unique<Impl>();
    threads = threads > 0 ? threads : static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    if (!impl->model.Open(model, threads, error))
    {
        Log("ERROR: %s", error.c_str());
        return nullptr;
    }
    impl->stats.provider = impl->model.provider();
    return std::unique_ptr<Session>(new Session(std::move(impl)));
}

void Session::Cancel()
{
    impl_->cancelled = true;
    impl_->model.Cancel();
}

const Session::Stats &Session::stats() const
{
    return impl_->stats;
}

Outcome Session::PrepareFile(const std::filesystem::path &textures, const std::filesystem::path &file,
                             const std::filesystem::path &output, int map_size, std::string &failure)
{
    // DeepBump at the map size, its normals as the model gives them (the owner's choice), EAC RG11. Each stage is timed:
    // the log line of the texture says where the time went.
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
    const std::optional<uint64_t> key =
        candidate && tx.Load(file) ? PreparedKey(*candidate, tx, map_size, relief) : std::nullopt;
    if (!key)
        return Outcome::Skipped;
    if (fx::HasReliefMap(output, *key))
        return Outcome::Existing;
    const double read_ms = lap();
    // The source: the first level no larger than twice the map, which the model's input is box-filtered down from.
    int level = 0;
    while (level + 1 < static_cast<int>(tx.levels) &&
           std::max(tx.width >> level, tx.height >> level) > uint32_t(2 * map_size))
        ++level;
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
    fx::NormalMapOptions options;
    options.max_size = map_size;
    int mw = 0, mh = 0;
    fx::NormalMapSize(width, height, options, mw, mh);
    const std::vector<float> grey = fx::DeepBumpInput(rgba, width, height, mw, mh);
    double infer_ms = 0.0;
    const std::vector<float> normals = fx::DeepBumpNormals(grey, mw, mh, [&](const float *image, int pw, int ph, float *out) {
        const Clock::time_point infer_start = Clock::now();
        const bool ok = impl_->model.Infer(image, pw, ph, out, failure);
        infer_ms = std::chrono::duration<double, std::milli>(Clock::now() - infer_start).count();
        return ok;
    });
    impl_->stats.infer_seconds += infer_ms / 1000.0;
    const double model_ms = lap(); // the input, padding and crop around the inference, and the inference
    if (impl_->cancelled)
    {
        // onnxruntime's error for a stopped inference varies (and its output, if any, is not the model's).
        failure = "cancelled";
        Log("relief %s: cancelled", candidate->name.c_str());
        return Outcome::Failed;
    }
    if (normals.empty())
    {
        Log("ERROR: no relief map for %s: %s", candidate->name.c_str(), failure.c_str());
        return Outcome::Failed;
    }
    const fx::NormalMap rg8 = fx::DeepBumpNormalMap(normals, mw, mh);
    const double mips_ms = lap();
    const fx::NormalMap map{rg8.width, rg8.height, rg8.mip_count, fx::NormalMapFormat::EacRg11,
                            texture::encode_eac_chain(texture::EacFormat::Rg11, rg8.rg.data(), rg8.width, rg8.height, rg8.mip_count)};
    const double encode_ms = lap();
    if (!fx::WriteReliefMap(output, *key, map))
    {
        failure = "cannot write " + fx::ReliefFile(output, *key).string();
        Log("ERROR: no relief map for %s: %s", candidate->name.c_str(), failure.c_str());
        return Outcome::Failed;
    }
    const double write_ms = lap();
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    ++impl_->stats.prepared;
    impl_->stats.max_texture_ms = std::max(impl_->stats.max_texture_ms, ms);
    Log("relief %s: %dx%d, %.0f ms (read %.0f, decode %.0f, model %.0f of which inference %.0f, "
        "mips %.0f, encode %.0f, write %.0f)",
        candidate->name.c_str(), mw, mh, ms, read_ms, decode_ms, model_ms, infer_ms, mips_ms, encode_ms,
        write_ms);
    return Outcome::Prepared;
}

int Prune(const std::filesystem::path &textures, const std::filesystem::path &output, int map_size)
{
    std::vector<std::filesystem::path> current;
    for (const Candidate &candidate : Candidates(textures))
    {
        TxFile tx;
        if (!tx.Load(candidate.file))
            continue;
        fx::ReliefClass relief;
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
        if (entry.path().extension() == ".relief" && !std::binary_search(current.begin(), current.end(), entry.path()))
        {
            std::filesystem::remove(entry.path(), error);
            ++removed;
        }
    return removed;
}

namespace
{
std::string CompleteMark(const std::filesystem::path &textures, int map_size)
{
    // Every class's method (model, options, versions): another build's maps never count as complete.
    uint64_t methods = 0;
    for (const fx::ReliefClass relief : {fx::ReliefClass::Stone, fx::ReliefClass::Wood, fx::ReliefClass::Building,
                                         fx::ReliefClass::Plaster, fx::ReliefClass::HeightMap})
        methods = (methods ^ fx::ReliefMethod(relief)) * 0x100000001b3ull;
    char hex[24];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(methods));
    return std::string("method ") + hex + " size " + std::to_string(map_size) + " textures " +
           std::to_string(Candidates(textures).size()) + "\n";
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
        maps += entry.path().extension() == ".relief" ? 1 : 0;
    return maps;
}
} // namespace

void MarkComplete(const std::filesystem::path &textures, const std::filesystem::path &output, int map_size)
{
    std::error_code error;
    std::filesystem::create_directories(output, error);
    std::ofstream(CompleteFile(output), std::ios::trunc) << CompleteMark(textures, map_size) << "maps " << CountMaps(output)
                                                         << "\n";
}

bool Complete(const std::filesystem::path &textures, const std::filesystem::path &output, int map_size, int *missing)
{
    if (missing)
        *missing = 0;
    std::ifstream in(CompleteFile(output));
    std::string method, maps;
    if (!std::getline(in, method) || !std::getline(in, maps) || method + "\n" != CompleteMark(textures, map_size) ||
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
    std::unique_ptr<Session> session = Session::Open(job.model, job.threads, result.error);
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
        result.removed = Prune(job.textures, job.output, job.map_size);
        MarkComplete(job.textures, job.output, job.map_size);
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
