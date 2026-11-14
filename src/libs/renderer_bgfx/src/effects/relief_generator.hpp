// The game's relief maps made while it runs (the owner's choice): PBRify's models (src/libs/relief) on one worker thread in the
// game process, into the relief directory (fx/relief_store.hpp) the game reads.
//
//   * A loaded colour texture of a relief class without its map, or of a gloss class without its gloss map, is requested
//     (NormalMaps::on_texture_loaded); one preparation makes both (PBRify's normal, height and roughness networks). The
//     requests go first, the one drawn most recently first (on screen now), then the rest in load order (the current
//     location). When its map is written the texture's entry becomes Stored: the draw reads and uploads it and the
//     relief fades in (NormalMaps::handle).
//   * With no request for 30 s (not while a location loads), while relief is on, the rest of the game's relief textures (relief::List, by name) are
//     made from their files, one at a time, so later locations find them ready.
//     A complete pass prunes stale maps and leaves a mark (fx::kReliefMethodVersion, map size, candidate count): the
//     next starts skip it.
//   * The worker and onnxruntime's threads run at nice 10, on all cores but two (the game's main and render threads).
//     On Android 11+ the device's thermal status (and, on 12+, the headroom forecast) pauses the work from MODERATE up (logged); older Android has no
//     thermal API (logged once). The models are loaded at the first job and released after 10 s with no work left.
// The maps persist: a texture's map is made once per game, method and map size.
#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace storm::relief
{
class Session;
}

namespace storm::effects
{
struct NormalMapEntry;

struct ReliefSetup
{
    std::filesystem::path directory; // the relief directory
    std::filesystem::path textures;  // the game's RESOURCE/Textures
    // Reads a model file by its name (src/libs/relief/model/README.md): the APK assets on Android, engine.ini relief_models on the
    // host. Null: no maps are made (logged at the first request); the textures without a map get no relief.
    std::function<std::vector<uint8_t>(const char *file)> model;
};

class ReliefGenerator
{
  public:
    static ReliefGenerator &instance();
    ~ReliefGenerator();

    // Main thread, before the first texture is loaded. Stops the work of a previous setup.
    void configure(ReliefSetup setup);
    // Main thread: a loaded texture whose map is not in the directory; `file` is its game file.
    void request(const std::shared_ptr<NormalMapEntry> &entry, std::filesystem::path file);
    // Main thread: relief is on (the rest of the game is made only then).
    void set_active(bool active);
    // Stops the worker (the texture in progress is abandoned: onnxruntime stops its inference).
    void shutdown();

    struct Stats
    {
        uint32_t requested = 0;    // loaded textures waiting for their map
        int rest = -1;             // other relief textures of the game still to make; -1 not counted yet
        uint32_t made = 0;         // maps made since the start
        double made_ms = 0.0;      // their worker time
        double slowest_ms = 0.0;   // the slowest of them
        bool hot = false;          // paused by the thermal status
        std::string device;        // where the networks run (relief::Session's provider), once loaded
    };
    Stats stats();

  private:
    struct Request
    {
        std::weak_ptr<NormalMapEntry> entry;
        std::filesystem::path file;
        uint64_t order = 0;
    };
    void run();
    std::optional<Request> next_request(); // under mutex_
    bool open_session();
    void prepare_request(const Request &request);
    void prepare_rest();
    bool thermal_hot();

    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    bool stopping_ = false;
    bool active_ = false;
    ReliefSetup setup_;
    std::deque<Request> requests_;
    uint64_t order_ = 0;
    std::chrono::steady_clock::time_point last_request_{}, idle_since_{};
    Stats stats_;
    // Worker only.
    std::unique_ptr<relief::Session> session_;
    std::array<std::vector<uint8_t>, 9> models_; // relief::kModelFiles; onnxruntime keeps no copy of the bytes it was given
    bool model_failed_ = false;
    std::vector<std::filesystem::path> rest_;
    size_t rest_at_ = 0;
    bool rest_listed_ = false, rest_done_ = false;
    int candidates_ = 0;
    relief::Session *running_ = nullptr; // under mutex_: the session shutdown() cancels
};

} // namespace storm::effects
