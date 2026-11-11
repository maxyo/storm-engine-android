#pragma once

// Hover triggers of Caribbean Legend interfaces (the TRIGGER node, nodes with UseTrigger = 1).
//
// While the mouse rests on a trigger node, the script first gets a delay (event
// Event_GetTriggerDelay, seconds; a negative delay disables the trigger for this hover). When it has
// passed, the node is announced (Event_NodeStartTrigger: the script draws a highlight frame) and the
// TRIGGER node fills its circle for its `time`; when the circle is full the node is confirmed
// (Event_NodeEndTrigger: the script opens the node's info window). Leaving a started node, or the
// TRIGGER node being switched off, cancels it (Event_NullTrigger: frame and info window go away).
// Leaving before the start emits nothing. Derived from how the CL scripts answer these events.

#include <functional>
#include <string>
#include <vector>

namespace storm::xinterface
{
class HoverTrigger
{
  public:
    enum class Event
    {
        Start,  // Event_NodeStartTrigger(node)
        End,    // Event_NodeEndTrigger(node)
        Cancel, // Event_NullTrigger()
    };
    struct Emitted
    {
        Event event;
        std::string node;
    };

    // `hovered` is the trigger node under the mouse ("" for none); `delay` asks the script.
    std::vector<Emitted> update(const std::string &hovered, float seconds, float fillTime,
                                const std::function<float()> &delay)
    {
        std::vector<Emitted> out;
        if (hovered != node_)
        {
            cancel(out);
            node_ = hovered;
            if (!node_.empty())
            {
                remaining_ = delay();
                state_ = remaining_ < 0.f ? State::Disabled : State::Waiting;
                seconds = 0.f; // the hover starts now
            }
        }
        if (state_ == State::Waiting)
        {
            remaining_ -= seconds;
            if (remaining_ > 0.f)
                return out;
            out.push_back({Event::Start, node_});
            state_ = State::Filling;
            elapsed_ = 0.f;
            seconds = -remaining_; // the part of this frame after the start
        }
        if (state_ == State::Filling)
        {
            elapsed_ += seconds;
            if (elapsed_ >= fillTime)
            {
                out.push_back({Event::End, node_});
                state_ = State::Done;
            }
        }
        return out;
    }

    // Stops a hover in progress (the TRIGGER node was switched off or released).
    std::vector<Emitted> reset()
    {
        std::vector<Emitted> out;
        cancel(out);
        node_.clear();
        return out;
    }

    // Fill of the circle, 0..1, or a negative value when no circle is shown.
    float progress(float fillTime) const
    {
        if (state_ != State::Filling)
            return -1.f;
        return fillTime > 0.f ? elapsed_ / fillTime : 1.f;
    }

  private:
    enum class State
    {
        Idle,
        Disabled,
        Waiting,
        Filling,
        Done,
    };

    void cancel(std::vector<Emitted> &out)
    {
        if (state_ == State::Filling || state_ == State::Done)
            out.push_back({Event::Cancel, node_});
        state_ = State::Idle;
    }

    std::string node_;
    State state_ = State::Idle;
    float remaining_ = 0.f;
    float elapsed_ = 0.f;
};
} // namespace storm::xinterface
