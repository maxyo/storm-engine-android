// The ray bakes' threads (town light, visibility, bounce, AO volume): a bake thread of their own, apart from the
// texture transcode workers, so a long or paused bake never holds up a transcode or a relief look-up; and a load
// thread for the bakes' cache reads, so a result in the cache never waits behind a running bake.
//
// A bake is posted with PostBake and runs on the bake thread, one after the other; its ParallelRanges spread it over
// BakeThreads() threads. A bake posted with a load stage runs that first on the load thread: found in the cache it is
// done there, otherwise its bake stage is queued on the bake thread. It is cancelled when its owner drops it (the BakeTicket, or the Bake<Job> that holds it, is
// reset or destroyed: the player left the place, the option was switched off) and by ShutdownBakes: a bake not
// started yet is not run, a running one stops at its next piece (ParallelRanges throws BakeCancelled, which the bake
// thread catches) and a bake waiting in the thermal pause is woken at once.
#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace storm::fx
{

// Runs `work(first, last)` over [0, count) on `threads` threads, in pieces of at least `min_piece` items (about 16 per
// thread); returns when all are done. The bakes over an Occluders (one Scratch per piece). The threads it starts run at
// nice 10 (Linux, Android): the game's main and render threads go first. Between pieces every thread waits while the
// device is hot (SetBakeThermal), the pause and the resume logged. On the bake thread a cancelled bake takes no
// further piece and throws BakeCancelled once the pieces already running are done.
void ParallelRanges(size_t count, unsigned threads, const std::function<void(size_t, size_t)> &work,
                    size_t min_piece = 256);
// The device's thermal status for ParallelRanges (true: hot; asked between pieces, so cheap), null: never pauses.
// While hot it is asked again every `pause_ms`; a cancelled bake leaves the pause at once.
void SetBakeThermal(bool (*hot)(), int pause_ms = 5000);
// The threads of a background bake: all cores but the game's main and render threads (at least one), as the relief
// generation takes them.
unsigned BakeThreads();

// Thrown by ParallelRanges in a bake that was cancelled; caught by the bake thread (logged there).
struct BakeCancelled
{
};

namespace detail
{
struct BakeState
{
    std::atomic<bool> cancelled{false};
};
} // namespace detail

// The owner's hold on a posted bake: dropping it (reset, assignment, destruction) cancels the bake.
class BakeTicket
{
  public:
    BakeTicket() = default;
    explicit BakeTicket(std::shared_ptr<detail::BakeState> state) : state_(std::move(state)) {}
    BakeTicket(BakeTicket &&other) noexcept = default;
    BakeTicket &operator=(BakeTicket &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            state_ = std::move(other.state_);
        }
        return *this;
    }
    BakeTicket(const BakeTicket &) = delete;
    BakeTicket &operator=(const BakeTicket &) = delete;
    ~BakeTicket() { reset(); }
    void reset();

  private:
    std::shared_ptr<detail::BakeState> state_;
};

enum class BakeStage
{
    Load, // on the load thread: the cache read; true when it filled the job, false to be baked
    Bake, // on the bake thread: the bake (returns true)
};
using BakeStep = std::function<bool(BakeStage)>;

// Main thread. Queues `work` on the bake thread (started on first use); `what` names it in the log.
BakeTicket PostBake(std::string what, std::function<void()> work);
// Main thread. `step(Load)` on the load thread, then, when it returns false, `step(Bake)` on the bake thread.
BakeTicket PostBake(std::string what, BakeStep step);
// Cancels every bake (queued or running, the thermal pause woken) and joins the bake thread; the next PostBake
// starts it again. Also run when the program ends.
void ShutdownBakes();

// A bake's result slot as its owner holds it: the job the bake writes into and the ticket that cancels the bake when
// the owner lets go of the job (reset, a new start, destruction).
template <class Job> class Bake
{
  public:
    explicit operator bool() const { return job_ != nullptr; }
    Job *operator->() const { return job_.get(); }
    Job &operator*() const { return *job_; }
    // `work` captures `job` and fills it on the bake thread.
    void start(std::string what, std::shared_ptr<Job> job, std::function<void()> work)
    {
        reset();
        job_ = std::move(job);
        ticket_ = PostBake(std::move(what), std::move(work));
    }
    // `step` captures `job`: its load stage fills it from the cache, or its bake stage bakes it (PostBake).
    void start(std::string what, std::shared_ptr<Job> job, BakeStep step)
    {
        reset();
        job_ = std::move(job);
        ticket_ = PostBake(std::move(what), std::move(step));
    }
    void reset()
    {
        ticket_.reset();
        job_.reset();
    }

  private:
    std::shared_ptr<Job> job_;
    BakeTicket ticket_;
};

} // namespace storm::fx
