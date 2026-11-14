#include "storm/fx/bakes.hpp"

#include "storm/fx/log.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace storm::fx
{
namespace
{
void LowerThreadPriority()
{
#if defined(__linux__)
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);
#endif
}

bool Cancelled(const detail::BakeState *bake)
{
    return bake && bake->cancelled.load(std::memory_order_relaxed);
}

// The bake the calling thread runs (the bake thread while it runs one; null elsewhere: never cancelled).
thread_local const detail::BakeState *t_bake = nullptr;

std::atomic<bool (*)()> g_hot{nullptr};
std::mutex g_pause;
std::condition_variable g_pause_wake; // a bake was cancelled: the pause ends for it
bool g_paused = false;
std::chrono::steady_clock::time_point g_paused_at;
std::atomic<int> g_pause_ms{5000};

void WakePausedBakes()
{
    {
        std::lock_guard lock(g_pause);
    }
    g_pause_wake.notify_all();
}

// Between two pieces of a bake: while the device is hot every bake thread waits, the pause logged once; a cancelled
// bake goes on at once (to its end: ParallelRanges takes no further piece).
void ThermalGate(const detail::BakeState *bake)
{
    bool (*hot)() = g_hot.load();
    if (!hot || !hot())
        return;
    std::unique_lock lock(g_pause);
    for (;;)
    {
        if (Cancelled(bake))
            return;
        if (!hot())
        {
            if (g_paused)
            {
                g_paused = false;
                Log(LogLevel::Info, "ray bakes resumed after %.1f s: the device cooled down",
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - g_paused_at).count());
            }
            return;
        }
        if (!g_paused)
        {
            g_paused = true;
            g_paused_at = std::chrono::steady_clock::now();
            Log(LogLevel::Info, "ray bakes paused: the device is hot (thermal status MODERATE or higher)");
        }
        g_pause_wake.wait_for(lock, std::chrono::milliseconds(g_pause_ms.load()), [&] { return Cancelled(bake); });
    }
}

class BakeQueue
{
  public:
    static BakeQueue &instance()
    {
        static BakeQueue queue;
        return queue;
    }
    ~BakeQueue() { shutdown(false); }

    BakeTicket post(std::string what, std::function<void()> work)
    {
        auto state = std::make_shared<detail::BakeState>();
        {
            std::lock_guard lock(mutex_);
            queue_.push_back({std::move(what), std::move(work), state});
            if (!thread_.joinable())
            {
                stopping_ = false;
                thread_ = std::thread(&BakeQueue::run, this);
            }
        }
        wake_.notify_one();
        return BakeTicket(std::move(state));
    }

    void shutdown(bool log)
    {
        size_t dropped = 0;
        bool running = false;
        {
            std::lock_guard lock(mutex_);
            if (!thread_.joinable())
                return;
            stopping_ = true;
            for (Task &task : queue_)
                dropped += task.state->cancelled.load() ? 0 : 1;
            queue_.clear();
            if (running_)
                running = !running_->cancelled.exchange(true);
        }
        wake_.notify_all();
        WakePausedBakes();
        thread_.join();
        thread_ = {};
        if (log && (dropped || running))
            Log(LogLevel::Info, "ray bakes: shut down, %zu queued not run, %s", dropped,
                running ? "the running one cancelled" : "none running");
    }

  private:
    struct Task
    {
        std::string what;
        std::function<void()> work;
        std::shared_ptr<detail::BakeState> state;
    };

    void run()
    {
        LowerThreadPriority();
        std::unique_lock lock(mutex_);
        for (;;)
        {
            wake_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
            if (stopping_)
                return;
            Task task = std::move(queue_.front());
            queue_.pop_front();
            if (task.state->cancelled.load())
                continue; // its owner let go of it before it started: nothing reads it
            running_ = task.state;
            lock.unlock();
            t_bake = task.state.get();
            try
            {
                task.work();
            }
            catch (const BakeCancelled &)
            {
                Log(LogLevel::Info, "ray bake %s: cancelled while running (its owner let go of it, or the bakes shut down)",
                    task.what.c_str());
            }
            t_bake = nullptr;
            task = {}; // its captures (the geometry's copies) go here, not with the next bake
            lock.lock();
            running_.reset();
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Task> queue_;
    std::shared_ptr<detail::BakeState> running_;
    std::thread thread_;
    bool stopping_ = false;
};
} // namespace

void BakeTicket::reset()
{
    if (!state_)
        return;
    if (!state_->cancelled.exchange(true))
        WakePausedBakes();
    state_.reset();
}

BakeTicket PostBake(std::string what, std::function<void()> work)
{
    return BakeQueue::instance().post(std::move(what), std::move(work));
}

void ShutdownBakes()
{
    BakeQueue::instance().shutdown(true);
}

void SetBakeThermal(bool (*hot)(), int pause_ms)
{
    g_hot = hot;
    g_pause_ms = pause_ms;
}

void ParallelRanges(size_t count, unsigned threads, const std::function<void(size_t, size_t)> &work, size_t min_piece)
{
    const detail::BakeState *bake = t_bake;
    min_piece = std::max<size_t>(1, min_piece);
    threads = std::max(1u, std::min<unsigned>(threads, static_cast<unsigned>(std::max<size_t>(1, count / min_piece))));
    // Pieces of at least min_piece items, about 16 per thread, taken in turn: the thermal gate sits between them.
    const size_t piece = std::max<size_t>(min_piece, (count + threads * 16 - 1) / (threads * 16));
    std::atomic<size_t> next{0};
    auto run = [&] {
        for (;;)
        {
            if (Cancelled(bake))
                return;
            const size_t first = next.fetch_add(piece);
            if (first >= count)
                return;
            ThermalGate(bake);
            if (Cancelled(bake))
                return;
            work(first, std::min(count, first + piece));
        }
    };
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < threads; ++t)
        pool.emplace_back([&] {
            LowerThreadPriority();
            run();
        });
    // A ParallelRanges inside `work` on this thread throws when the bake is cancelled: the threads are joined first.
    std::exception_ptr failure;
    try
    {
        run();
    }
    catch (...)
    {
        failure = std::current_exception();
        next = count;
    }
    for (std::thread &th : pool)
        th.join();
    if (failure)
        std::rethrow_exception(failure);
    if (Cancelled(bake))
        throw BakeCancelled{};
}

unsigned BakeThreads()
{
    const unsigned cores = std::thread::hardware_concurrency();
    return cores > 2 ? cores - 2 : 1;
}

} // namespace storm::fx
