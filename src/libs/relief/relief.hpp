// Relief maps from DeepBump (fx/deepbump.hpp) through onnxruntime, for the game's stone, wood, building, plaster and
// ground textures, made from the player's game files into the relief directory (fx/relief_store.hpp) the game reads.
// The game makes them while it runs (renderer_bgfx effects/relief_generator.hpp: Session, List, Prune, MarkComplete);
// the host tool makes a whole game at once (Prepare). Contract: README.md here.
#pragma once

#include "storm/fx/relief_store.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
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

// The model loaded once (onnxruntime, CPU provider) for many textures. One thread at a time (Cancel aside).
class Session
{
  public:
    struct Stats
    {
        int prepared = 0;
        double infer_seconds = 0.0, max_texture_ms = 0.0;
        std::string provider;
    };
    // `threads`: inference threads; 0 = every core. Null and `error` on failure.
    static std::unique_ptr<Session> Open(std::span<const uint8_t> model, int threads, std::string &error);
    ~Session();
    // One texture file under `textures` (the game's RESOURCE/Textures; the key does not depend on where the game
    // lies), unless its map is there already.
    Outcome PrepareFile(const std::filesystem::path &textures, const std::filesystem::path &file,
                        const std::filesystem::path &output, int map_size, std::string &failure);
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
// game's own height maps stay while they are current. Returns how many.
int Prune(const std::filesystem::path &textures, const std::filesystem::path &output, int map_size);
// The mark of a complete pass (Prune done): the method, the map size and how many relief textures the game had. Cheap
// to check (names only): the game skips its background pass while the mark matches.
void MarkComplete(const std::filesystem::path &textures, const std::filesystem::path &output, int map_size);
// Also false when maps of that pass are missing since (the system evicted part of its cache): `missing` says how many.
bool Complete(const std::filesystem::path &textures, const std::filesystem::path &output, int map_size, int *missing = nullptr);

// The whole job at once (host tool and checks): List, every texture, Prune.
struct Job
{
    std::filesystem::path textures; // the game's RESOURCE/Textures
    std::filesystem::path output;   // the relief directory
    std::span<const uint8_t> model; // the DeepBump model (model/README.md)
    int map_size = fx::kReliefMapSize;
    int threads = 0;
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

long long PeakRssKb();
// Logging of the library: Android log tag "storm-relief" and stderr.
void Log(const char *format, ...);

} // namespace storm::relief
