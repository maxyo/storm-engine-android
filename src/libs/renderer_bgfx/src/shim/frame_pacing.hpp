// Frame pacing of the game window: Android Frame Pacing (Swappy GL, Android Game SDK) presents each frame at
// a vsync of the display (bgfx::setEglPresent, patches/bgfx-swap-present-hook.patch).
//
// Rate: 60 fps (kTargetFrame) at a whole number of display refreshes; Swappy may pick another display mode
// for it (a 90 Hz panel switches to its 60 Hz mode where it has one; on 120 Hz every second refresh). When
// frames take longer, Swappy's auto swap interval lowers the rate to a steady fraction of it, down to 30 fps
// (kSlowestFrame), and raises it again when they fit; it never paces faster than kTargetFrame.
//
// ADPF: the hint session's target is kTargetFrame's swap interval, not the interval Swappy fell back to (a
// lowered target would let the CPU clock down and keep the game at the lower rate); its actual work is the
// game thread's CPU time per frame, without the present wait measured here (platform/host_hooks.hpp).
// Swappy's CPU affinity is off: the scheduler and ADPF place the game thread.
//
// Without Swappy (its init failed or the device disables it) an error says why, and bgfx swaps with
// eglSwapBuffers at swap interval 1: the display's rate, unpaced.
//
// Every 30 s of presented frames the log gets a "frame pacing:" line with the frame intervals measured after
// each present (percentiles and a histogram in display refreshes) and the mean present wait. Swappy's own
// statistics stay off: they query the frame timestamps of every frame.
#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

namespace storm::shim
{

class FramePacing
{
  public:
    static constexpr std::chrono::nanoseconds kTargetFrame{16'666'667};
    static constexpr std::chrono::nanoseconds kSlowestFrame{33'333'333};

    static FramePacing &instance();

    // Before bgfx::init of the game window, on the thread that renders (Android: Swappy with SDL's activity).
    void start();
    // The window the game presents to (Swappy reports the frame rate for it); nullptr while there is none.
    void set_window(void *native_window);
    void shutdown();

  private:
#ifdef __ANDROID__
    static bool present(void *display, void *surface);
    bool present_frame(void *display, void *surface);
    void choose_interval();
    void log_stats(double seconds);
    void reset_stats();

    bool swappy_ = false;
    std::chrono::nanoseconds swap_interval_{0}; // what Swappy paces at now
    std::chrono::nanoseconds refresh_period_{0};
    std::chrono::steady_clock::time_point last_present_{};
    std::chrono::steady_clock::time_point window_start_{};
    std::vector<float> intervals_ms_;
    double wait_ms_ = 0.0;
#endif
};

} // namespace storm::shim
