#include "effects/relief_generator.hpp"

#include "effects/normal_maps.hpp"
#include "effects/thermal.hpp"
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
constexpr auto kThermalPause = std::chrono::seconds(5);
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
    }
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
    running_ = nullptr;
    session_.reset();
    model_.clear();
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

std::optional<ReliefGenerator::Request> ReliefGenerator::next_request()
{
    // Textures unloaded meanwhile are dropped: they are requested again when loaded again.
    std::erase_if(requests_, [](const Request &request) { return request.entry.expired(); });
    if (requests_.empty())
        return std::nullopt;
    // On screen now first: the most recently drawn, then the load order (the current location's textures).
    const auto best = std::min_element(requests_.begin(), requests_.end(), [](const Request &a, const Request &b) {
        const std::shared_ptr<NormalMapEntry> ea = a.entry.lock(), eb = b.entry.lock();
        const uint32_t da = ea ? ea->drawn.load() : 0, db = eb ? eb->drawn.load() : 0;
        return da != db ? da > db : a.order < b.order;
    });
    Request request = std::move(*best);
    requests_.erase(best);
    return request;
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
        {
            std::unique_lock lock(mutex_);
            for (;;)
            {
                if (stopping_)
                    return;
                const Clock::time_point now = Clock::now();
                const bool rest = active_ && !rest_done_ && !model_failed_;
                request = next_request();
                if (request || (rest && now - last_request_ >= kRestQuiet))
                    break;
                // Nothing due: a request wakes the worker; the rest waits for a quiet spell; with no work left at
                // all the model goes after kIdleRelease.
                Clock::time_point until = rest ? last_request_ + kRestQuiet : Clock::time_point::max();
                if (session_ && !rest)
                {
                    if (now - idle_since_ >= kIdleRelease)
                    {
                        running_ = nullptr;
                        session_.reset();
                        model_.clear();
                        model_.shrink_to_fit();
                        shim::logf("effects: relief generation idle: the DeepBump model is released");
                        continue;
                    }
                    until = std::min(until, idle_since_ + kIdleRelease);
                }
                if (until == Clock::time_point::max())
                    wake_.wait(lock);
                else
                    wake_.wait_until(lock, until);
            }
            if (thermal_hot())
            {
                if (request)
                    requests_.push_front(std::move(*request));
                wake_.wait_for(lock, kThermalPause, [this] { return stopping_; });
                continue;
            }
        }
        if (!open_session())
        {
            if (request)
                if (const std::shared_ptr<NormalMapEntry> entry = request->entry.lock())
                    entry->state = NormalMapEntry::Missing;
            std::lock_guard lock(mutex_);
            for (const Request &r : requests_)
                if (const std::shared_ptr<NormalMapEntry> entry = r.entry.lock())
                    entry->state = NormalMapEntry::Missing;
            requests_.clear();
            continue;
        }
        if (request)
            prepare_request(*request);
        else
            prepare_rest();
        std::lock_guard lock(mutex_);
        idle_since_ = Clock::now();
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
        // A host without engine.ini relief_model: a configuration, not a defect.
        model_failed_ = true;
        shim::logf("effects: relief generation off: no DeepBump model configured (engine.ini relief_model): textures "
                   "without a map get no relief");
        return false;
    }
    const Clock::time_point start = Clock::now();
    model_ = setup_.model();
    std::string error = "the DeepBump model could not be read";
    // All cores but the game's main and render threads.
    const unsigned cores = std::thread::hardware_concurrency();
    const int threads = cores > 2 ? static_cast<int>(cores - 2) : 1;
    std::unique_ptr<relief::Session> session = model_.empty() ? nullptr : relief::Session::Open(model_, threads, error);
    if (!session)
    {
        model_failed_ = true;
        model_.clear();
        shim::logf("ERROR: effects: relief generation unavailable (%s): textures without a map get no relief",
                   error.c_str());
        return false;
    }
    shim::logf("effects: relief generation: DeepBump model loaded (%.1f MB, onnxruntime %s) in %.0f ms",
               static_cast<double>(model_.size()) / 1048576.0, session->stats().provider.c_str(), Ms(start));
    std::lock_guard lock(mutex_);
    if (stopping_)
        return false;
    session_ = std::move(session);
    running_ = session_.get();
    return true;
}

void ReliefGenerator::prepare_request(const Request &request)
{
    const std::shared_ptr<NormalMapEntry> entry = request.entry.lock();
    if (!entry)
        return; // unloaded meanwhile
    const Clock::time_point start = Clock::now();
    std::string failure;
    const relief::Outcome outcome =
        session_->PrepareFile(setup_.textures, request.file, setup_.directory, fx::kReliefMapSize, failure);
    const double ms = Ms(start);
    {
        std::lock_guard lock(mutex_);
        if (stopping_)
            return;
        if (outcome == relief::Outcome::Prepared)
        {
            ++stats_.made;
            stats_.made_ms += ms;
            stats_.slowest_ms = std::max(stats_.slowest_ms, ms);
        }
    }
    if ((outcome == relief::Outcome::Prepared || outcome == relief::Outcome::Existing) &&
        fx::HasReliefMap(setup_.directory, entry->key))
    {
        entry->state = NormalMapEntry::Stored; // the next draw reads and uploads it
        return;
    }
    if (outcome == relief::Outcome::Failed)
        shim::logf("ERROR: effects: relief map of %s not made (%s)", entry->name.c_str(), failure.c_str());
    else
        // The preparation and the game disagree on the texture (its key or class): a defect to fix, not a state.
        shim::logf("ERROR: effects: relief map of %s: its file %s %s", entry->name.c_str(), request.file.string().c_str(),
                   outcome == relief::Outcome::Skipped ? "gets no prepared map" : "has a map under another key");
    entry->state = NormalMapEntry::Failed;
}

void ReliefGenerator::prepare_rest()
{
    if (!rest_listed_)
    {
        rest_listed_ = true;
        const Clock::time_point start = Clock::now();
        int missing = 0;
        if (relief::Complete(setup_.textures, setup_.directory, fx::kReliefMapSize, &missing))
        {
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
        const int stale = relief::Prune(setup_.textures, setup_.directory, fx::kReliefMapSize);
        if (stale > 0)
            shim::logf("effects: relief generation: %d maps of another method or game removed", stale);
        rest_ = relief::List(setup_.textures);
        rest_at_ = 0;
        shim::logf("effects: relief generation: %zu relief textures of the game to make in the background (listed in "
                   "%.0f ms)",
                   rest_.size(), Ms(start));
    }
    if (rest_at_ >= rest_.size())
    {
        const int removed = relief::Prune(setup_.textures, setup_.directory, fx::kReliefMapSize);
        relief::MarkComplete(setup_.textures, setup_.directory, fx::kReliefMapSize);
        shim::logf("effects: relief generation: every relief texture of the game has its map (%d stale maps removed)",
                   removed);
        std::lock_guard lock(mutex_);
        rest_done_ = true;
        stats_.rest = 0;
        rest_.clear();
        return;
    }
    const Clock::time_point start = Clock::now();
    std::string failure;
    const relief::Outcome outcome =
        session_->PrepareFile(setup_.textures, rest_[rest_at_++], setup_.directory, fx::kReliefMapSize, failure);
    const double ms = Ms(start);
    std::lock_guard lock(mutex_);
    stats_.rest = static_cast<int>(rest_.size() - rest_at_);
    if (outcome == relief::Outcome::Prepared)
    {
        ++stats_.made;
        stats_.made_ms += ms;
        stats_.slowest_ms = std::max(stats_.slowest_ms, ms);
    }
}

bool ReliefGenerator::thermal_hot()
{
    const bool hot = device_hot();
    if (hot != stats_.hot)
        shim::logf(hot ? "effects: relief generation paused: the device is hot (thermal status MODERATE or higher)"
                       : "effects: relief generation resumed: the device cooled down");
    stats_.hot = hot;
    return hot;
}

} // namespace storm::effects
