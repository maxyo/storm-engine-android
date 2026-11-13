// How far the game thread may run ahead of bgfx's render thread (Context::present, host-tested in
// tests/frame_pacing).
//
// The render thread presents a frame after the game thread handed it over, and the present waits for the frame's
// vsync slot. After handing over frame N the game thread waits for a frame to be presented before it simulates
// frame N+1:
// - serial: frame N itself. Simulation and rendering take turns, as without a render thread; a frame is shown
//   one swap interval after its input was read. It fits while game work + render work fit the interval.
// - overlapped: frame N-1. Frame N+1 is simulated while frame N is rendered; each kind of work has a whole
//   interval of its own, and a frame is shown two intervals after its input was read.
// So frames overlap only while they would not fit otherwise: where they fit, overlap adds a frame of input
// latency and gains nothing. The interval is the one the frame rate setting asks for, not one Swappy lowered
// the rate to: at a lowered rate everything fits, and the rate could never rise again.
#pragma once

#include <algorithm>

namespace storm::shim
{

class FrameOverlap
{
  public:
    static constexpr double kEnter = 0.85;      // smoothed (game + render) / interval above which frames overlap
    static constexpr double kLeave = 0.60;      // ... and below which they may stop overlapping,
    static constexpr int kCalmFrames = 180;     // after this many frames in a row, none above kCalmPeak
    static constexpr double kCalmPeak = 0.75;
    static constexpr double kSmoothing = 0.2;   // weight of the newest frame in the smoothed load
    static constexpr double kHitch = 1.5;       // what one frame can add to it at most

    // One presented frame: the game thread's work and the render thread's work for a frame (without their
    // waits), and the swap interval aimed at, all in the same unit. Returns true when the choice changed.
    // `interval` <= 0 (no known frame rate: desktop, no pacing): overlapped, the most frames.
    bool update(double game, double render, double interval)
    {
        const bool before = overlapped_;
        if (interval <= 0.0)
        {
            overlapped_ = true;
            load_ = 0.0;
            return before != overlapped_;
        }
        const double load = (game + render) / interval;
        // A single hitch (a load, a shader link) counts as one frame that did not fit, not as a lasting load.
        load_ += kSmoothing * (std::min(load, kHitch) - load_);
        if (!overlapped_)
        {
            // Two frames in a row that did not fit, or a load that stays high.
            if (load_ > kEnter || (load > 1.0 && last_ > 1.0))
            {
                overlapped_ = true;
                calm_ = 0;
            }
        }
        else
        {
            calm_ = load < kCalmPeak && load_ < kLeave ? calm_ + 1 : 0;
            if (calm_ >= kCalmFrames)
                overlapped_ = false;
        }
        last_ = load;
        return before != overlapped_;
    }

    bool overlapped() const { return overlapped_; }
    double load() const { return load_; } // smoothed (game + render) / interval

  private:
    bool overlapped_ = false;
    double load_ = 0.0, last_ = 0.0;
    int calm_ = 0;
};

} // namespace storm::shim
