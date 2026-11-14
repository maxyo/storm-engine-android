#include "effects/relief_generator.hpp"

#include "effects/normal_maps.hpp"
#include "effects/thermal.hpp"
#include "backend.hpp"
#include "effects/upscale.hpp"
#include "relief.hpp"
#include "shim/shim.hpp"
#include "storm/fx/relief_store.hpp"

#include <algorithm>
#include <chrono>

#if defined(__linux__) || defined(__ANDROID__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace storm::effects
{

namespace
{
using Clock = std::chrono::steady_clock;
constexpr auto kIdleRelease = std::chrono::seconds(10);
constexpr auto kWaitPause = std::chrono::seconds(5); // asked again this often while the work must wait
// The rest of the game only after this long without a request (and after the start): not while a location loads.
constexpr auto kRestQuiet = std::chrono::seconds(30);

double Ms(Clock::time_point since)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
}

} // namespace

ReliefGenerator &ReliefGenerator::instance()
{
    static ReliefGenerator generator;
    return generator;
}

ReliefGenerator::~ReliefGenerator()
{
    shutdown();
}

void ReliefGenerator::configure(ReliefSetup setup)
{
    shutdown();
    std::lock_guard lock(mutex_);
    setup_ = std::move(setup);
    stopping_ = false;
    stats_ = {};
    model_failed_ = false;
    next_upscale_ = false;
    admitted_ = 0;
    rest_.clear();
    rest_at_ = 0;
    rest_listed_ = rest_done_ = false;
    last_request_ = idle_since_ = Clock::now();
    worker_ = std::thread([this] { run(); });
}

void ReliefGenerator::shutdown()
{
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        if (running_)
            running_->Cancel();
        for (const Request &request : requests_)
            if (const std::shared_ptr<NormalMapEntry> entry = request.entry.lock())
                entry->state = NormalMapEntry::Missing;
        requests_.clear();
        for (const std::weak_ptr<UpscaleEntry> &weak : upscales_)
            if (const std::shared_ptr<UpscaleEntry> entry = weak.lock())
                entry->state = UpscaleEntry::Missing;
        upscales_.clear();
    }
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
    running_ = nullptr;
    session_.reset();
    // The Vulkan instance closes with the generator, not in ncnn's exit handler: by then the driver's own exit
    // handlers have run and the process crashed in it.
    relief::ReleaseGpu();
    for (std::vector<uint8_t> &bytes : models_)
        bytes = {};
}

bool ReliefGenerator::request_upscale(const std::shared_ptr<UpscaleEntry> &entry)
{
    {
        std::lock_guard lock(mutex_);
        if (!worker_.joinable() || stopping_ || model_failed_)
            return false;
        std::erase_if(upscales_, [](const std::weak_ptr<UpscaleEntry> &weak) { return weak.expired(); });
        if (upscales_.size() >= kUpscaleQueue)
            return false;
        entry->queued_after = admitted_;
        entry->queued_scene = generation_focus().snapshot().scene;
        upscales_.push_back(entry);
    }
    wake_.notify_all();
    return true;
}

void ReliefGenerator::set_display(bool on, std::filesystem::path directory, bool bc)
{
    std::lock_guard lock(mutex_);
    display_ = on;
    display_directory_ = std::move(directory);
    display_bc_ = bc;
}

void ReliefGenerator::request(const std::shared_ptr<NormalMapEntry> &entry, std::filesystem::path file)
{
    {
        std::lock_guard lock(mutex_);
        if (!worker_.joinable() || stopping_)
        {
            entry->state = NormalMapEntry::Missing;
            return;
        }
        entry->queued_after = admitted_;
        entry->queued_scene = generation_focus().snapshot().scene;
        requests_.push_back({entry, std::move(file), order_++});
        last_request_ = Clock::now();
    }
    wake_.notify_all();
}

void ReliefGenerator::set_active(bool active)
{
    {
        std::lock_guard lock(mutex_);
        if (active == active_)
            return;
        active_ = active;
    }
    wake_.notify_all();
}

ReliefGenerator::Stats ReliefGenerator::stats()
{
    std::lock_guard lock(mutex_);
    Stats stats = stats_;
    stats.requested = static_cast<uint32_t>(std::count_if(requests_.begin(), requests_.end(), [](const Request &request) {
        return !request.entry.expired();
    }));
    return stats;
}

namespace
{
template<class Entry> auto rank(const std::shared_ptr<Entry> &entry, GenerationSnapshot at, uint64_t admitted)
{
    auto value = entry ? generation_rank(entry->priority, at, admitted - entry->queued_after)
                       : std::pair<uint32_t, float>{UINT32_MAX, std::numeric_limits<float>::infinity()};
    if (entry && value.first == UINT32_MAX && entry->queued_scene == at.scene)
        --value.first; // unseen current-scene load before retained entries from a previous scene
    return value;
}
}

std::optional<ReliefGenerator::Request> ReliefGenerator::next_request()
{
    std::erase_if(requests_, [](const Request &request) { return request.entry.expired(); });
    if (requests_.empty())
        return std::nullopt;
    const auto at = generation_focus().snapshot();
    const auto best = std::min_element(requests_.begin(), requests_.end(), [&](const Request &a, const Request &b) {
        const auto ea = a.entry.lock(), eb = b.entry.lock();
        const auto ra = rank(ea, at, admitted_), rb = rank(eb, at, admitted_);
        if (ra != rb) return ra < rb;
        if (ra.second < 0.0f) return a.order < b.order; // oldest admitted-waiter first
        const uint32_t da = ea ? ea->drawn.load() : 0, db = eb ? eb->drawn.load() : 0;
        return da != db ? da > db : a.order < b.order;
    });
    Request request = std::move(*best);
    requests_.erase(best);
    return request;
}

std::shared_ptr<UpscaleEntry> ReliefGenerator::next_upscale()
{
    if (upscales_.empty()) return {};
    const auto at = generation_focus().snapshot();
    const auto best = std::min_element(upscales_.begin(), upscales_.end(), [&](const auto &a, const auto &b) {
        const auto ea = a.lock(), eb = b.lock();
        const auto ra = rank(ea, at, admitted_), rb = rank(eb, at, admitted_);
        if (ra != rb) return ra < rb;
        return ra.second < 0.0f && ea && eb && ea->queued_after < eb->queued_after;
    });
    auto result = best->lock();
    upscales_.erase(best);
    return result;
}

void ReliefGenerator::reconcile_upscales()
{
    // A normal-map job can also write this exact display texture. Publishing that completed file is not inference:
    // do it before the thermal gate, or a warm phone can leave an already prepared texture invisible indefinitely.
    // The texture worker still validates its header, dimensions and payload before any GPU replacement is bound.
    std::erase_if(upscales_, [&](const std::weak_ptr<UpscaleEntry> &weak) {
        const auto entry = weak.lock();
        if (!entry || entry->state != UpscaleEntry::Waiting)
            return true;
        if (display_ && fx::HasUpscaled(display_directory_, entry->key))
        {
            entry->state = UpscaleEntry::Stored;
            return true;
        }
        return false;
    });
}

void ReliefGenerator::run()
{
#if defined(__linux__) || defined(__ANDROID__)
    // Below the game's threads; onnxruntime's thread pool, created on this thread, inherits it.
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);
#endif
    for (;;)
    {
        std::optional<Request> request;
        std::shared_ptr<UpscaleEntry> upscale;
        {
            std::unique_lock lock(mutex_);
            for (;;)
            {
                if (stopping_)
                    return;
                if (trim_)
                {
                    trim_ = false;
                    release_models("the system is low on memory");
                }
                const Clock::time_point now = Clock::now();
                const bool rest = active_ && !rest_done_ && !model_failed_;
                reconcile_upscales();
                // Reselect from fresh 500 ms player-distance observations before every job. In-flight work is
                // never restarted. Alternate only within the same proximity band; background work remains last.
                request = next_request();
                upscale = next_upscale();
                if (request && upscale)
                {
                    const auto normal = request->entry.lock();
                    const auto at = generation_focus().snapshot();
                    const auto nr = rank(normal, at, admitted_), ur = rank(upscale, at, admitted_);
                    bool choose_upscale = next_upscale_;
                    if (nr.first != ur.first) choose_upscale = ur.first < nr.first;
                    else if ((nr.second < 0.0f) != (ur.second < 0.0f)) choose_upscale = ur.second < 0.0f;
                    else if (nr.second < 0.0f && normal && normal->queued_after != upscale->queued_after)
                        choose_upscale = upscale->queued_after < normal->queued_after;
                    if (choose_upscale)
                    {
                        requests_.push_front(std::move(*request));
                        request.reset();
                    }
                    else
                    {
                        upscales_.push_front(upscale);
                        upscale.reset();
                    }
                }
                if (request || upscale || (rest && now - last_request_ >= kRestQuiet))
                    break;
                // Nothing due: a request wakes the worker; the rest waits for a quiet spell; with no work left at
                // all the model goes after kIdleRelease.
                Clock::time_point until = rest ? last_request_ + kRestQuiet : Clock::time_point::max();
                if (session_ && !rest)
                {
                    if (now - idle_since_ >= kIdleRelease)
                    {
                        release_models("idle");
                        continue;
                    }
                    until = std::min(until, idle_since_ + kIdleRelease);
                }
                if (until == Clock::time_point::max())
                    wake_.wait(lock);
                else
                    wake_.wait_until(lock, until);
            }
            if (must_wait())
            {
                if (request)
                    requests_.push_front(std::move(*request));
                if (upscale)
                    upscales_.push_front(upscale);
                wake_.wait_for(lock, kWaitPause, [this] { return stopping_; });
                continue;
            }
            if (request || upscale)
            {
                next_upscale_ = request.has_value(); // pauses and cache-only completion consume no turn
                ++admitted_;
            }
            stats_.working = true;
        }
        if (!open_session())
        {
            std::lock_guard lock(mutex_);
            if (request)
                if (const std::shared_ptr<NormalMapEntry> entry = request->entry.lock())
                    entry->state = NormalMapEntry::Missing;
            if (upscale)
                upscale->state = stopping_ ? UpscaleEntry::Missing : UpscaleEntry::Failed;
            for (const Request &r : requests_)
                if (const std::shared_ptr<NormalMapEntry> entry = r.entry.lock())
                    entry->state = NormalMapEntry::Missing;
            requests_.clear();
            stats_.working = false;
            continue;
        }
        if (request)
            prepare_request(*request);
        else if (upscale)
            prepare_upscale(upscale);
        else
        {
            prepare_rest();
            // On the CPU the rest of the game goes one texture per session while the game plays: onnxruntime's memory
            // (its environment, the three sessions) is not held between them. (On the GPU the networks stay: making
            // their pipelines again would cost more than the texture.)
            std::lock_guard lock(mutex_);
            if (session_ && !session_->stats().gpu)
            {
                running_ = nullptr;
                session_.reset();
            }
        }
        std::lock_guard lock(mutex_);
        idle_since_ = Clock::now();
        stats_.working = false;
    }
}

bool ReliefGenerator::open_session()
{
    if (session_)
        return true;
    if (model_failed_)
        return false;
    if (!setup_.model)
    {
        // A host without engine.ini relief_models: a configuration, not a defect.
        model_failed_ = true;
        shim::logf("effects: relief generation off: no relief models configured (engine.ini relief_models): textures "
                   "without a map get no relief");
        return false;
    }
    const Clock::time_point start = Clock::now();
    std::string error = "the relief models could not be read";
    bool read = true;
    for (size_t i = 0; i < std::size(relief::kModelFiles); ++i)
    {
        models_[i] = setup_.model(relief::kModelFiles[i]);
        if (models_[i].empty())
        {
            read = false;
            error = std::string("cannot read the model file ") + relief::kModelFiles[i];
        }
    }
    // All cores but the game's main and render threads.
    const unsigned cores = std::thread::hardware_concurrency();
    const int threads = cores > 2 ? static_cast<int>(cores - 2) : 1;
    const relief::Models models{models_[0], models_[1], models_[2], models_[3],  models_[4],  models_[5],
                                models_[6], models_[7], models_[8], models_[9], models_[10], models_[11]};
    std::unique_ptr<relief::Session> session =
        read ? relief::Session::Open(models, threads, setup_.directory, error) : nullptr;
    if (!session)
    {
        model_failed_ = true;
        for (std::vector<uint8_t> &bytes : models_)
            bytes = {};
        shim::logf("ERROR: effects: relief generation unavailable (%s): textures without a map get no relief",
                   error.c_str());
        return false;
    }
    size_t bytes = 0;
    for (const std::vector<uint8_t> &model : models_)
        bytes += model.size();
    shim::logf("effects: relief generation: PBRify models loaded (%.1f MB) on %s in %.0f ms",
               static_cast<double>(bytes) / 1048576.0, session->stats().provider.c_str(), Ms(start));
    // Into the game's log (and so the diagnostics): the library's own log reaches only logcat.
    if (!session->stats().gpu_crash.empty())
        shim::logf("ERROR: effects: relief GPU inference crashed the previous run on %s: using the CPU",
                   session->stats().gpu_crash.c_str());
    std::lock_guard lock(mutex_);
    if (stopping_)
        return false;
    stats_.device = session->stats().provider;
    session_ = std::move(session);
    running_ = session_.get();
    return true;
}

void ReliefGenerator::prepare_request(const Request &request)
{
    const std::shared_ptr<NormalMapEntry> entry = request.entry.lock();
    if (!entry)
        return; // unloaded meanwhile
    std::optional<relief::UpscaleOutput> display;
    {
        std::lock_guard lock(mutex_);
        if (display_)
            display = relief::UpscaleOutput{display_directory_, display_bc_};
    }
    const Clock::time_point start = Clock::now();
    std::string failure;
    const relief::Outcome outcome =
        session_->PrepareFile(setup_.textures, request.file, setup_.directory, fx::kReliefMapSize, failure,
                              display ? &*display : nullptr);
    const double ms = Ms(start);
    {
        std::lock_guard lock(mutex_);
        stats_.device = session_->stats().provider; // the GPU may have handed over to the CPU (relief::DemoteGpu)
        if (stopping_)
        {
            entry->state = NormalMapEntry::Missing;
            return;
        }
        if (outcome == relief::Outcome::Prepared)
        {
            ++stats_.made;
            stats_.made_ms += ms;
            stats_.slowest_ms = std::max(stats_.slowest_ms, ms);
        }
    }
    // A gloss entry waits for the texture's gloss map, a relief entry for its relief map: one preparation makes both.
    const bool gloss = entry->gloss != fx::GlossClass::None;
    if ((outcome == relief::Outcome::Prepared || outcome == relief::Outcome::Existing) &&
        (gloss ? fx::HasGlossMap(setup_.directory, entry->key) : fx::HasReliefMap(setup_.directory, entry->key)))
    {
        entry->state = NormalMapEntry::Stored; // the next draw reads and uploads it
        return;
    }
    if (outcome == relief::Outcome::Failed)
        shim::logf("ERROR: effects: %s map of %s not made (%s)", gloss ? "gloss" : "relief", entry->name.c_str(),
                   failure.c_str());
    else
        // The preparation and the game disagree on the texture (its key or class): a defect to fix, not a state.
        shim::logf("ERROR: effects: %s map of %s: its file %s %s", gloss ? "gloss" : "relief", entry->name.c_str(),
                   request.file.string().c_str(),
                   outcome == relief::Outcome::Skipped ? "gets no prepared map" : "has a map under another key");
    entry->state = NormalMapEntry::Failed;
}

void ReliefGenerator::trim()
{
    {
        std::lock_guard lock(mutex_);
        trim_ = true;
    }
    wake_.notify_all();
}

void ReliefGenerator::release_models(const char *why)
{
    const bool had = session_ != nullptr;
    running_ = nullptr;
    session_.reset();
    for (std::vector<uint8_t> &bytes : models_)
        bytes = {};
    const bool gpu = relief::ReleaseGpu();
    if (had || gpu)
        shim::logf("effects: relief generation (%s): the relief models are released%s", why,
                   gpu ? ", the GPU's Vulkan instance closed" : "");
}

void ReliefGenerator::prepare_rest()
{
    if (!rest_listed_)
    {
        rest_listed_ = true;
        const Clock::time_point start = Clock::now();
        // One walk of the game's folders (names only) for the listing, the mark and the pruning.
        rest_ = relief::List(setup_.textures);
        rest_at_ = 0;
        const double list_ms = Ms(start);
        int missing = 0;
        if (relief::Complete(setup_.directory, fx::kReliefMapSize, rest_.size(), &missing))
        {
            shim::logf("effects: relief generation: the game's %zu relief and gloss textures have their maps (checked in "
                       "%.0f ms)",
                       rest_.size(), Ms(start));
            rest_.clear();
            std::lock_guard lock(mutex_);
            rest_done_ = true;
            stats_.rest = 0;
            return;
        }
        if (missing > 0)
            shim::logf("effects: relief generation: %d maps of the complete pass are missing (the cache was cleared or "
                       "evicted by the system): made again",
                       missing);
        // Maps of another method (an older build, another model or options) are removed first: they never match a key
        // again, and their room is the new maps'.
        const Clock::time_point prune_start = Clock::now();
        const int stale = relief::Prune(setup_.textures, rest_, setup_.directory, fx::kReliefMapSize);
        if (stale > 0)
            shim::logf("effects: relief generation: %d maps of another method or game removed", stale);
        shim::logf("effects: relief generation: %zu relief and gloss textures of the game to make in the background (listed in "
                   "%.0f ms, pruned in %.0f ms)",
                   rest_.size(), list_ms, Ms(prune_start));
    }
    if (rest_at_ >= rest_.size())
    {
        const int removed = relief::Prune(setup_.textures, rest_, setup_.directory, fx::kReliefMapSize);
        relief::MarkComplete(setup_.directory, fx::kReliefMapSize, rest_.size());
        shim::logf("effects: relief generation: every relief texture of the game has its map (%d stale maps removed)",
                   removed);
        std::lock_guard lock(mutex_);
        rest_done_ = true;
        stats_.rest = 0;
        rest_.clear();
        return;
    }
    // The ×2 its small textures' maps are made from is kept too while the game shows them (cheap: the encode only).
    std::optional<relief::UpscaleOutput> display;
    {
        std::lock_guard lock(mutex_);
        if (display_)
            display = relief::UpscaleOutput{display_directory_, display_bc_};
    }
    const Clock::time_point start = Clock::now();
    std::string failure;
    const relief::Outcome outcome = session_->PrepareFile(setup_.textures, rest_[rest_at_++], setup_.directory,
                                                          fx::kReliefMapSize, failure, display ? &*display : nullptr);
    const double ms = Ms(start);
    std::lock_guard lock(mutex_);
    stats_.rest = static_cast<int>(rest_.size() - rest_at_);
    stats_.device = session_->stats().provider;
    if (outcome == relief::Outcome::Prepared)
    {
        ++stats_.made;
        stats_.made_ms += ms;
        stats_.slowest_ms = std::max(stats_.slowest_ms, ms);
    }
}

void ReliefGenerator::prepare_upscale(const std::shared_ptr<UpscaleEntry> &entry)
{
    relief::UpscaleOutput display;
    {
        std::lock_guard lock(mutex_);
        if (!display_)
        {
            entry->state = UpscaleEntry::Missing; // switched off meanwhile
            return;
        }
        display = {display_directory_, display_bc_};
    }
    const Clock::time_point start = Clock::now();
    std::string failure;
    const relief::Outcome outcome = session_->Upscale(setup_.textures, entry->file, display,
                                                      static_cast<int>(std::max(entry->width, entry->height)), failure);
    const double ms = Ms(start);
    std::lock_guard lock(mutex_);
    if (stopping_)
    {
        entry->state = UpscaleEntry::Missing;
        return;
    }
    switch (outcome)
    {
    case relief::Outcome::Prepared:
        ++stats_.upscaled;
        stats_.upscale_ms += ms;
        [[fallthrough]];
    case relief::Outcome::Existing:
        entry->state = UpscaleEntry::Stored; // the next plan reads it
        break;
    case relief::Outcome::Skipped:
        // The game and the preparation disagree on the texture's class or file: a defect, not a state.
        shim::logf("ERROR: effects upscale: %s: its file %s gets no ×2", entry->name.c_str(), entry->file.string().c_str());
        entry->state = UpscaleEntry::Failed;
        break;
    case relief::Outcome::Failed:
        shim::logf("ERROR: effects upscale: ×2 of %s not made (%s)", entry->name.c_str(), failure.c_str());
        entry->state = UpscaleEntry::Failed;
        break;
    }
}

bool ReliefGenerator::must_wait()
{
    const char *why = background_wait_reason();
    if (why && stats_.paused != why)
        shim::logf("effects: relief generation paused: %s", why);
    else if (!why && !stats_.paused.empty())
        shim::logf("effects: relief generation resumed (was paused: %s)", stats_.paused.c_str());
    stats_.paused = why ? why : "";
    return why != nullptr;
}

} // namespace storm::effects
