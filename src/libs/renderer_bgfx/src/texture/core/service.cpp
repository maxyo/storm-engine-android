#include "service.hpp"

#include <algorithm>
#include <chrono>
#include <string_view>

#if defined(__linux__) || defined(__ANDROID__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace storm::texture
{
namespace
{
constexpr size_t kMaxQueuedJobs = 64;
constexpr size_t kMaxQueuedBytes = 96u << 20;
constexpr auto kReportInterval = std::chrono::seconds(10);
constexpr uint32_t kEvictEveryWrites = 64;

void lower_thread_priority()
{
#if defined(__linux__) || defined(__ANDROID__)
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);
#endif
}
} // namespace

Service &Service::instance()
{
    static Service service;
    return service;
}

Service::~Service() { shutdown(); }

void Service::configure(const Config &config, LogFn log)
{
    shutdown();
    std::lock_guard lock(mutex_);
    config_ = config;
    log_ = log;
    stopping_ = false;
    evict_requested_ = true;
}

void Service::shutdown()
{
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        for (auto &job : queue_)
            job->state = Job::Failed;
        queue_.clear();
        tasks_.clear();
        queued_bytes_ = 0;
    }
    wake_.notify_all();
    for (std::thread &thread : workers_)
        thread.join();
    workers_.clear();
    if (hits_ + misses_ > 0)
        report("shutdown");
    hits_ = misses_ = transcoded_ = 0;
    encoded_pixels_ = encode_nanoseconds_ = 0;
    reported_transcoded_ = UINT64_MAX;
}

std::optional<Blob> Service::lookup(uint64_t key, const Layout &layout)
{
    std::optional<Blob> blob;
    if (!config_.directory.empty())
        blob = read_cache_file(config_.directory, key, layout);
    ++(blob ? hits_ : misses_);
    return blob;
}

bool Service::has_room() const
{
    std::lock_guard lock(mutex_);
    return queue_.size() < kMaxQueuedJobs && queued_bytes_ < kMaxQueuedBytes;
}

std::shared_ptr<Job> Service::submit(const Layout &layout, uint64_t key, EtcFormat format, std::vector<uint8_t> input)
{
    std::unique_lock lock(mutex_);
    if (auto it = in_flight_.find(key); it != in_flight_.end())
        if (auto job = it->second.lock(); job && !job->cancelled)
        {
            ++job->holders; // the same contents are already being transcoded
            return job;
        }
    if (queue_.size() >= kMaxQueuedJobs || queued_bytes_ >= kMaxQueuedBytes)
        return nullptr;
    auto job = std::make_shared<Job>();
    job->layout = layout;
    job->key = key;
    job->format = format;
    job->input = std::move(input);
    job->holders = 1;
    queued_bytes_ += job->input.size();
    queue_.push_back(job);
    in_flight_[key] = job;
    if (workers_.empty())
        start_workers();
    lock.unlock();
    wake_.notify_one();
    return job;
}

void Service::post(std::function<void()> task)
{
    {
        std::lock_guard lock(mutex_);
        tasks_.push_back(std::move(task));
        if (workers_.empty())
            start_workers();
    }
    wake_.notify_one();
}

void Service::release(std::shared_ptr<Job> &job)
{
    if (!job)
        return;
    {
        std::lock_guard lock(mutex_); // submit must not hand out a job that is being cancelled
        if (--job->holders == 0)
            job->cancelled = true;
    }
    job.reset();
}

void Service::start_workers()
{
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    const unsigned count = std::clamp(cores / 4, 1u, 2u);
    for (unsigned i = 0; i < count; ++i)
        workers_.emplace_back(&Service::worker, this, i);
}

void Service::worker(unsigned index)
{
    lower_thread_priority();
    uint32_t writes = 0;
    auto last_report = std::chrono::steady_clock::now();
    std::unique_lock lock(mutex_);
    while (!stopping_)
    {
        if (index == 0 && evict_requested_ && !config_.directory.empty())
        {
            evict_requested_ = false;
            const auto directory = config_.directory;
            const uint64_t max_bytes = config_.max_bytes;
            lock.unlock();
            uint64_t total = scan_cache(directory);
            if (total > max_bytes)
                total = evict_cache(directory, max_bytes / 10 * 9);
            cache_bytes_ = total;
            lock.lock();
            continue;
        }
        if (queue_.empty() && !tasks_.empty())
        {
            std::function<void()> task = std::move(tasks_.front());
            tasks_.pop_front();
            ++running_;
            lock.unlock();
            task();
            lock.lock();
            --running_;
            continue;
        }
        if (queue_.empty())
        {
            wake_.wait_for(lock, std::chrono::seconds(1));
            if (index == 0 && std::chrono::steady_clock::now() - last_report >= kReportInterval)
            {
                last_report = std::chrono::steady_clock::now();
                lock.unlock();
                report("progress");
                lock.lock();
            }
            continue;
        }
        std::shared_ptr<Job> job = std::move(queue_.front());
        queue_.pop_front();
        queued_bytes_ -= job->input.size();
        ++running_;
        lock.unlock();

        if (!job->cancelled)
        {
            job->state = Job::Running;
            const auto start = std::chrono::steady_clock::now();
            Blob blob = transcode_to_ktx(job->layout, job->input.data(), job->format);
            const auto elapsed = std::chrono::steady_clock::now() - start;
            uint64_t pixels = 0;
            for (uint32_t level = 0; level < job->layout.levels; ++level)
                pixels += uint64_t(job->layout.level_width(level)) * job->layout.level_height(level);
            encoded_pixels_ += pixels * job->layout.faces;
            encode_nanoseconds_ += std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
            if (!config_.directory.empty())
            {
                const uint64_t written = write_cache_file(config_.directory, job->key, job->layout, job->format, blob);
                if (written)
                {
                    cache_bytes_ += written;
                    if (++writes % kEvictEveryWrites == 0 || cache_bytes_ > config_.max_bytes)
                        evict_requested_ = true;
                }
            }
            std::vector<uint8_t>().swap(job->input);
            job->result = std::make_shared<const Blob>(std::move(blob));
            ++transcoded_;
            job->state = Job::Done;
        }
        else
            job->state = Job::Failed;

        if (index == 0 && std::chrono::steady_clock::now() - last_report >= kReportInterval)
        {
            last_report = std::chrono::steady_clock::now();
            report("progress");
        }
        lock.lock();
        --running_;
        if (auto it = in_flight_.find(job->key); it != in_flight_.end() && it->second.lock() == job)
            in_flight_.erase(it);
    }
}

void Service::report(const char *when)
{
    if (!log_)
        return;
    size_t queued;
    {
        std::lock_guard lock(mutex_);
        queued = queue_.size();
    }
    const uint64_t transcoded = transcoded_;
    // progress lines only while something is happening
    if (std::string_view(when) == "progress" && queued == 0 && running_ == 0 && transcoded == reported_transcoded_)
        return;
    reported_transcoded_ = transcoded;
    const double seconds = encode_nanoseconds_ / 1e9;
    log_("texture cache (%s): hits %llu, misses %llu, transcoded %llu, %.1f MP/s per worker, cache %.1f MiB, "
         "queue %zu, fallback textures %.1f MiB",
         when, static_cast<unsigned long long>(hits_.load()), static_cast<unsigned long long>(misses_.load()),
         static_cast<unsigned long long>(transcoded), seconds > 0 ? encoded_pixels_ / 1e6 / seconds : 0.0,
         cache_bytes_ / 1048576.0, queued + running_, fallback_bytes_ / 1048576.0);
}

} // namespace storm::texture
