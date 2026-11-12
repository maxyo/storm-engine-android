// Finger motion as Android's own views have it: velocity from the timestamped samples of a
// pointer, and the fling that follows a release. The portable UI (lists, the map's pan:
// src/libs/ui touch_motion.h) and the game cameras driven by touch (the world map's turn,
// platform/android_camera_turn.hpp) share it, so every drag follows the finger and coasts alike.
//
// The model is the published one of android.view.VelocityTracker ("lsq2") and
// android.widget.OverScroller (SplineOverScroller): no library carries it for C++. Its
// constants are Android's ViewConfiguration defaults. Header-only, standard library only.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace storm::touch
{

struct Point
{
    float x = 0.0f, y = 0.0f;
    float operator[](int axis) const
    {
        return axis == 0 ? x : y;
    }
};

// Android's ViewConfiguration, in dp (density = physical pixels per dp).
constexpr float kMinFlingDp = 50.0f;   // a slower release does not fling (getScaledMinimumFlingVelocity)
constexpr float kMaxFlingDp = 8000.0f; // per second (getScaledMaximumFlingVelocity)

// The velocity of one pointer at its newest sample, in pixels per second: a least-squares
// fit of a second-degree polynomial per axis over the samples of the last 100 ms
// (VelocityTracker's default "lsq2" strategy). A pause of more than 40 ms between samples
// starts the history anew: the finger had stopped.
class VelocityTracker
{
  public:
    void Clear();
    // A sample more than 40 ms after the one before clears the history first (the finger
    // had stopped): a release after a pause has no velocity.
    void Add(double time, Point position);
    // The pointer went up at `time`. Its position is not a sample (it repeats the last move;
    // an injected or late up may even step back), but an up 40 ms or more after the last
    // sample means the finger had stopped: no velocity (VelocityTracker, ACTION_UP).
    void Lift(double time);
    Point Velocity() const;

  private:
    struct Sample
    {
        double time = 0.0;
        Point position;
    };
    static constexpr int kHistory = 20;
    std::array<Sample, kHistory> samples_{};
    int count_ = 0, newest_ = -1;
};

// The travel of a fling along one axis: OverScroller's spline (SplineOverScroller.fling),
// evaluated at absolute times, so the position at any frame time is exact whatever the frame
// rate (no per-frame integration). Distance and duration grow with the release speed; the
// curve starts at the release speed and ends at rest.
class Fling
{
  public:
    // `velocity` in pixels per second, `density` in pixels per dp.
    void Start(double time, float position, float velocity, float density);
    void Stop()
    {
        active_ = false;
    }
    bool Active() const
    {
        return active_;
    }
    // The position at `time`; the fling ends (Active false) once it is at rest.
    float Advance(double time);

  private:
    bool active_ = false;
    double start_ = 0.0;
    double duration_ = 0.0; // seconds
    float origin_ = 0.0f, distance_ = 0.0f; // signed
};

namespace detail
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
inline const double kDecelerationRate = std::log(0.78) / std::log(0.9);

// SPLINE_POSITION: the distance travelled (0..1) at each hundredth of the duration.
inline const std::array<float, kSamples + 1> &SplinePosition()
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
inline float Derivative(const double *t, const float *x, int n)
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

} // namespace detail

inline void VelocityTracker::Clear()
{
    count_ = 0;
    newest_ = -1;
}

inline void VelocityTracker::Add(double time, Point position)
{
    if (count_ > 0 && time - samples_[newest_].time > detail::kAssumeStopped)
        Clear();
    newest_ = (newest_ + 1) % kHistory;
    samples_[newest_] = {time, position};
    count_ = std::min(count_ + 1, kHistory);
}

inline void VelocityTracker::Lift(double time)
{
    if (count_ > 0 && time - samples_[newest_].time >= detail::kAssumeStopped)
        Clear();
}

inline Point VelocityTracker::Velocity() const
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
        if (newest - sample.time > detail::kHorizon)
            break;
        t[n] = sample.time - newest;
        x[n] = sample.position.x;
        y[n] = sample.position.y;
        ++n;
    }
    return {detail::Derivative(t, x, n), detail::Derivative(t, y, n)};
}

inline void Fling::Start(double time, float position, float velocity, float density)
{
    active_ = false;
    origin_ = position;
    start_ = time;
    if (velocity == 0.0f)
        return;
    const double physical = detail::kGravity * 39.37 * (density * 160.0) * 0.84; // OverScroller.mPhysicalCoeff (ppi = 160 dpi per dp)
    const double l = std::log(detail::kInflexion * std::fabs(velocity) / (detail::kScrollFriction * physical));
    duration_ = std::exp(l / (detail::kDecelerationRate - 1.0));
    distance_ = static_cast<float>(detail::kScrollFriction * physical * std::exp(detail::kDecelerationRate / (detail::kDecelerationRate - 1.0) * l)) *
                (velocity < 0.0f ? -1.0f : 1.0f);
    active_ = duration_ > 0.0 && distance_ != 0.0f;
}

inline float Fling::Advance(double time)
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
    const auto &spline = detail::SplinePosition();
    const int index = static_cast<int>(detail::kSamples * t);
    const double t_inf = static_cast<double>(index) / detail::kSamples, t_sup = static_cast<double>(index + 1) / detail::kSamples;
    const double coef = (spline[index + 1] - spline[index]) / (t_sup - t_inf);
    return origin_ + static_cast<float>((spline[index] + (t - t_inf) * coef) * distance_);
}

} // namespace storm::touch
