// Touch scrolling of the UI's boxes. Lists (TouchScroll) and the map's pan (behaviour panzoom)
// use the finger physics shared with the game cameras (util touch_physics.hpp: Android's
// VelocityTracker and OverScroller), so every drag follows the finger and coasts alike.
//
// Our own code rather than RmlUi's touch scrolling: RmlUi 6.3 takes no sample times (its
// velocity comes from when the engine loop got to the events), integrates its inertia per
// frame with a linear friction and a delta clamp (the travel depends on the frame rate), and
// rounds every step to whole pixels (slow motion stalls, then stops at a cut-off).
#pragma once

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Vector2.h>

#include "touch_physics.hpp"

namespace storm::ui
{

using touch::Fling;
using touch::kMaxFlingDp;
using touch::kMinFlingDp;
using touch::VelocityTracker;

// Drags and flings of RmlUi's scrolling boxes (overflow auto/scroll) by touch, for one pointer
// at a time. The finger moves the content 1:1 from where the drag starts (beyond the slop);
// every sample of the pointer counts for the release velocity; the fling then runs on the
// frame clock (Update). A pointer that goes down on a box in motion stops it and drags it at
// once (a caught fling is never a tap). The content stops at its edges; a fling that reaches
// one ends there.
class TouchScroll
{
  public:
    // `slop` in pixels: how far the finger travels before a drag starts.
    void Down(Rml::Element *target, Rml::Vector2f at, double time, float density, float slop);
    // True from the sample that starts the drag on (the press under the finger is withdrawn).
    bool Move(Rml::Vector2f at, double time);
    void Up(double time, bool cancelled);
    // Moves a flinging box to where it is at `time`; true while one is in motion.
    bool Update(double time);
    // Down caught a box in motion (the touch is a drag from its start).
    bool Caught() const
    {
        return caught_;
    }
    bool Dragging() const
    {
        return dragging_;
    }
    bool Flinging() const
    {
        return fling_[0].Active() || fling_[1].Active();
    }

  private:
    // Sets the box's scroll offset (RmlUi keeps whole pixels) from the exact position.
    void Place();

    Rml::ObserverPtr<Rml::Element> box_;
    bool axis_[2] = {false, false}; // the box scrolls along x / y
    Rml::Vector2f position_;        // exact scroll offset (the box shows it rounded)
    Rml::Vector2f last_, start_;
    float density_ = 1.0f, slop_ = 0.0f;
    bool down_ = false, dragging_ = false, caught_ = false;
    VelocityTracker tracker_;
    Fling fling_[2];
};

} // namespace storm::ui
