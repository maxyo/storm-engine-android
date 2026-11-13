#include "deck_walk.hpp"
#include "platform/android_deck_walk.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace storm::android;

int main()
{
    DeckWalk walk;
    bool allowed = true;
    walk.guard = [&] { return allowed; };
    walk.update(7);
    const auto epoch = [] { return combatCancelEpoch.load(); };
    const auto near = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };

    // nothing until the stick moves; a walk forward and to the side, as far as it is pushed
    assert(!walk.read());
    assert(walk.offer("7\t40\t0", epoch()));
    auto step = walk.read();
    assert(step && near(step->x, 0.4f) && near(step->y, 0.0f));
    assert(walk.offer("7\t-38\t38", epoch()));
    step = walk.read();
    assert(step && near(step->x, -0.38f) && near(step->y, 0.38f));
    // held through any number of frames, until its stop
    for (int frame = 0; frame < 1000; ++frame) assert(walk.read());
    assert(walk.offer("7\t0\t0", epoch()) && !walk.read());

    // a diagonal rounded a little past 100 stays inside the unit circle
    assert(walk.offer("7\t71\t71", epoch()));
    step = walk.read();
    assert(step && std::hypot(step->x, step->y) <= 1.0f + 1e-5f);

    // a new walk is taken in the current revision only; the stop counts in any
    walk.update(8);
    assert(walk.read()); // a taken walk carries on
    assert(walk.offer("7\t0\t0", epoch()) && !walk.read());
    assert(!walk.offer("7\t0\t100", epoch()) && walk.offer("8\t0\t100", epoch()));

    // a failing guard and a cancelled input stop it
    allowed = false;
    assert(!walk.read());
    assert(!walk.offer("8\t0\t100", epoch()));
    allowed = true;
    assert(walk.offer("8\t0\t-100", epoch()) && walk.read());
    const auto before = epoch();
    cancelAndroidCombatInput();
    assert(!walk.read());
    assert(!walk.offer("8\t0\t100", before)); // sent before the cancellation
    assert(walk.offer("8\t0\t100", epoch()));

    for (const auto *bad : {"", "8", "8\t1", "x\t1\t1", "8\t1\t1\t1", "8\t101\t0", "8\t0\t-101", "8\t80\t80", "8\t1.5\t0", "8\t\t1",
                            "8\t99999999999\t0"})
        assert(!walk.offer(bad, epoch()));
    DeckWalk unguarded;
    unguarded.update(1);
    assert(!unguarded.offer("1\t0\t50", epoch()) && !unguarded.read());
    // the walker's pace is real time: x1…x8 of the game's time scale walk the same distance a
    // frame; frozen time stands still
    for (const uint32_t scale : {1u, 2u, 4u, 8u})
        assert(deckWalkTime(16u * scale, 16u) == 16u);
    assert(deckWalkTime(0u, 16u) == 0u);

    // the way: ahead along the view, sideways to its right, the length as pushed
    const float quarter = 1.57079633f;
    auto way = deckWalkShift(0.0f, 1.0f, 0.0f, 0.0f);
    assert(near(way.x, 0.0f) && near(way.z, 1.0f)); // looking along +z, ahead is +z
    way = deckWalkShift(1.0f, 0.0f, 0.0f, 0.0f);
    assert(near(way.x, 1.0f) && near(way.z, 0.0f)); // the right is +x
    way = deckWalkShift(1.0f, 0.0f, 0.0f, quarter);
    assert(near(way.x, 0.0f) && near(way.z, -1.0f)); // turned right (+x ahead), the right is -z
    way = deckWalkShift(0.0f, 1.0f, 0.5f, 0.0f);
    assert(near(way.z, std::cos(0.5f))); // ahead slower while looking down, as the original walk
    way = deckWalkShift(0.0f, -0.4f, 0.0f, 0.0f);
    assert(near(way.z, -0.4f)); // back, as far as pushed
    way = deckWalkShift(-0.6f, 0.8f, 0.0f, 0.7f);
    assert(near(std::hypot(way.x, way.z), 1.0f)); // a full push in any direction is a full step

    std::cout << "deck walk: real-time pace under time acceleration, the way ahead and sideways, analog vector, held until stopped, revision, guard, cancellation, bounds, malformed requests PASS\n";
}
