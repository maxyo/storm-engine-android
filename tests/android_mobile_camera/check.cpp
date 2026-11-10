#include <platform/android_camera.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace storm::android;

// The yaw a drag of `counts` gives when the engine sees it in frames of `perFrame` counts.
static float yaw(int counts, int perFrame)
{
    float total = 0.0f;
    for (int left = counts; left > 0; left -= perFrame)
        total += mobileCameraYawStep(static_cast<float>(left < perFrame ? left : perFrame));
    return total;
}

int main()
{
    // A drag turns the view by all of it, whatever the frame rate: the touch port's old orbit
    // applied only dltTime * rotInertia of each frame's drag (about 1/24 at 60 fps, 1/2 at
    // 10 fps) and threw the rest away, so a running hero's camera hardly followed the finger.
    const float slow = yaw(256, 4), fast = yaw(256, 25), one = yaw(256, 256 > 30 ? 30 : 256);
    assert(std::fabs(slow - 2.56f) < 1e-4f && std::fabs(fast - 2.56f) < 1e-4f && std::fabs(one - 2.56f) < 1e-4f);
    // The same angle per count as the native fight turn (Player::Rotate), both ways, and a
    // single frame never jumps more than the native 0.3 rad.
    assert(std::fabs(mobileCameraYawStep(10.0f) - 0.1f) < 1e-6f && std::fabs(mobileCameraYawStep(-10.0f) + 0.1f) < 1e-6f);
    assert(mobileCameraYawStep(1000.0f) == 0.3f && mobileCameraYawStep(-1000.0f) == -0.3f && mobileCameraYawStep(0.0f) == 0.0f);
    std::puts("PASS mobile camera: the whole drag turns the view at any frame rate, native angle per count, bounded per frame");
}
