#include "touch_motion.h"

#include <RmlUi/Core/ComputedValues.h>

#include <algorithm>
#include <cmath>

namespace storm::ui
{

namespace
{

// VelocityTracker
constexpr double kHorizon = 0.100;       // seconds of history the fit uses
constexpr double kAssumeStopped = 0.040; // a gap this long means the pointer had stopped

// OverScroller (SplineOverScroller)
constexpr double kScrollFriction = 0.015; // ViewConfiguration.getScrollFriction()
constexpr double kGravity = 9.80665;      // SensorManager.GRAVITY_EARTH
constexpr double kInflexion = 0.35;
constexpr double kStartTension = 0.5, kEndTension = 1.0;
constexpr int kSamples = 100;
const double kDecelerationRate = std::log(0.78) / std::log(0.9);

// SPLINE_POSITION: the distance travelled (0..1) at each hundredth of the duration.
const std::array<float, kSamples + 1> &SplinePosition()
{
    static const std::array<float, kSamples + 1> table = [] {
        std::array<float, kSamples + 1> out{};
        const float p1 = kStartTension * kInflexion, p2 = 1.0f - kEndTension * (1.0f - kInflexion);
        float x_min = 0.0f;
        for (int i = 0; i < kSamples; ++i)
        {
            const float alpha = static_cast<float>(i) / kSamples;
            float x_max = 1.0f, x = 0.0f, coef = 0.0f;
            while (true)
            {
                x = x_min + (x_max - x_min) / 2.0f;
                coef = 3.0f * x * (1.0f - x);
                const float tx = coef * ((1.0f - x) * p1 + x * p2) + x * x * x;
                if (std::fabs(tx - alpha) < 1e-5f)
                    break;
                if (tx > alpha)
                    x_max = x;
                else
                    x_min = x;
            }
            out[i] = coef * ((1.0f - x) * kStartTension + x) + x * x * x;
        }
        out[kSamples] = 1.0f;
        return out;
    }();
    return table;
}

// Least squares fit of x(t) = b0 + b1 t + b2 t^2 (t relative to the newest sample); returns b1,
// the velocity at the newest sample. Two samples give the straight line through them.
float Derivative(const double *t, const float *x, int n)
{
    if (n < 2)
        return 0.0f;
    if (n == 2)
        return t[0] == t[1] ? 0.0f : static_cast<float>((x[0] - x[1]) / (t[0] - t[1]));
    double s[5] = {}, r[3] = {};
    for (int i = 0; i < n; ++i)
    {
        double power = 1.0;
        for (int k = 0; k < 5; ++k)
        {
            s[k] += power;
            if (k < 3)
                r[k] += power * x[i];
            power *= t[i];
        }
    }
    // Normal equations [s0 s1 s2; s1 s2 s3; s2 s3 s4] b = r, by Cramer's rule.
    const auto det3 = [](double a, double b, double c, double d, double e, double f, double g, double h, double i) {
        return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    };
    const double det = det3(s[0], s[1], s[2], s[1], s[2], s[3], s[2], s[3], s[4]);
    if (std::fabs(det) < 1e-18)
    {
        // Degenerate (samples at the same instants): the line through the oldest and newest.
        return t[0] == t[n - 1] ? 0.0f : static_cast<float>((x[0] - x[n - 1]) / (t[0] - t[n - 1]));
    }
    const double b1 = det3(s[0], r[0], s[2], s[1], r[1], s[3], s[2], r[2], s[4]) / det;
    return static_cast<float>(b1);
}

} // namespace

void VelocityTracker::Clear()
{
    count_ = 0;
    newest_ = -1;
}

void VelocityTracker::Add(double time, Rml::Vector2f position)
{
    if (count_ > 0 && time - samples_[newest_].time > kAssumeStopped)
        Clear();
    newest_ = (newest_ + 1) % kHistory;
    samples_[newest_] = {time, position};
    count_ = std::min(count_ + 1, kHistory);
}

Rml::Vector2f VelocityTracker::Velocity() const
{
    if (count_ == 0)
        return {0.0f, 0.0f};
    const double newest = samples_[newest_].time;
    double t[kHistory];
    float x[kHistory], y[kHistory];
    int n = 0;
    for (int i = 0; i < count_; ++i)
    {
        const Sample &sample = samples_[(newest_ - i + kHistory) % kHistory];
        if (newest - sample.time > kHorizon)
            break;
        t[n] = sample.time - newest;
        x[n] = sample.position.x;
        y[n] = sample.position.y;
        ++n;
    }
    return {Derivative(t, x, n), Derivative(t, y, n)};
}

void Fling::Start(double time, float position, float velocity, float density)
{
    active_ = false;
    origin_ = position;
    start_ = time;
    if (velocity == 0.0f)
        return;
    const double physical = kGravity * 39.37 * (density * 160.0) * 0.84; // OverScroller.mPhysicalCoeff (ppi = 160 dpi per dp)
    const double l = std::log(kInflexion * std::fabs(velocity) / (kScrollFriction * physical));
    duration_ = std::exp(l / (kDecelerationRate - 1.0));
    distance_ = static_cast<float>(kScrollFriction * physical * std::exp(kDecelerationRate / (kDecelerationRate - 1.0) * l)) *
                (velocity < 0.0f ? -1.0f : 1.0f);
    active_ = duration_ > 0.0 && distance_ != 0.0f;
}

float Fling::Advance(double time)
{
    if (!active_)
        return origin_ + distance_;
    const double t = (time - start_) / duration_;
    if (t >= 1.0)
    {
        active_ = false;
        return origin_ + distance_;
    }
    if (t <= 0.0)
        return origin_;
    const auto &spline = SplinePosition();
    const int index = static_cast<int>(kSamples * t);
    const double t_inf = static_cast<double>(index) / kSamples, t_sup = static_cast<double>(index + 1) / kSamples;
    const double coef = (spline[index + 1] - spline[index]) / (t_sup - t_inf);
    return origin_ + static_cast<float>((spline[index] + (t - t_inf) * coef) * distance_);
}

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
    tracker_.Add(time, at);
}

bool TouchScroll::Move(Rml::Vector2f at, double time)
{
    Rml::Element *box = box_.get();
    if (!down_ || !box)
        return false;
    tracker_.Add(time, at);
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

void TouchScroll::Up(Rml::Vector2f at, double time, bool cancelled)
{
    const bool fling = down_ && dragging_ && !cancelled && box_;
    down_ = dragging_ = caught_ = false;
    if (!fling)
        return;
    tracker_.Add(time, at);
    const Rml::Vector2f velocity = tracker_.Velocity();
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
