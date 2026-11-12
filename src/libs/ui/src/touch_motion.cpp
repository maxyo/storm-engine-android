#include "touch_motion.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Transform.h>
#include <RmlUi/Core/TransformPrimitive.h>

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
    const bool same = box && box == box_.get();
    caught_ = same && Flinging();
    fling_[0].Stop();
    fling_[1].Stop();
    // A finger on a stretched box holds the stretch where it is and drags on from it; any
    // other box lets go of its stretch at once.
    for (int i = 0; i < 2; ++i)
    {
        if (same && stretch_[i].Value() != 0.0f)
        {
            pull_[i] = stretch_[i].PullFor(Size(i));
            stretch_[i].Pull(pull_[i], Size(i));
            caught_ = true;
        }
        else
        {
            pull_[i] = 0.0f;
            stretch_[i].Stop();
        }
    }
    if (!same)
        Stretch(time);
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
        {
            // The travel goes into the pull past an edge first, then into the scroll offset.
            const float wanted = position_[i] + pull_[i] - (at[i] - last_[i]);
            position_[i] = std::clamp(wanted, 0.0f, limit[i]);
            pull_[i] = wanted - position_[i];
            stretch_[i].Pull(pull_[i], Size(i));
        }
    last_ = at;
    Place();
    Stretch(time);
    return true;
}

void TouchScroll::Up(double time, bool cancelled)
{
    const bool fling = down_ && dragging_ && !cancelled && box_;
    down_ = dragging_ = caught_ = false;
    // A stretch springs back, whatever ended the drag; a stretched axis does not fling.
    bool stretched[2] = {false, false};
    for (int i = 0; i < 2; ++i)
    {
        stretched[i] = stretch_[i].Value() != 0.0f;
        if (stretched[i])
            stretch_[i].Release(time);
        pull_[i] = 0.0f;
    }
    last_update_ = time;
    if (!fling)
        return;
    tracker_.Lift(time);
    const touch::Point velocity = tracker_.Velocity();
    for (int i = 0; i < 2; ++i)
    {
        const float speed = -velocity[i]; // the content moves against the finger's scroll offset
        if (!axis_[i] || stretched[i] || std::fabs(speed) < kMinFlingDp * density_)
            continue;
        fling_[i].Start(time, position_[i], std::clamp(speed, -kMaxFlingDp * density_, kMaxFlingDp * density_), density_);
    }
}

bool TouchScroll::Update(double time)
{
    Rml::Element *box = box_.get();
    const bool springing = stretch_[0].Springing() || stretch_[1].Springing();
    if (!Flinging() && !springing)
        return false;
    // Gone, or scrolled by something else meanwhile (ScrollTo, a rebuild): the motion ends.
    if (!box || std::fabs(box->GetScrollLeft() - std::round(position_.x)) > 1.0f || std::fabs(box->GetScrollTop() - std::round(position_.y)) > 1.0f)
    {
        fling_[0].Stop();
        fling_[1].Stop();
        stretch_[0].Stop();
        stretch_[1].Stop();
        Stretch(time);
        return false;
    }
    const Rml::Vector2f limit = Limit(box);
    for (int i = 0; i < 2; ++i)
    {
        if (!fling_[i].Active())
            continue;
        const float before = position_[i];
        const float at = fling_[i].Advance(time);
        position_[i] = std::clamp(at, 0.0f, limit[i]);
        if (position_[i] != at) // reached an edge: the fling ends there, its momentum stretches the content
        {
            fling_[i].Stop();
            const double elapsed = time - last_update_;
            if (elapsed > 0.0)
                stretch_[i].Absorb(time, static_cast<float>((at - before) / elapsed), Size(i));
        }
    }
    last_update_ = time;
    Place();
    Stretch(time);
    return true; // this frame moved; the next Update reports the end
}

float TouchScroll::Size(int axis) const
{
    const Rml::Element *box = box_.get();
    return box ? (axis == 0 ? const_cast<Rml::Element *>(box)->GetClientWidth() : const_cast<Rml::Element *>(box)->GetClientHeight()) : 0.0f;
}

void TouchScroll::Stretch(double time)
{
    Rml::Element *box = box_.get();
    const float amount[2] = {stretch_[0].At(time), stretch_[1].At(time)};
    if (!box || (amount[0] == 0.0f && amount[1] == 0.0f))
    {
        for (const auto &row : stretched_)
            if (Rml::Element *element = row.get())
                element->RemoveProperty(Rml::PropertyId::Transform);
        stretched_.clear();
        return;
    }
    // The rows: the children of the box, or of its only child (a list's wrapper), down to
    // where there are several. The runtime's edge fade is none of them.
    const auto rows_of = [](Rml::Element *element) {
        std::vector<Rml::Element *> rows;
        for (int i = 0; i < element->GetNumChildren(); ++i)
            if (Rml::Element *child = element->GetChild(i); !child->IsClassSet("scroll-fade"))
                rows.push_back(child);
        return rows;
    };
    std::vector<Rml::Element *> rows = rows_of(box);
    while (rows.size() == 1)
        rows = rows_of(rows.front());
    // Each row moves away from the far edge by the stretch times its distance from it: the
    // edge pulled moves the most, the far one stays.
    const Rml::Vector2f origin = box->GetAbsoluteOffset(Rml::BoxArea::Padding);
    const float size[2] = {Size(0), Size(1)};
    std::vector<Rml::ObserverPtr<Rml::Element>> stretched;
    for (Rml::Element *row : rows)
    {
        const bool ours = std::any_of(stretched_.begin(), stretched_.end(), [row](const auto &held) { return held.get() == row; });
        if (!ours && row->GetComputedValues().has_local_transform())
        {
            if (!warned_)
                Rml::Log::Message(Rml::Log::LT_WARNING, "Scroll stretch: a row with a transform of its own keeps it (%s)", row->GetAddress().c_str());
            warned_ = true;
            continue;
        }
        const Rml::Vector2f at = row->GetAbsoluteOffset(Rml::BoxArea::Border) + row->GetBox().GetSize(Rml::BoxArea::Border) * 0.5f - origin;
        float shift[2] = {0.0f, 0.0f};
        for (int i = 0; i < 2; ++i)
        {
            const float centre = std::clamp(at[i], 0.0f, size[i]);
            shift[i] = amount[i] < 0.0f ? -amount[i] * (size[i] - centre) : -amount[i] * centre;
        }
        Rml::TransformPtr transform = Rml::MakeShared<Rml::Transform>();
        transform->AddPrimitive(Rml::Transforms::Translate2D(shift[0], shift[1], Rml::Unit::PX));
        row->SetProperty(Rml::PropertyId::Transform, Rml::Property(transform, Rml::Unit::TRANSFORM));
        stretched.push_back(row->GetObserverPtr());
    }
    stretched_ = std::move(stretched);
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
