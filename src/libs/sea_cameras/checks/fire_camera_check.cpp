// Host check of FIRE_CAMERA: registered under the name the scripts create, and its side sectors
// cover exactly the directions for which the scripts pick that side (bow and stern within
// 30 degrees of the axis, the sides from 30 to 150 degrees).
#include "fire_camera.h"

#include "vma.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace
{
int failures = 0;

void Check(bool condition, const char *what)
{
    std::printf("%s  %s\n", condition ? "ok  " : "FAIL", what);
    failures += !condition;
}

// The side the scripts choose for a look direction (radians, relative to the bow).
char ScriptSide(float angle)
{
    const float pi = 3.14159265358979f;
    if (angle > -pi / 6 && angle < pi / 6)
        return 'f';
    if (angle >= pi / 6 && angle <= pi * 5 / 6)
        return 'r';
    if (angle >= -pi * 5 / 6 && angle <= -pi / 6)
        return 'l';
    return 'b';
}
} // namespace

int main()
{
    const auto started = std::chrono::steady_clock::now();
    bool registered = false;
    for (auto *c : __STORM_CLASSES_REGISTRY)
        registered |= std::string_view(c->GetName()) == "FIRE_CAMERA";
    Check(registered, "class FIRE_CAMERA is registered");

    bool covers = true;
    for (int step = -179; step <= 179; ++step)
    {
        const float angle = step * 3.14159265358979f / 180.0f;
        FIRE_CAMERA::Sector sector{};
        covers &= FIRE_CAMERA::SideSector(ScriptSide(angle), sector);
        float delta = std::remainder(angle - sector.yaw, 2.0f * 3.14159265358979f);
        covers &= std::abs(delta) <= sector.half_width + 1e-4f;
    }
    Check(covers, "each script side's sector contains every direction that selects it");
    FIRE_CAMERA::Sector unused{};
    Check(!FIRE_CAMERA::SideSector('x', unused), "an unknown side is rejected");

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("%s: %d failure(s), %.2f s\n", failures ? "FAIL" : "PASS", failures, seconds);
    return failures ? 1 : 0;
}
