// Host check of the snow entity: registered under the name the scripts use, and the flake
// motion keeps every flake inside the visibility box around a moving eye while it falls.
#include "snow.hpp"

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

bool Inside(const std::vector<SNOW::Flake> &flakes, const SNOW::Settings &s, const glm::vec3 &eye)
{
    for (const auto &f : flakes)
        if (std::abs(f.pos.x - eye.x) > s.radius + 1e-3f || std::abs(f.pos.z - eye.z) > s.radius + 1e-3f ||
            std::abs(f.pos.y - eye.y) > 0.5f * s.height + 1e-3f)
            return false;
    return true;
}
} // namespace

int main()
{
    const auto started = std::chrono::steady_clock::now();
    bool registered = false;
    for (auto *c : __STORM_CLASSES_REGISTRY)
        registered |= std::string_view(c->GetName()) == "SNOW";
    Check(registered, "class SNOW is registered (scripts create \"snow\"; names are case-insensitive)");

    SNOW::Settings settings; // dust preset of the scripts
    settings.radius = 5.5f;
    settings.height = 3.5f;
    std::vector<SNOW::Flake> flakes = {{{0, 0, 0}, 0.2f, 0.0f}, {{5, 1.5f, -5}, 0.35f, 1.0f}, {{-5, -1.7f, 5}, 0.15f, 2.0f}};
    const float y0 = flakes[0].pos.y;
    SNOW::Advance(flakes, settings, {0, 0, 0}, 0.5f, 0.0f);
    Check(flakes[0].pos.y < y0, "flakes fall");
    Check(Inside(flakes, settings, {0, 0, 0}), "flakes stay in the box");
    glm::vec3 eye(0, 0, 0);
    for (int i = 0; i < 200; ++i)
    {
        eye += glm::vec3(0.37f, 0.05f, -0.21f); // walking and climbing
        SNOW::Advance(flakes, settings, eye, 0.016f, i * 0.016f);
    }
    Check(Inside(flakes, settings, eye), "flakes follow a moving eye (wrap-around)");

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("%s: %d failure(s), %.2f s\n", failures ? "FAIL" : "PASS", failures, seconds);
    return failures ? 1 : 0;
}
