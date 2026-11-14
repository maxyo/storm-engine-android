// Frame pacing of the game window: Android Frame Pacing (Swappy, Android Game SDK) presents each frame at a vsync
// of the display, on either backend: Swappy GL in place of eglSwapBuffers (bgfx::setEglPresent,
// patches/bgfx-swap-present-hook.patch), Swappy Vk in place of vkQueuePresentKHR (bgfx::setVkPresentHooks,
// patches/bgfx-vk-present-hook.patch). Everything below is the same code for both; only the Swappy calls differ.
// Swappy Vk works per swap chain: it is set up when bgfx has created one (on the presenting thread) and the
// interval is chosen for it at the game thread's next frame.
//
// Rate: the settings' «Частота кадров» (storm::render::SetFrameRate, live; kDefaultFrame = 60 fps until set) at a
// whole number of display refreshes (swap_interval.hpp); Swappy may pick another display mode for it (60 fps on a
// 90/120 Hz panel: its 60 Hz mode or every second 120 Hz refresh). When frames take longer, Swappy's auto swap
// interval lowers the rate to a steady fraction of it, down to 30 fps (kSlowestFrame), and raises it again when
// they fit; it never paces faster than the setting. Frames that do not fit even the slowest let Swappy step past it,
// where it stops pacing (swap_interval.hpp): logged as unpaced, not as a frame rate. About a second after a choice the
// display's refresh rate (Android Display) is compared with the chosen mode; a display that stayed in another mode
// gets a new choice among whole refreshes of the period it runs (logged as an error).
//
// Swappy's own ladder (auto swap interval) stays: it decides from the CPU and GPU (fence) time of each frame, which the
// game does not see; an interval choice of our own with hysteresis would have only the present intervals.
//
// ADPF: the hint session's target is the setting's swap interval, not the interval Swappy fell back to (a
// lowered target would let the CPU clock down and keep the game at the lower rate); its actual work is the
// game thread's CPU time per frame, without the present wait measured here (platform/host_hooks.hpp).
// Swappy's CPU affinity is off: the scheduler and ADPF place the game thread.
//
// Without Swappy (its init failed or the device disables it) an error says why, and the present is a plain
// eglSwapBuffers at swap interval 1 / vkQueuePresentKHR on the FIFO swap chain: the display's rate, unpaced; the
// frame times are logged the same way and ADPF aims at the setting's frame time.
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
    // bgfx presents on its render thread (the present functions then run there; everything else of this class is
    // called on the game thread). `vulkan`: the device is started on bgfx's Vulkan backend.
    void start(bool render_thread, bool vulkan);
    // Game thread, once per presented frame: takes in what the presenting thread reported (a new Vulkan swap
    // chain: the swap interval is chosen for it).
    void game_frame();
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
    // Game thread: while on, every present interval is also kept for take_intervals() (the automatic graphics
    // preset's measurement, renderer auto_preset.hpp); off drops what was kept.
    void set_sampling(bool on);
    // Game thread: the present intervals (ms) kept since the last call.
    std::vector<float> take_intervals();
    void shutdown();

  private:
#ifdef __ANDROID__
    static bool present(void *display, void *surface);
    // bgfx::VkPresentHooks (presenting thread).
    static void vk_swap_chain_created(void *physical_device, void *device, void *queue, uint32_t queue_family,
                                      uint64_t swap_chain, void *native_window);
    static void vk_swap_chain_destroying(void *device, uint64_t swap_chain);
    static int32_t vk_present(void *queue, const void *present_info);
    // Presenting thread: the present took from `before` to `after`.
    void presented(std::chrono::steady_clock::time_point before, std::chrono::steady_clock::time_point after);
    // What Swappy paces at now and the display's refresh period, for the backend in use.
    std::chrono::nanoseconds swappy_interval();
    std::chrono::nanoseconds swappy_refresh();
    // `display_period` (ns) > 0: the display did not enter the chosen mode; choose among whole refreshes of it.
    void choose_interval(uint64_t display_period = 0);
    // Game thread, kModeCheckDelay after a choice: the display runs the chosen mode, else choose again.
    void check_display_mode();
    // Any presenting path without Swappy: says why as an error, reads the display's rate for the statistics.
    void unpaced(const char *why);
    void log_stats(double seconds);
    void reset_stats();

    bool started_ = false;
    bool render_thread_ = false; // the presents run on bgfx's render thread
    bool vulkan_ = false;        // Swappy Vk; false: Swappy GL
    // Vulkan: the swap chain Swappy Vk paces (zero: none) and its device, set by the presenting thread; the game
    // thread's Swappy calls for it hold chain_mutex_ so the swap chain cannot go meanwhile.
    std::mutex chain_mutex_;
    void *vk_device_ = nullptr;
    uint64_t vk_swap_chain_ = 0;
    uint64_t vk_refresh_ns_ = 0;             // SwappyVk_initAndGetRefreshCycleDuration
    uint64_t vk_interval_ns_ = 0;            // the interval the game thread chose last; a new swap chain starts on it
    uint64_t vk_threshold_ns_ = 0;           // Swappy's max auto swap interval for it
    std::atomic<bool> chain_changed_{false}; // a new swap chain: the game thread chooses its interval
    void *vulkan_library_ = nullptr;         // the loader, for vkQueuePresentKHR without Swappy
    int32_t (*vk_queue_present_)(void *queue, const void *present_info) = nullptr;
    std::chrono::nanoseconds preferred_{0}; // game thread: the interval of the setting
    static constexpr std::chrono::seconds kModeCheckDelay{1};
    uint64_t mode_check_period_ = 0; // game thread: the mode period to check at mode_check_at_; 0: none
    std::chrono::steady_clock::time_point mode_check_at_{};
    std::chrono::nanoseconds present_wait_{0}; // presenting thread
    std::chrono::nanoseconds target_ = kDefaultFrame;
    std::atomic<bool> swappy_{false}; // false: no Swappy, presents with eglSwapBuffers / vkQueuePresentKHR
    // What follows is written by the present (render thread) and by the game thread's interval choice.
    std::mutex mutex_;
    std::chrono::nanoseconds swap_interval_{0}; // what Swappy paces at now
    std::chrono::nanoseconds threshold_{0};     // Swappy paces intervals up to it (setMaxAutoSwapIntervalNS)
    std::chrono::nanoseconds refresh_period_{0};
    std::chrono::steady_clock::time_point last_present_{};
    std::chrono::steady_clock::time_point window_start_{};
    std::vector<float> intervals_ms_;
    double wait_ms_ = 0.0;
    bool sampling_ = false;
    std::vector<float> samples_ms_; // set_sampling / take_intervals
#endif
};

} // namespace storm::shim
