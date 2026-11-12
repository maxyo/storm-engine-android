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
        using storm::platform::presentWaited;
        FrameWorkClock clock;
        expect(!clock.lap(t0), "first loop top reports nothing");
        clock.frame();
        presentWaited(10ms);
        const auto work = clock.lap(t0 + 33ms);
        expect(work && *work == 23ms, "frame work excludes the present wait");
        presentWaited(7ms); // a present outside a frame iteration is not carried into the next one
        expect(!clock.lap(t0 + 83ms), "an iteration without a frame (background sleep) reports nothing");
        clock.frame();
        expect(clock.lap(t0 + 100ms) == 17ms, "frame without a present wait reports the whole iteration");
        clock.frame();
        presentWaited(5ms);
        presentWaited(5ms);
        expect(clock.lap(t0 + 130ms) == 20ms, "several presents in one iteration are all excluded");
        clock.frame();
        presentWaited(50ms);
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
    storm::platform::reportFrameWork(16ms); // no-ops off Android
    storm::platform::frameWorkTarget(16ms);
    std::puts("PASS: host hooks (frame work clock, save-in-progress scope, no-op frame work hint)");
    return 0;
}
