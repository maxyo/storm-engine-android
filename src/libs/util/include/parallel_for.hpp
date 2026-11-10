#pragma once

// Minimal persistent fork-join helper for short, independent per-frame jobs.
//
// The Android NDK's libc++ has no parallel algorithms, so loops that desktop
// builds run with std::execution::par_unseq were sequential there. ParallelFor
// keeps a few helper threads parked on a condition variable; run() hands out
// indices [0, count) through one atomic counter, takes part in the work itself
// and returns only after every helper has left the job, so the callable and
// everything it references may live on the caller's stack.
//
// Contract: fn(i) must be safe to call concurrently for different i and must
// not throw. run() is not reentrant and must always be called from one thread
// at a time (the game thread).

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>
#include <vector>

namespace storm
{
class ParallelFor
{
  public:
    // Helpers in addition to the calling thread. The default leaves one core
    // for the audio/render driver threads and stays on the big cluster of a
    // typical 4+4 phone instead of waking every little core.
    static unsigned defaultHelpers()
    {
        const unsigned cores = std::thread::hardware_concurrency();
        return cores <= 2 ? 0 : std::min(cores - 2, 3u);
    }

    explicit ParallelFor(unsigned helpers = defaultHelpers())
    {
        threads_.reserve(helpers);
        for (unsigned i = 0; i < helpers; ++i)
            threads_.emplace_back([this] { work(); });
    }

    ~ParallelFor()
    {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        for (auto &thread : threads_)
            thread.join();
    }

    ParallelFor(const ParallelFor &) = delete;
    ParallelFor &operator=(const ParallelFor &) = delete;

    unsigned helpers() const { return static_cast<unsigned>(threads_.size()); }

    template <typename Function> void run(size_t count, Function &&fn)
    {
        if (count < 2 || threads_.empty())
        {
            for (size_t i = 0; i < count; ++i)
                fn(i);
            return;
        }
        Job<Function> job{fn};
        {
            std::lock_guard lock(mutex_);
            job_ = &job;
            count_ = count;
            next_.store(0, std::memory_order_relaxed);
            active_ = threads_.size();
            ++generation_;
        }
        wake_.notify_all();
        drain(job, count);
        std::unique_lock lock(mutex_);
        done_.wait(lock, [this] { return active_ == 0; });
        job_ = nullptr;
    }

  private:
    struct JobBase
    {
        virtual void call(size_t index) = 0;

      protected:
        ~JobBase() = default;
    };
    template <typename Function> struct Job final : JobBase
    {
        explicit Job(Function &function) : function(function) {}
        void call(size_t index) override { function(index); }
        Function &function;
    };

    void drain(JobBase &job, size_t count)
    {
        for (size_t i; (i = next_.fetch_add(1, std::memory_order_relaxed)) < count;)
            job.call(i);
    }

    void work()
    {
        size_t seen = 0;
        std::unique_lock lock(mutex_);
        for (;;)
        {
            wake_.wait(lock, [&] { return stop_ || generation_ != seen; });
            if (stop_)
                return;
            seen = generation_;
            JobBase *job = job_;
            const size_t count = count_;
            lock.unlock();
            drain(*job, count);
            lock.lock();
            if (--active_ == 0)
                done_.notify_one();
        }
    }

    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable wake_, done_;
    JobBase *job_ = nullptr;
    size_t count_ = 0;
    size_t generation_ = 0;
    size_t active_ = 0;
    std::atomic<size_t> next_{0};
    bool stop_ = false;
};
} // namespace storm
