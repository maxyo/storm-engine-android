// Frame pacing of the game window: Android Frame Pacing (Swappy GL, Android Game SDK) presents each frame at
// a vsync of the display (bgfx::setEglPresent, patches/bgfx-swap-present-hook.patch).
//
// Rate: the settings' «Частота кадров» (storm::render::SetFrameRate, live; kDefaultFrame = 60 fps until set) at a
// whole number of display refreshes (swap_interval.hpp); Swappy may pick another display mode for it (60 fps on a
// 90/120 Hz panel: its 60 Hz mode or every second 120 Hz refresh). When frames take longer, Swappy's auto swap
// interval lowers the rate to a steady fraction of it, down to 30 fps (kSlowestFrame), and raises it again when
// they fit; it never paces faster than the setting.
//
// ADPF: the hint session's target is the setting's swap interval, not the interval Swappy fell back to (a
// lowered target would let the CPU clock down and keep the game at the lower rate); its actual work is the
// game thread's CPU time per frame, without the present wait measured here (platform/host_hooks.hpp).
// Swappy's CPU affinity is off: the scheduler and ADPF place the game thread.
//
// Without Swappy (its init failed or the device disables it) an error says why, and the present is a plain
// eglSwapBuffers at swap interval 1: the display's rate, unpaced; the frame times are logged the same way and
// ADPF aims at the setting's frame time.
//
// Every 30 s of presented frames the log gets a "frame pacing:" line with the frame intervals measured after
// each present (percentiles and a histogram in display refreshes) and the mean present wait. Swappy's own
// statistics stay off: they query the frame timestamps of every frame.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

namespace storm::shim
{

class FramePacing
{
  public:
    static constexpr std::chrono::nanoseconds kDefaultFrame{16'666'667};
    static constexpr std::chrono::nanoseconds kSlowestFrame{33'333'333};

    static FramePacing &instance();

    // Game thread, before bgfx::init of the game window (Android: Swappy with SDL's activity). `render_thread`:
    // bgfx presents on its render thread (the present function then runs there; everything else of this class is
    // called on the game thread).
    void start(bool render_thread);
    // Game thread: it was blocked this long handing a frame to the render thread, which is not work (ADPF). Without
    // a render thread the present itself, on the game thread, reports its wait.
    void game_thread_waited(std::chrono::nanoseconds wait);
    // Game thread: bgfx's render thread (Linux tid) shares the frame's work; ADPF's hint session takes it in.
    void render_thread_started(int tid);
    // Game thread: the swap interval the frame rate setting asks for (not one Swappy lowered the rate to); zero
    // when no frame rate is paced (desktop).
    std::chrono::nanoseconds preferred_interval() const;
    // Presenting thread: how long the presents since the last call waited (zero without pacing).
    std::chrono::nanoseconds take_present_wait();
    // The window the game presents to (Swappy reports the frame rate for it); nullptr while there is none.
    void set_window(void *native_window);
    // The frame rate to pace at, frames per second; applies at once (logged), before start() at its start.
    void set_frame_rate(uint32_t fps);
    void shutdown();

  private:
#ifdef __ANDROID__
    static bool present(void *display, void *surface);
    bool present_frame(void *display, void *surface);
    void choose_interval();
    void log_stats(double seconds);
    void reset_stats();

    bool started_ = false;
    bool render_thread_ = false; // present_frame runs on bgfx's render thread
    std::chrono::nanoseconds preferred_{0}; // game thread: the interval of the setting
    std::chrono::nanoseconds present_wait_{0}; // presenting thread
    std::chrono::nanoseconds target_ = kDefaultFrame;
    std::atomic<bool> swappy_{false}; // false: started without Swappy, presents with eglSwapBuffers
    // What follows is written by the present (render thread) and by the game thread's interval choice.
    std::mutex mutex_;
    std::chrono::nanoseconds swap_interval_{0}; // what Swappy paces at now
    std::chrono::nanoseconds refresh_period_{0};
    std::chrono::steady_clock::time_point last_present_{};
    std::chrono::steady_clock::time_point window_start_{};
    std::vector<float> intervals_ms_;
    double wait_ms_ = 0.0;
#endif
};

} // namespace storm::shim
