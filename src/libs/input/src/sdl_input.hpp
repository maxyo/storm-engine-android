#pragma once

#include <input.hpp>
#include <SDL2/SDL.h>
#include <map>
#include <mutex>
#include <vector>

#include "platform/platform.hpp"
#ifdef __ANDROID__
#include <atomic>
#include "frame_buttons.hpp"
#endif

namespace storm
{
class SDLInput : public Input
{
  public:
    SDLInput();
    ~SDLInput() override;

    void PumpEvents() override;
#ifdef __ANDROID__
    // Thread-safe cancellation boundary used after Java releases its holders.
    static void CancelPendingInput();
#endif
    int Subscribe(const EventHandler &handler) override;
    void Unsubscribe(int id) override;

    bool KeyboardModState(const KeyboardKey &key) const override;
    bool KeyboardKeyState(const KeyboardKey &key) const override;
    bool KeyboardSDLKeyState(const SDL_Scancode &key) const override;
    bool MouseKeyState(const MouseKey &key) const override;
    bool SetRelativeMouseMode(bool enabled) override;

    uint32_t GetWheelFactor() const override;

    bool ControllerButtonState(const ControllerButton &button) const override;
    int ControllerAxisValue(const ControllerAxis &axis) const override;

  private:
    static int SDLCALL SDLEventHandler(void *userdata, SDL_Event *evt);
    void ProcessEvent(const SDL_Event &event);
    void OpenController();

private:
#ifdef __ANDROID__
    bool SnapshotCurrent() const
    {
        return snapshotGeneration_ == inputGeneration_.load(std::memory_order_acquire);
    }
    std::mutex pendingMutex_;
    std::vector<SDL_Event> pendingEvents_;
    // Producers invalidate snapshots without touching engine-thread button data.
    std::atomic<uint64_t> inputGeneration_{0};
    uint64_t snapshotGeneration_ = 0;
    FrameButtons<SDL_NUM_SCANCODES> keyboard_;
    FrameButtons<6> mouse_;
    bool focused_ = true;
    bool foreground_ = true;
#endif
    std::map<int, EventHandler> handlers_;
    const uint8_t *keyStates_;
    std::unique_ptr<SDL_GameController, std::function<void(SDL_GameController *)>> controller_;
    SDL_JoystickID joyID_;
};
} // namespace storm
