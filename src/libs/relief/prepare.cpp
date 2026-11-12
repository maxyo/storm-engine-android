#include "relief.hpp"

#include "formats.hpp"
#include "storm/fx/deepbump.hpp"
#include "storm/fx/normal_map.hpp"
#include "storm/fx/relief_store.hpp"

#include <onnxruntime_c_api.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
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
            width > 8192 || height > 8192 || levels == 0 || top_bytes != texture::tx_level_bytes(format, width, height))
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

// Every texture the rules give relief, plus the "bump" names (their pixels decide).
std::vector<Candidate> ListCandidates(const std::filesystem::path &textures)
{
    std::vector<Candidate> list;
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(textures, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
    {
        if (!it->is_regular_file(error) || Lower(it->path().extension().string()) != ".tx")
            continue;
        const std::string name = fx::NormalizeTexturePath(std::filesystem::relative(it->path(), textures, error).generic_string());
        const fx::ReliefVerdict verdict = fx::ClassifyTextureName(name);
        const bool bump = fx::NameSuggestsHeightMap(name) && !(verdict.by_folder && verdict.relief == fx::ReliefClass::None);
        if (verdict.relief != fx::ReliefClass::None || bump)
            list.push_back({it->path(), name, verdict.relief});
    }
    std::sort(list.begin(), list.end(), [](const Candidate &a, const Candidate &b) { return a.name < b.name; });
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
    if (bytes.size() != texture::tx_level_bytes(tx.format, pw, ph))
        return std::nullopt;
    return fx::ReliefKey(candidate.name, bytes, pw, ph, fx::kReliefMethodVersion, map_size);
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
    return texture::decode_tx_level(tx.format, tx.Level(probe).data(), pw, ph, rgba.data()) && fx::IsGrayscale(rgba);
}

// onnxruntime with the DeepBump model: one session, every core on its tiles.
class Model
{
  public:
    ~Model()
    {
        if (!api_)
            return;
        if (session_)
            api_->ReleaseSession(session_);
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
        // onnxruntime's CPU provider (MLAS kernels, NEON on arm64) on every core. Its XNNPACK provider aborts inside
        // onnxruntime 1.31.1 while the session is created (x86_64 emulator, src/libs/relief/README.md); NNAPI is
        // deprecated.
        if (!Ok(api_->SetIntraOpNumThreads(options_, threads), error))
            return false;
        provider_ = "CPU, " + std::to_string(threads) + " threads";
        if (!Ok(api_->CreateSessionFromArray(env_, model.data(), model.size(), options_, &session_), error) ||
            !Ok(api_->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory_), error))
            return false;
        return true;
    }

    // One tile: grey in, planar normals out.
    bool Infer(const float *tile, float *normals, std::string &error)
    {
        const int64_t shape[4] = {1, 1, fx::kDeepBumpTile, fx::kDeepBumpTile};
        constexpr size_t texels = static_cast<size_t>(fx::kDeepBumpTile) * fx::kDeepBumpTile;
        OrtValue *input = nullptr, *output = nullptr;
        bool ok = Ok(api_->CreateTensorWithDataAsOrtValue(memory_, const_cast<float *>(tile), texels * sizeof(float), shape, 4,
                                                          ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input),
                     error);
        const char *in_names[] = {"input"}, *out_names[] = {"output"};
        ok = ok && Ok(api_->Run(session_, nullptr, in_names, &input, 1, out_names, 1, &output), error);
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
    std::string provider_;
};

// One texture's map: DeepBump at the map size, scaled to our own derivation's RMS tilt (the owner's "C at 100 %"),
// EAC RG11, written to the relief directory.
bool PrepareTexture(const TxFile &tx, fx::ReliefClass relief, uint64_t key, const Job &job, Model &model, Result &result,
                    std::string &failure)
{
    const int width = static_cast<int>(tx.width), height = static_cast<int>(tx.height);
    std::vector<uint8_t> rgba(size_t(tx.width) * tx.height * 4);
    texture::decode_tx_level(tx.format, tx.data.data(), tx.width, tx.height, rgba.data());
    fx::NormalMapOptions options = fx::ReliefOptions(relief);
    options.max_size = job.map_size;
    int mw = 0, mh = 0;
    fx::NormalMapSize(width, height, options, mw, mh);
    // The strength: our own derivation's RMS tilt at its reference size (1024), whatever size the map is made at.
    fx::NormalMapOptions reference = options;
    reference.max_size = fx::kNormalMapReferenceSize;
    const float target = fx::TiltRms(fx::DeriveNormalMap(rgba, width, height, reference));
    const std::vector<float> grey = fx::DeepBumpInput(rgba, width, height, mw, mh);
    const Clock::time_point infer_start = Clock::now();
    const std::vector<float> normals = fx::DeepBumpNormals(grey, mw, mh, [&](const float *tile, float *out) {
        ++result.tiles;
        return model.Infer(tile, out, failure);
    });
    result.infer_seconds += Seconds(infer_start);
    if (normals.empty())
        return false;
    const fx::NormalMap rg8 = fx::DeepBumpNormalMap(normals, mw, mh, target);
    const fx::NormalMap map{rg8.width, rg8.height, rg8.mip_count, fx::NormalMapFormat::EacRg11,
                            texture::encode_eac_rg11_chain(rg8.rg.data(), rg8.width, rg8.height, rg8.mip_count)};
    if (!fx::WriteReliefMap(job.output, key, map))
    {
        failure = "cannot write " + fx::ReliefFile(job.output, key).string();
        return false;
    }
    return true;
}

long long PeakRssKb()
{
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss;
}
} // namespace

std::string Result::Json() const
{
    char text[640];
    std::snprintf(text, sizeof(text),
                  "{\"status\":%d,\"total\":%d,\"prepared\":%d,\"existing\":%d,\"failed\":%d,\"skipped\":%d,\"removed\":%d,\"tiles\":%lld,"
                  "\"seconds\":%.1f,\"infer_seconds\":%.1f,\"max_texture_ms\":%.0f,\"peak_rss_kb\":%lld,\"provider\":\"%s\"}",
                  status, total, prepared, existing, failed, skipped, removed, tiles, seconds, infer_seconds, max_texture_ms, peak_rss_kb,
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

Status Check(const std::filesystem::path &textures, const std::filesystem::path &output, int map_size)
{
    Status status;
    for (const Candidate &candidate : ListCandidates(textures))
    {
        TxFile tx;
        if (!tx.Load(candidate.file))
            continue;
        const std::optional<uint64_t> key = Key(candidate, tx, map_size);
        if (!key)
            continue;
        if (GreyHeightMap(candidate, tx) || candidate.relief == fx::ReliefClass::None)
            continue;
        ++status.total;
        status.missing += fx::HasReliefMap(output, *key) ? 0 : 1;
    }
    return status;
}

Result Prepare(const Job &job, const std::function<bool(const Progress &)> &progress)
{
    const Clock::time_point start = Clock::now();
    Result result;
    const int threads = job.threads > 0 ? job.threads : std::max(1u, std::thread::hardware_concurrency());
    Model model;
    if (!model.Open(job.model, threads, result.error))
    {
        result.status = 2;
        Log("ERROR: %s", result.error.c_str());
        return result;
    }
    result.provider = model.provider();
    std::error_code error;
    std::filesystem::create_directories(job.output, error);
    if (error)
    {
        result.status = 2;
        result.error = "cannot create " + job.output.string() + ": " + error.message();
        Log("ERROR: %s", result.error.c_str());
        return result;
    }

    const std::vector<Candidate> candidates = ListCandidates(job.textures);
    Progress state;
    state.total = static_cast<int>(candidates.size());
    Log("relief preparation: %d candidate textures in %s, maps up to %d px into %s, onnxruntime %s", state.total,
        job.textures.string().c_str(), job.map_size, job.output.string().c_str(), result.provider.c_str());
    if (progress && progress(state))
        result.status = 1;

    std::vector<std::filesystem::path> current; // the maps of this game: anything else in the directory is stale
    for (const Candidate &candidate : candidates)
    {
        if (result.status != 0)
            break;
        const Clock::time_point texture_start = Clock::now();
        state.current = candidate.name;
        TxFile tx;
        const std::optional<uint64_t> key = tx.Load(candidate.file) ? Key(candidate, tx, job.map_size) : std::nullopt;
        if (key)
            current.push_back(fx::ReliefFile(job.output, *key));
        const fx::ReliefClass relief = key && !GreyHeightMap(candidate, tx) ? candidate.relief : fx::ReliefClass::None;
        if (relief == fx::ReliefClass::None)
            ++result.skipped; // unknown format, too small, a grey height map (the game derives it) or a "bump" without a rule
        else if (fx::HasReliefMap(job.output, *key))
            ++result.existing;
        else if (std::string failure; !PrepareTexture(tx, relief, *key, job, model, result, failure))
        {
            ++result.failed;
            Log("ERROR: no relief map for %s: %s", candidate.name.c_str(), failure.c_str());
        }
        else
        {
            ++result.prepared;
            const double ms = Seconds(texture_start) * 1000.0;
            result.max_texture_ms = std::max(result.max_texture_ms, ms);
            Log("relief %s: %.0f ms", candidate.name.c_str(), ms);
        }
        ++state.done;
        if (progress && progress(state))
            result.status = 1;
    }
    result.total = state.total;
    if (result.status == 0)
    {
        // A complete run: maps of textures this game no longer has (another game, an older method) are removed. The
        // game's own height maps go too; it derives them again when it needs them.
        std::sort(current.begin(), current.end());
        for (const auto &entry : std::filesystem::directory_iterator(job.output, error))
            if (entry.path().extension() == ".relief" && !std::binary_search(current.begin(), current.end(), entry.path()))
            {
                std::filesystem::remove(entry.path(), error);
                ++result.removed;
            }
    }
    result.seconds = Seconds(start);
    result.peak_rss_kb = PeakRssKb();
    Log("relief preparation %s: %d textures, %d prepared, %d already there, %d without relief, %d failed, %d stale maps "
        "removed; %.1f s "
        "(%.1f s inference, %lld tiles, slowest texture %.0f ms), peak RSS %lld MB",
        result.status == 1 ? "cancelled" : "done", result.total, result.prepared, result.existing, result.skipped,
        result.failed, result.removed, result.seconds, result.infer_seconds, result.tiles, result.max_texture_ms, result.peak_rss_kb / 1024);
    return result;
}

} // namespace storm::relief
