// Background transcode service for DXT textures (their mip chains rebuilt, mips.hpp; encoded as ETC2 or BC,
// cache.hpp): cache lookups (main thread), a small
// low-priority worker pool, cache writes/eviction and the summary log line.
// No bgfx calls happen here; the shim swaps textures on its own thread.
#pragma once

#include "cache.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace storm::texture
{

enum class Mode : uint8_t
{
    Auto, // BC if the GPU has it, else ETC2 if available, else let bgfx decode
    Bc,   // always upload DXT as is
    Etc2, // transcode whenever the GPU supports ETC2
    Rgba, // decode DXT to RGBA8 in the shim
};

struct Config
{
    Mode mode = Mode::Auto;
    std::filesystem::path directory; // empty: no disk cache (transcode only)
    uint64_t max_bytes = 3ull << 30;
};

struct Job
{
    enum State : int
    {
        Queued,
        Running,
        Done,
        Failed,
    };
    Layout layout;
    uint64_t key = 0;
    GpuFormat format = GpuFormat::Etc2Rgb8; // `layout` is the input's; output_layout() of it is the result's
    std::vector<uint8_t> input; // D3D payload, released once encoded
    std::shared_ptr<const Blob> result; // set before state becomes Done
    std::atomic<int> state{Queued};
    // Set by the service only, when the last holder releases the job (see Service::release); a
    // deduplicated job is shared by every texture with the same contents.
    std::atomic<bool> cancelled{false};
    std::atomic<int> holders{0};
};

class Service
{
  public:
    using LogFn = void (*)(const char *format, ...);

    static Service &instance();
    ~Service();

    void configure(const Config &config, LogFn log);
    Mode mode() const { return config_.mode; }
    // Joins the workers and logs the final summary. Safe to call repeatedly.
    void shutdown();

    // Main thread. Counts a hit or a miss.
    std::optional<Blob> lookup(uint64_t key, const Layout &layout);
    // Main thread. Returns nullptr when the queue is full (try again later).
    // Returns a job the caller holds until it calls release(); identical contents share one job.
    std::shared_ptr<Job> submit(const Layout &layout, uint64_t key, GpuFormat format, std::vector<uint8_t> input);
    // Drops the caller's hold and resets `job`; the job is cancelled when no holder is left.
    void release(std::shared_ptr<Job> &job);
    bool has_room() const;
    // Main thread. Other CPU work for the workers (normal maps derived from height textures,
    // src/effects/normal_maps.cpp): runs when no transcode is queued. Dropped unrun at shutdown. (The ray bakes
    // have a thread of their own: fx::PostBake.)
    void post(std::function<void()> task);
    // The on-disk cache directory (empty: no disk cache). Fixed between configure() calls.
    const std::filesystem::path &cache_directory() const { return config_.directory; }
    // Bytes of temporary (pre-ETC2) textures alive on the GPU.
    void add_fallback_bytes(int64_t delta) { fallback_bytes_ += delta; }
    // Worker threads the service runs (a quarter of the cores, one or two).
    static unsigned worker_count();

  private:
    void start_workers();
    void worker(unsigned index);
    void report(const char *when);

    Config config_;
    LogFn log_ = nullptr;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::shared_ptr<Job>> queue_;
    std::deque<std::function<void()>> tasks_;
    std::unordered_map<uint64_t, std::weak_ptr<Job>> in_flight_;
    size_t queued_bytes_ = 0;
    std::vector<std::thread> workers_;
    bool stopping_ = false;
    bool evict_requested_ = true;
    bool empty_logged_ = false; // the cache directory was found empty or gone (worker 0 only)

    std::atomic<uint64_t> hits_{0}, misses_{0}, transcoded_{0};
    std::atomic<uint64_t> encoded_pixels_{0}, encode_nanoseconds_{0};
    std::atomic<uint64_t> cache_bytes_{0};
    std::atomic<int64_t> fallback_bytes_{0};
    std::atomic<int> running_{0};
    uint64_t reported_transcoded_ = UINT64_MAX;
};

} // namespace storm::texture
