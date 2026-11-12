#pragma once
#include "touch_physics.hpp"

#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace storm::android
{
// The world map's camera turned by a finger. On the world map the look finger of the platform
// host (the application's CameraGesture) sends every sample of its drag with its MotionEvent
// time instead of the PC's mouse with the camera-rotate key held:
//
//   camera:turn\tdown\t<x px>\t<time ms>\t<surface width px>\t<density>
//   camera:turn\tmove\t<x px>\t<time ms>
//   camera:turn\tup\t<time ms>          the finger lifted: the turn coasts on (a fling)
//   camera:turn\tcancel                 the gesture was taken away: it stops where it is
//
// The camera keeps the game's model (it turns about the point it looks at, and its height is the
// pinch's): the finger only sets the turn. While it is down the camera has turned by the
// finger's travel since it went down (1:1, see WdmCamera: the near ground follows the finger);
// on release the travel goes on with Android's fling (touch_physics.hpp: VelocityTracker,
// OverScroller), evaluated at the frame's time, so the angle at any moment is the same at any
// frame rate and the release continues at the finger's speed. Times are milliseconds of
// CLOCK_MONOTONIC (SystemClock.uptimeMillis), the frame's included.
//
// Game-thread owned: the engine loop offers the commands, the world map's camera takes the
// travel of each frame.
class CameraTurn
{
    bool down_ = false;
    float width_ = 1.0f, density_ = 1.0f;
    float start_ = 0.0f;                 // px: where the finger went down
    float finger_ = 0.0f, taken_ = 0.0f; // px: the finger's travel since down, and what the camera has turned by
    touch::VelocityTracker tracker_;
    touch::Fling fling_;

    static bool number(std::string_view text, double &out)
    {
        if (text.empty() || text.size() > 24)
            return false;
        const std::string copy(text);
        char *end = nullptr;
        out = std::strtod(copy.c_str(), &end);
        return end == copy.c_str() + copy.size() && std::isfinite(out);
    }
    static std::vector<std::string_view> fields(std::string_view text)
    {
        std::vector<std::string_view> out;
        for (size_t at = 0;;)
        {
            const size_t tab = text.find('\t', at);
            out.push_back(text.substr(at, tab == std::string_view::npos ? std::string_view::npos : tab - at));
            if (tab == std::string_view::npos)
                return out;
            at = tab + 1;
        }
    }

  public:
    // The text after "camera:". Returns false (nothing changes) for anything malformed and for
    // a move or an up without a down.
    bool offer(std::string_view request)
    {
        const std::vector<std::string_view> f = fields(request);
        if (f.size() < 2 || f[0] != "turn")
            return false;
        double x = 0.0, time = 0.0, width = 0.0, density = 0.0;
        if (f[1] == "down" && f.size() == 6 && number(f[2], x) && number(f[3], time) && number(f[4], width) && number(f[5], density) &&
            width > 0.0 && density > 0.0)
        {
            // A finger on a coasting turn stops it there: what it travelled stays turned.
            if (fling_.Active())
                fling_.Stop();
            down_ = true;
            width_ = static_cast<float>(width);
            density_ = static_cast<float>(density);
            start_ = static_cast<float>(x);
            finger_ = taken_ = 0.0f;
            tracker_.Clear();
            tracker_.Add(time / 1000.0, {static_cast<float>(x), 0.0f});
            return true;
        }
        if (f[1] == "move" && f.size() == 4 && down_ && number(f[2], x) && number(f[3], time))
        {
            finger_ = static_cast<float>(x) - start_;
            tracker_.Add(time / 1000.0, {static_cast<float>(x), 0.0f});
            return true;
        }
        if (f[1] == "up" && f.size() == 3 && down_ && number(f[2], time))
        {
            down_ = false;
            tracker_.Lift(time / 1000.0);
            const float limit = touch::kMaxFlingDp * density_;
            const float velocity = std::fmax(-limit, std::fmin(limit, tracker_.Velocity().x));
            if (std::fabs(velocity) >= touch::kMinFlingDp * density_)
                fling_.Start(time / 1000.0, finger_, velocity, density_);
            return true;
        }
        if (f[1] == "cancel" && f.size() == 2)
        {
            down_ = false;
            fling_.Stop();
            finger_ = taken_;
            return true;
        }
        return false;
    }

    // The camera's turn for the frame at `now_ms`, as a fraction of the surface's width (the
    // finger's travel since the last take); 0 when no finger turns it and nothing coasts.
    float take(double now_ms)
    {
        if (fling_.Active())
            finger_ = fling_.Advance(now_ms / 1000.0);
        const float step = finger_ - taken_;
        taken_ = finger_;
        return step / width_;
    }

    bool active() const
    {
        return down_ || fling_.Active();
    }
};
inline CameraTurn cameraTurn;

// The camera's turn (radians) for a horizontal finger travel of `fraction` of the screen's
// width, such that the ground at the bottom edge of the view, under the middle of the screen,
// follows the finger: it lies between the camera and the point the camera turns about, at the
// ground distance `rho` from that point and the depth `depth` along the line of sight, and a
// turn by a moves it sideways on the screen by rho * a at that depth. The camera looks down by
// `pitch` (radians below the horizon); `projectionW` / `projectionH` are the projection's
// 1 / tan(half field of view), horizontal and vertical. Every distance scales with the camera's
// height, so the angle per finger travel does not depend on it: the near ground follows the
// finger 1:1 at every height. Positive: the finger moved right, the near ground goes right.
inline float turnAngle(float fraction, float pitch, float projectionW, float projectionH)
{
    const float halfVertical = std::atan(1.0f / projectionH);
    const float bottom = std::fmin(pitch + halfVertical, 1.48f); // the bottom ray, kept short of straight down
    const float rho = 1.0f / std::tan(pitch) - 1.0f / std::tan(bottom); // per unit of height
    const float depth = std::cos(halfVertical) / std::sin(bottom);
    if (!(rho > 1e-3f))
        return 0.0f;
    const float sideways = fraction * 2.0f * depth / projectionW; // the finger's travel on the ground at that depth
    return sideways / rho;
}
} // namespace storm::android
