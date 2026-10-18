#pragma once

#include <array>
#include <cstddef>

namespace storm
{
// An Android producer can deliver down and up between two engine frames. Keep
// one press visible for the whole consuming frame, independently of final held
// state. Multiple taps in one frame intentionally coalesce; there is no backlog.
template <std::size_t Count> class FrameButtons
{
  public:
    void BeginFrame() { pressed_.fill(false); }

    void Set(std::size_t button, bool down, bool repeat = false)
    {
        if (button >= Count || repeat) return;
        if (down && !held_[button]) pressed_[button] = true;
        held_[button] = down;
    }

    bool Active(std::size_t button) const
    {
        return button < Count && (held_[button] || pressed_[button]);
    }

    void Clear()
    {
        held_.fill(false);
        pressed_.fill(false);
    }

  private:
    std::array<bool, Count> held_{};
    std::array<bool, Count> pressed_{};
};
} // namespace storm
