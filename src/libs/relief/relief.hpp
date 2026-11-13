// Relief maps from PBRify's normal, height and roughness models (fx/relief_model.hpp) on the GPU (ncnn) or the CPU
// (onnxruntime), for the game's stone,
// wood, building, plaster and ground textures, made from the player's game files into the relief directory (fx/relief_store.hpp) the game reads.
// The game makes them while it runs (renderer_bgfx effects/relief_generator.hpp: Session, List, Prune, MarkComplete);
// the host tool makes a whole game at once (Prepare). Contract: README.md here.
#pragma once

#include "storm/fx/relief_store.hpp"
#include "storm/fx/upscale.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace storm::relief
{

// The relief textures of a game (its RESOURCE/Textures), by name. Only names are read (a scan of the models for the
// most used ones took minutes on a phone's storage, in the way of the textures on screen).
std::vector<std::filesystem::path> List(const std::filesystem::path &textures);

enum class Outcome
{
    Prepared,
    Existing, // its map is there already
    Skipped,  // no prepared map for it: unknown format, too small, a grey height map (the game derives it), no rule
    Failed,   // logged with the reason
};

// The model files (model/README.md): the int8 models for the CPU and their ncnn graphs for the GPU (empty: the CPU).
struct Models
{
    std::span<const uint8_t> normal;    // pbrify-normal-int8.onnx
    std::span<const uint8_t> height;    // pbrify-height-int8.onnx
    std::span<const uint8_t> roughness; // pbrify-roughness-int8.onnx
    std::span<const uint8_t> normal_param, normal_bin;       // pbrify-normal.ncnn.param, .bin
    std::span<const uint8_t> height_param, height_bin;       // pbrify-height.ncnn.param, .bin
    std::span<const uint8_t> roughness_param, roughness_bin; // pbrify-roughness.ncnn.param, .bin
    std::span<const uint8_t> upscale;                        // pbrify-upscale-int8.onnx (fx/upscale.hpp)
    std::span<const uint8_t> upscale_param, upscale_bin;     // pbrify-upscale.ncnn.param, .bin
};

// Where the game's ×2 textures go (fx/upscale.hpp: the upscale directory) and the GPU tier they are encoded for.
struct UpscaleOutput
{
    std::filesystem::path directory;
    bool bc = false; // BC1/BC3 (GPUs with S3TC), else ETC2
};
// The model files' names, in Models' order.
inline constexpr const char *kModelFiles[] = {"pbrify-normal-int8.onnx",   "pbrify-height-int8.onnx",
                                              "pbrify-roughness-int8.onnx", "pbrify-normal.ncnn.param",
                                              "pbrify-normal.ncnn.bin",     "pbrify-height.ncnn.param",
                                              "pbrify-height.ncnn.bin",     "pbrify-roughness.ncnn.param",
                                              "pbrify-roughness.ncnn.bin",  "pbrify-upscale-int8.onnx",
                                              "pbrify-upscale.ncnn.param",  "pbrify-upscale.ncnn.bin"};

// The models loaded once for many textures, on the device measured for this game (backend.hpp ChooseDevice: the GPU
// through ncnn, or the CPU through onnxruntime; logged). One thread at a time (Cancel aside).
class Session
{
  public:
    struct Stats
    {
        int prepared = 0;
        double infer_seconds = 0.0, max_texture_ms = 0.0;
        std::string provider;
        bool gpu = false;      // the networks run on the GPU (else onnxruntime on the CPU)
        std::string gpu_crash; // the GPU (and driver) whose inference crashed a previous run, so the CPU is used; else empty
    };
    // `threads`: the CPU's inference threads; 0 = every core. `cache`: where the device choice is kept (the cache
    // root's relief directory). Null and `error` on failure.
    static std::unique_ptr<Session> Open(const Models &models, int threads, const std::filesystem::path &cache,
                                         std::string &error);
    ~Session();
    // One texture file under `textures` (the game's RESOURCE/Textures; the key does not depend on where the game
    // lies), unless its map is there already. A texture up to the map's size is upscaled first and its maps are made
    // from its ×2 (fx::ReliefMapsFromUpscale); with `display` that ×2 is also kept for the game (fx/upscale.hpp).
    Outcome PrepareFile(const std::filesystem::path &textures, const std::filesystem::path &file,
                        const std::filesystem::path &output, int map_size, std::string &failure,
                        const UpscaleOutput *display = nullptr);
    // The game's ×2 of one texture file (fx::ClassifyUpscale by its path: Skipped for a texture that gets none), made
    // from its level whose larger side is `source_size` (the texture as the game loaded it; 0 its top level), unless it is
    // there already.
    Outcome Upscale(const std::filesystem::path &textures, const std::filesystem::path &file, const UpscaleOutput &display,
                    int source_size, std::string &failure);
    // Any thread (the game's shutdown): the texture being prepared fails at once, and every later one.
    void Cancel();
    const Stats &stats() const;

  private:
    struct Impl;
    explicit Session(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// Removes the maps of textures this game no longer has and every map of another method (another game, an older build,
// model or options; fx::ReliefMethod), and the gloss maps (fx/gloss_map.hpp) of textures it no longer has; the
// game's own height maps stay while they are current. `files`: List(textures) (one walk of the game's folders serves
// the listing, the mark and the pruning). Reads each file's header and probe level only. Returns how many.
int Prune(const std::filesystem::path &textures, const std::vector<std::filesystem::path> &files,
          const std::filesystem::path &output, int map_size);
// The mark of a complete pass (Prune done): the method, the map size and how many relief textures the game had
// (`candidates`: List(textures).size()). Cheap to check: the game skips its background pass while the mark matches.
void MarkComplete(const std::filesystem::path &output, int map_size, size_t candidates);
// Also false when maps of that pass are missing since (the system evicted part of its cache): `missing` says how many.
bool Complete(const std::filesystem::path &output, int map_size, size_t candidates, int *missing = nullptr);

// The whole job at once (host tool and checks): List, every texture, Prune.
struct Job
{
    std::filesystem::path textures; // the game's RESOURCE/Textures
    std::filesystem::path output;   // the relief directory
    Models models;
    int map_size = fx::kReliefMapSize;
    int threads = 0;
    std::optional<UpscaleOutput> display; // the ×2 the maps of small textures are made from, kept for the game too
};
struct Progress
{
    int done = 0, total = 0;
    std::string current;
};
struct Result
{
    int status = 0; // 0 complete, 1 cancelled, 2 error (`error` says why); failed textures alone do not make an error
    std::string error;
    int total = 0, prepared = 0, existing = 0, failed = 0, skipped = 0, removed = 0;
    double seconds = 0.0, infer_seconds = 0.0, max_texture_ms = 0.0;
    long long peak_rss_kb = 0;
    std::string provider;
    std::string Json() const;
};
// `progress` after each texture (and once with done = 0); returning true cancels.
Result Prepare(const Job &job, const std::function<bool(const Progress &)> &progress);

// The first level of the game texture `file` (.tx) whose larger side is at most `max_size`, decoded to RGBA8 (R first)
// into `rgba`. False when the file is no texture the relief reads (palette, cube map, unknown format, damaged).
bool ReadTextureLevel(const std::filesystem::path &file, int max_size, std::vector<uint8_t> &rgba, int &width, int &height);

// The textures of a game that get a ×2 (fx::ClassifyUpscale: their class by name, an alpha that cuts holes as the
// game decides it, effects texture_pixels), with their sizes: the host tool's list for `Session::Upscale`.
struct UpscaleCandidate
{
    std::filesystem::path file;
    std::string name;
    fx::UpscaleTarget target;
    int width = 0, height = 0;
};
std::vector<UpscaleCandidate> UpscaleCandidates(const std::filesystem::path &textures);

long long PeakRssKb();
// Logging of the library: Android log tag "storm-relief" and stderr.
void Log(const char *format, ...);

} // namespace storm::relief
