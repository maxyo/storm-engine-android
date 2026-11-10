#pragma once
#include <algorithm>

namespace storm::android
{
// Mobile camera mode (location CameraFollow::MoveCamera): out of a fight the camera turns
// only from the player's drag, by all of it, every frame, and never re-centres behind the
// hero. One drag count (ChrTurnH, the relative mouse after the sensitivity setting) is the
// same angle as the native turn in a fight (Player::Rotate: 0.01 rad, at most 0.3 per frame),
// so walking, running, standing and fighting all turn the view alike.
inline float mobileCameraYawStep(float turn)
{
    return std::clamp(turn * 0.01f, -0.3f, 0.3f);
}
} // namespace storm::android
