// The game's relief maps made while it runs (the owner's choice): PBRify's models (src/libs/relief) on one worker thread in the
// game process, into the relief directory (fx/relief_store.hpp) the game reads.
//
//   * A loaded colour texture of a relief class without its map, or of a gloss class without its gloss map, is requested
//     (NormalMaps::on_texture_loaded); one preparation makes both (PBRify's normal, height and roughness networks). The
//     requests are reselected from recent player-distance observations before each job, alternating with standalone
//     upscales within the same 10 m band. Unobserved entries fall back to draw/load order; old-scene entries follow
//     current-scene work. Live work ages after 16 other admissions. When its map is written the texture's entry
//     becomes Stored: the draw reads and uploads it and the relief fades in (NormalMaps::handle).
//   * With no request for 30 s (not while a location loads), while relief is on, the rest of the game's relief textures (relief::List, by name) are
//     made from their files, one at a time, so later locations find them ready.
//     A complete pass prunes stale maps and leaves a mark (fx::kReliefMethodVersion, map size, candidate count): the
//     next starts skip it.
//   * The worker and onnxruntime's threads run at nice 10, on all cores but two (the game's main and render threads).
//     On Android 11+ the device's thermal status (and, on 12+, the headroom forecast) pauses the work from MODERATE up (logged); older Android has no
//     thermal API (logged once). The renderer's hold (the automatic graphics preset's measurement) pauses it too. The models are loaded at the first job and released after 10 s with no work left.
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
struct UpscaleEntry;

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
    // Main thread: request a missing ×2 texture (effects/upscale.hpp). Alternates with loaded relief requests,
    // before background preparation. A completed cache file is published even while thermal protection pauses
    // inference. The caller sets Waiting before enqueueing. False when
    // kUpscaleQueue requests wait already (the entry asks again later). Done: the entry becomes Stored, or Failed.
    bool request_upscale(const std::shared_ptr<UpscaleEntry> &entry);
    // Main thread: the game shows the ×2 textures (Settings::upscale): the requested relief maps made from a ×2 also
    // keep it in `directory`, encoded for the tier (`bc`). The rest of the game keeps only its maps.
    void set_display(bool on, std::filesystem::path directory, bool bc);
    // Stops the worker (the texture in progress is abandoned: onnxruntime stops its inference).
    void shutdown();
    // Any thread (the system is low on memory: render::TrimRelief): the models are released after the texture in
    // progress, and the GPU instance closed; they load again when work comes. Logged.
    void trim();

    struct Stats
    {
        uint32_t requested = 0;    // loaded textures waiting for their map
        int rest = -1;             // other relief textures of the game still to make; -1 not counted yet
        uint32_t made = 0;         // maps made since the start
        uint32_t upscaled = 0;     // ×2 textures made alone (effects/upscale.hpp) since the start
        double upscale_ms = 0.0;   // their worker time
        double made_ms = 0.0;      // their worker time
        double slowest_ms = 0.0;   // the slowest of them
        std::string paused;        // why the work waits (the thermal status, the renderer's hold); empty: it may run
        bool working = false;      // a map (or the list of the rest) is being made now
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
    std::shared_ptr<UpscaleEntry> next_upscale(); // under mutex_: nearest live candidate
    void reconcile_upscales(); // under mutex_: publish completed files; prune expired/stale requests
    bool open_session();
    void prepare_request(const Request &request);
    void prepare_rest();
    void release_models(const char *why); // worker, under mutex_
    bool must_wait(); // under mutex_: the background gate (thermal.hpp), changes logged
    void prepare_upscale(const std::shared_ptr<UpscaleEntry> &entry);

    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    bool stopping_ = false;
    bool trim_ = false;
    bool active_ = false;
    ReliefSetup setup_;
    std::deque<Request> requests_;
    std::deque<std::weak_ptr<UpscaleEntry>> upscales_;
    bool display_ = false, display_bc_ = false;
    std::filesystem::path display_directory_;
    uint64_t order_ = 0, admitted_ = 0;
    bool next_upscale_ = false; // a live lane never waits behind the entire other queue
    std::chrono::steady_clock::time_point last_request_{}, idle_since_{};
    Stats stats_;
    // Worker only.
    std::unique_ptr<relief::Session> session_;
    std::array<std::vector<uint8_t>, 12> models_; // relief::kModelFiles; onnxruntime keeps no copy of the bytes it was given
    bool model_failed_ = false;
    std::vector<std::filesystem::path> rest_;
    size_t rest_at_ = 0;
    bool rest_listed_ = false, rest_done_ = false;
    int candidates_ = 0;
    relief::Session *running_ = nullptr; // under mutex_: the session shutdown() cancels
};

} // namespace storm::effects
