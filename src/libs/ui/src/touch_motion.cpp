#include "touch_motion.h"

#include <RmlUi/Core/ComputedValues.h>

#include <algorithm>
#include <cmath>

namespace storm::ui
{

namespace
{

bool Scrolls(Rml::Style::Overflow overflow)
{
    return overflow == Rml::Style::Overflow::Auto || overflow == Rml::Style::Overflow::Scroll;
}

// The innermost box under the finger that scrolls along an axis now (its content is larger).
Rml::Element *ScrollingBox(Rml::Element *element, bool axis[2])
{
    for (; element; element = element->GetParentNode())
    {
        const Rml::Style::ComputedValues &values = element->GetComputedValues();
        axis[0] = Scrolls(values.overflow_x()) && element->GetScrollWidth() > element->GetClientWidth() + 0.5f;
        axis[1] = Scrolls(values.overflow_y()) && element->GetScrollHeight() > element->GetClientHeight() + 0.5f;
        if (axis[0] || axis[1])
            return element;
    }
    return nullptr;
}

Rml::Vector2f Limit(Rml::Element *box)
{
    return {std::max(0.0f, box->GetScrollWidth() - box->GetClientWidth()), std::max(0.0f, box->GetScrollHeight() - box->GetClientHeight())};
}

} // namespace

void TouchScroll::Down(Rml::Element *target, Rml::Vector2f at, double time, float density, float slop)
{
    bool axis[2] = {false, false};
    Rml::Element *box = ScrollingBox(target, axis);
    caught_ = box && box == box_.get() && Flinging();
    fling_[0].Stop();
    fling_[1].Stop();
    box_ = box ? box->GetObserverPtr() : Rml::ObserverPtr<Rml::Element>();
    down_ = box != nullptr;
    dragging_ = caught_;
    if (!box)
        return;
    axis_[0] = axis[0], axis_[1] = axis[1];
    if (!caught_) // a caught box goes on from its exact position
        position_ = {box->GetScrollLeft(), box->GetScrollTop()};
    density_ = density;
    slop_ = slop;
    start_ = last_ = at;
    tracker_.Clear();
    tracker_.Add(time, {at.x, at.y});
}

bool TouchScroll::Move(Rml::Vector2f at, double time)
{
    Rml::Element *box = box_.get();
    if (!down_ || !box)
        return false;
    tracker_.Add(time, {at.x, at.y});
    if (!dragging_)
    {
        const Rml::Vector2f travel = at - start_;
        const float along = std::max(axis_[0] ? std::fabs(travel.x) : 0.0f, axis_[1] ? std::fabs(travel.y) : 0.0f);
        if (along <= slop_)
            return false;
        // The content moves by the travel beyond the slop (RecyclerView): 1:1 from there.
        dragging_ = true;
        for (int i = 0; i < 2; ++i)
            last_[i] = std::fabs(travel[i]) > slop_ ? start_[i] + std::copysign(slop_, travel[i]) : at[i];
    }
    const Rml::Vector2f limit = Limit(box);
    for (int i = 0; i < 2; ++i)
        if (axis_[i])
            position_[i] = std::clamp(position_[i] - (at[i] - last_[i]), 0.0f, limit[i]);
    last_ = at;
    Place();
    return true;
}

void TouchScroll::Up(double time, bool cancelled)
{
    const bool fling = down_ && dragging_ && !cancelled && box_;
    down_ = dragging_ = caught_ = false;
    if (!fling)
        return;
    tracker_.Lift(time);
    const touch::Point velocity = tracker_.Velocity();
    for (int i = 0; i < 2; ++i)
    {
        const float speed = -velocity[i]; // the content moves against the finger's scroll offset
        if (!axis_[i] || std::fabs(speed) < kMinFlingDp * density_)
            continue;
        fling_[i].Start(time, position_[i], std::clamp(speed, -kMaxFlingDp * density_, kMaxFlingDp * density_), density_);
    }
}

bool TouchScroll::Update(double time)
{
    Rml::Element *box = box_.get();
    if (!Flinging())
        return false;
    // Gone, or scrolled by something else meanwhile (ScrollTo, a rebuild): the fling ends.
    if (!box || std::fabs(box->GetScrollLeft() - std::round(position_.x)) > 1.0f || std::fabs(box->GetScrollTop() - std::round(position_.y)) > 1.0f)
    {
        fling_[0].Stop();
        fling_[1].Stop();
        return false;
    }
    const Rml::Vector2f limit = Limit(box);
    for (int i = 0; i < 2; ++i)
    {
        if (!fling_[i].Active())
            continue;
        const float at = fling_[i].Advance(time);
        position_[i] = std::clamp(at, 0.0f, limit[i]);
        if (position_[i] != at) // reached an edge: the fling ends there
            fling_[i].Stop();
    }
    Place();
    return true; // this frame moved; the next Update reports the end
}

void TouchScroll::Place()
{
    Rml::Element *box = box_.get();
    if (!box)
        return;
    if (axis_[0])
        box->SetScrollLeft(position_.x);
    if (axis_[1])
        box->SetScrollTop(position_.y);
}

} // namespace storm::ui
