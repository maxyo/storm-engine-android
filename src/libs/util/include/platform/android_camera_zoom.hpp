#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

namespace storm::android
{
// The pinch of the camera look: two fingers on the look surface of the platform host (the
// application's camera gesture) send "camera:zoom\t<scale>", the factor by which the distance
// between the fingers changed since the last event (> 1: spread, the camera comes nearer).
// The camera in charge of the frame applies it to the value the game's own zoom controls
// move (the mouse wheel on PC), within the game's limits:
//   sea, third person  SHIP_CAMERA fDistance 0..1 (ShipCamera_Forward / _Backward)
//   world map          WdmCamera height 50..300 (WMapForward / WMapBackward; none in free mode)
//   spyglass           magnification 1..the glass's own (ChangeTelescopeType)
//   land, follow       LocationCamera radius, kLandZoomMin..kLandZoomMax of the game's radius
// The deck camera of this game has no zoom: a pinch there reaches no camera and is reported.
//
// Game-thread owned. The engine loop calls beginFrame() before it applies the commands of an
// iteration, so a pinch acts only in the frame it arrived for: one that no camera took (no
// frame ran, or no camera with a zoom is in charge) is dropped and reported, never applied
// later as a jump.
class CameraZoom
{
    float scale_ = 1.0f;
    bool pending_ = false, reported_ = false;

  public:
    // The land camera's zoom (pinchLandZoom), kept for the session across locations.
    float land = 1.0f;
    // Returns true for the first dropped pinch since a camera last took one (the caller logs it).
    bool beginFrame()
    {
        const bool dropped = pending_ && !reported_;
        if (pending_)
            reported_ = true;
        scale_ = 1.0f;
        pending_ = false;
        return dropped;
    }
    // The text after "camera:". Anything but "zoom\t<finite factor > 0>" is refused.
    bool offer(std::string_view request)
    {
        constexpr std::string_view kZoom = "zoom\t";
        if (!request.starts_with(kZoom) || request.size() > kZoom.size() + 24)
            return false;
        const std::string number(request.substr(kZoom.size()));
        char *end = nullptr;
        const float scale = std::strtof(number.c_str(), &end);
        if (number.empty() || end != number.c_str() + number.size() || !std::isfinite(scale) || scale <= 0.0f)
            return false;
        scale_ *= scale;
        pending_ = true;
        return true;
    }
    // The camera in charge of this frame takes the pinch (1 when there is none).
    float take()
    {
        const float scale = scale_;
        if (pending_)
            reported_ = false;
        scale_ = 1.0f;
        pending_ = false;
        return scale;
    }
};
inline CameraZoom cameraZoom;

// A camera distance after a pinch: fingers spread `scale` times bring it `scale` times nearer.
inline float pinchDistance(float distance, float scale, float nearest, float farthest)
{
    return std::clamp(distance / scale, nearest, farthest);
}

// A magnification after a pinch (the spyglass): fingers spread `scale` times magnify as much.
inline float pinchMagnification(float magnification, float scale, float lowest, float highest)
{
    return std::clamp(magnification * scale, lowest, highest);
}

// The zoom parameter (0..1) of a camera whose distance runs from `nearDistance` at 0 to
// `farDistance` at 1 (SHIP_CAMERA fDistance), now at `distance`: the parameter that puts the
// camera `scale` times nearer, by the slope of the whole range.
inline float pinchParameter(float parameter, float scale, float distance, float nearDistance, float farDistance)
{
    const float span = farDistance - nearDistance;
    if (!(span > 0.0f))
        return parameter;
    return std::clamp(parameter + (distance / scale - distance) / span, 0.0f, 1.0f);
}

// The land camera has no zoom of its own in this game (its radius is the game's or the
// scripts' MSG_CAMERA_SET_RADIUS value): the pinch scales that radius in the proportions of the
// series' land zoom (Caribbean Legend: default 0.75 of the farthest, nearest 0.5 of it), so the
// game's radius stays the default and the camera comes at most a third nearer or goes a third
// farther.
inline constexpr float kLandZoomMin = 0.5f / 0.75f, kLandZoomMax = 1.0f / 0.75f;
inline float pinchLandZoom(float zoom, float scale)
{
    return pinchDistance(zoom, scale, kLandZoomMin, kLandZoomMax);
}
} // namespace storm::android
