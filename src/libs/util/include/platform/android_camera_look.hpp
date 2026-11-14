#pragma once
#include "touch_physics.hpp"

#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace storm::android
{
// The world map's camera moved by a finger. On the world map the look finger of the platform
// host (the application's CameraGesture) sends every sample of its drag with its MotionEvent
// time instead of the PC's mouse:
//
//   camera:look\tdown\t<x px>\t<y px>\t<time ms>\t<surface width px>\t<surface height px>\t<density>
//   camera:look\tmove\t<x px>\t<y px>\t<time ms>
//   camera:look\tup\t<time ms>          the finger lifted: the drag coasts on (a fling)
//   camera:look\tcancel                 the gesture was taken away: it stops where it is
//
// The camera decides what the finger does (WdmCamera): the camera that follows the ship turns
// about the point it looks at by the finger's horizontal travel (turnAngle); the free camera
// (the map seen from above, WMapCameraSwitch) is panned so that the ground under the finger
// stays under it (panShift). Either way the travel is 1:1 while the finger is down, and on
// release it goes on with Android's fling (touch_physics.hpp: VelocityTracker, OverScroller)
// along the release's direction, evaluated at the frame's time: the position at any moment is
// the same at any frame rate and the release continues at the finger's speed. Times are
// milliseconds of CLOCK_MONOTONIC (SystemClock.uptimeMillis), the frame's included.
//
// Game-thread owned: the engine loop offers the commands, the world map's camera takes the
// travel of each frame.
struct LookStep
{
    float x = 0.0f, y = 0.0f;   // px: where the finger was at the last take
    float dx = 0.0f, dy = 0.0f; // px: its travel since (0 when no finger moves the camera and nothing coasts)
    float width = 1.0f, height = 1.0f; // px: the surface the finger is on

    bool moved() const
    {
        return dx != 0.0f || dy != 0.0f;
    }
};

class CameraLook
{
    bool down_ = false;
    float width_ = 1.0f, height_ = 1.0f, density_ = 1.0f;
    touch::Point finger_, taken_; // px: where the finger is (or the coast has brought it), and where the camera has followed it
    touch::Point coastFrom_, coastDirection_; // a fling: from where, along which unit direction
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
        if (f.size() < 2 || f[0] != "look")
            return false;
        double x = 0.0, y = 0.0, time = 0.0, width = 0.0, height = 0.0, density = 0.0;
        if (f[1] == "down" && f.size() == 8 && number(f[2], x) && number(f[3], y) && number(f[4], time) && number(f[5], width) &&
            number(f[6], height) && number(f[7], density) && width > 0.0 && height > 0.0 && density > 0.0)
        {
            // A finger on a coasting drag stops it there: what it travelled stays travelled.
            fling_.Stop();
            down_ = true;
            width_ = static_cast<float>(width);
            height_ = static_cast<float>(height);
            density_ = static_cast<float>(density);
            finger_ = taken_ = {static_cast<float>(x), static_cast<float>(y)};
            tracker_.Clear();
            tracker_.Add(time / 1000.0, finger_);
            return true;
        }
        if (f[1] == "move" && f.size() == 5 && down_ && number(f[2], x) && number(f[3], y) && number(f[4], time))
        {
            finger_ = {static_cast<float>(x), static_cast<float>(y)};
            tracker_.Add(time / 1000.0, finger_);
            return true;
        }
        if (f[1] == "up" && f.size() == 3 && down_ && number(f[2], time))
        {
            down_ = false;
            tracker_.Lift(time / 1000.0);
            const touch::Point velocity = tracker_.Velocity();
            const float speed = std::hypot(velocity.x, velocity.y);
            if (speed >= touch::kMinFlingDp * density_)
            {
                coastFrom_ = finger_;
                coastDirection_ = {velocity.x / speed, velocity.y / speed};
                fling_.Start(time / 1000.0, 0.0f, std::fmin(speed, touch::kMaxFlingDp * density_), density_);
            }
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

    // The finger's travel for the frame at `now_ms`, since the last take.
    LookStep take(double now_ms)
    {
        if (fling_.Active())
        {
            const float travelled = fling_.Advance(now_ms / 1000.0);
            finger_ = {coastFrom_.x + coastDirection_.x * travelled, coastFrom_.y + coastDirection_.y * travelled};
        }
        const LookStep step{taken_.x, taken_.y, finger_.x - taken_.x, finger_.y - taken_.y, width_, height_};
        taken_ = finger_;
        return step;
    }

    bool active() const
    {
        return down_ || fling_.Active();
    }
};
inline CameraLook cameraLook;

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

// A camera over the ground plane y = 0: its eye `height` above it, the game's Euler angles
// (storm::math::eulerMatrix: `pitch` down from the horizon, then `yaw` about the vertical) and
// the projection's _11 / _22 (1 / tan(half field of view), horizontal and vertical).
struct GroundCamera
{
    float height = 0.0f, pitch = 0.0f, yaw = 0.0f, projectionW = 1.0f, projectionH = 1.0f;
};

// The ground point under the screen point (x, y) px of a `width` x `height` surface, relative
// to the eye (world x, z). False when that point's ray does not reach the ground.
inline bool groundUnder(const GroundCamera &camera, float x, float y, float width, float height, float &groundX, float &groundZ)
{
    const float right = (2.0f * x / width - 1.0f) / camera.projectionW, up = (1.0f - 2.0f * y / height) / camera.projectionH;
    const float sp = std::sin(camera.pitch), cp = std::cos(camera.pitch), sy = std::sin(camera.yaw), cy = std::cos(camera.yaw);
    // The ray: forward (sy cp, -sp, cy cp) + right * (cy, 0, -sy) + up * (sy sp, cp, cy sp).
    const float rayX = sy * cp + cy * right + sy * sp * up, rayY = -sp + cp * up, rayZ = cy * cp - sy * right + cy * sp * up;
    if (!(rayY < -1e-3f) || !(camera.height > 0.0f))
        return false;
    const float distance = camera.height / -rayY;
    groundX = rayX * distance;
    groundZ = rayZ * distance;
    return true;
}

// The shift of the camera along the ground (world x, z) that brings the ground under the
// finger where it was at the last take under the finger where it is now: a pan 1:1 with the
// finger at any height. False (no shift) when either point is above the horizon.
inline bool panShift(const GroundCamera &camera, const LookStep &step, float &shiftX, float &shiftZ)
{
    float fromX, fromZ, toX, toZ;
    if (!groundUnder(camera, step.x, step.y, step.width, step.height, fromX, fromZ) ||
        !groundUnder(camera, step.x + step.dx, step.y + step.dy, step.width, step.height, toX, toZ))
        return false;
    shiftX = fromX - toX;
    shiftZ = fromZ - toZ;
    return true;
}
} // namespace storm::android
