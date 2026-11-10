// Host checks for src/libs/util/include/platform/host_hooks.hpp.
#include "platform/host_hooks.hpp"

#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace std::chrono_literals;
using storm::platform::FrameWorkClock;

static void expect(bool condition, const char *what)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}

int main()
{
    const FrameWorkClock::Clock::time_point t0{};
    {
        FrameWorkClock clock;
        expect(!clock.lap(t0), "first loop top reports nothing");
        clock.frame(10ms);
        const auto work = clock.lap(t0 + 33ms);
        expect(work && *work == 23ms, "frame work excludes the limiter sleep");
        expect(!clock.lap(t0 + 83ms), "an iteration without a frame (background sleep) reports nothing");
        clock.frame(0ms);
        expect(clock.lap(t0 + 100ms) == 17ms, "frame without a limiter sleep reports the whole iteration");
        clock.frame(5ms);
        clock.frame(5ms);
        expect(clock.lap(t0 + 130ms) == 20ms, "several sleeps in one iteration are all excluded");
        clock.frame(50ms);
        expect(clock.lap(t0 + 140ms) == 0ms, "work never goes negative");
    }
    {
        expect(!storm::platform::saveInProgress(), "no save initially");
        {
            const storm::platform::SaveInProgress outer;
            expect(storm::platform::saveInProgress(), "save in progress inside the scope");
            {
                const storm::platform::SaveInProgress nested;
            }
            expect(storm::platform::saveInProgress(), "nested save scope keeps the outer one in progress");
            bool seen = false;
            std::thread([&] { seen = storm::platform::saveInProgress(); }).join();
            expect(seen, "another thread sees the save in progress");
        }
        expect(!storm::platform::saveInProgress(), "no save after the scope");
    }
    storm::platform::reportFrameWork(16ms); // no-op off Android
    std::puts("PASS: host hooks (frame work clock, save-in-progress scope, no-op frame report)");
    return 0;
}
