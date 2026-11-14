// Production generation-priority helpers, without renderer, device, model inference or wall-clock sleeps.
// Snapshot windows and admission counts are injected explicitly: elapsed thermal-wait time cannot award a turn.
#include "generation_priority.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace storm::effects;

namespace
{
void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}

bool near(float actual, float expected)
{
    return std::isfinite(actual) && std::abs(actual - expected) <= 0.011f;
}

bool invalid(float value)
{
    return !std::isfinite(value) || value < 0.0f;
}

void check_focus()
{
    GenerationFocus focus;
    check(invalid(focus.distance({8.0f, 0.0f, 0.0f}, 1.0f)), "no player: never substitute camera position");
    focus.set_scene("town");
    focus.set_origin({10000.0f, 0.0f, 0.0f});
    focus.set_player({10005.0f, 0.0f, 0.0f});
    check(near(focus.distance({8.0f, 0.0f, 0.0f}, 1.0f), 2.0f),
          "camera-relative object center converts to absolute coordinates before player distance");
    focus.set_origin({0.0f, 0.0f, 0.0f});
    check(near(focus.distance({10008.0f, 0.0f, 0.0f}, 1.0f), 2.0f),
          "absolute and camera-relative representations give the same player distance");
    focus.set_player({10010.0f, 0.0f, 0.0f});
    check(near(focus.distance({10008.0f, 0.0f, 0.0f}, 1.0f), 1.0f), "player movement updates proximity");
    focus.set_origin({10007.0f, 0.0f, 0.0f});
    check(near(focus.distance({1.0f, 0.0f, 0.0f}, 1.0f), 1.0f), "camera/origin movement alone preserves proximity");
    focus.set_origin({0.0f, 0.0f, 0.0f});
    focus.set_player({0.0f, 0.0f, 0.0f});
    check(near(focus.distance({3.0f, 4.0f, 12.0f}, 2.0f), 11.0f), "three-dimensional sphere distance includes radius");
    check(near(focus.distance({3.0f, 0.0f, 0.0f}, 5.0f), 0.0f), "player inside object bounds is at distance zero");
    check(near(focus.distance({3.0f, 0.0f, 0.0f}, -2.0f), 3.0f), "negative radius clamps to zero");
    focus.clear_player();
    check(invalid(focus.distance({3.0f, 0.0f, 0.0f}, 0.0f)), "clearing player invalidates focus");

    int previous_owner = 0, new_owner = 1;
    focus.set_player({1.0f, 0.0f, 0.0f}, &previous_owner);
    const auto previous_scene = focus.snapshot().scene;
    focus.set_player({2.0f, 0.0f, 0.0f}, &new_owner);
    check(focus.snapshot().scene != previous_scene, "changing controlled entity invalidates earlier observations");
    focus.clear_player(&previous_owner);
    check(near(focus.distance({3.0f, 0.0f, 0.0f}, 0.0f), 1.0f), "old owner teardown cannot clear replacement focus");
    const auto current_scene = focus.snapshot().scene;
    focus.clear_player(&new_owner);
    check(invalid(focus.distance({3.0f, 0.0f, 0.0f}, 0.0f)) && focus.snapshot().scene != current_scene,
          "current owner teardown clears focus and invalidates scene observations");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const float bad : {nan, inf, -inf})
    {
        focus.set_player({bad, 0.0f, 0.0f});
        check(invalid(focus.distance({3.0f, 0.0f, 0.0f}, 0.0f)), "nonfinite player position gives no priority");
        focus.set_player({0.0f, 0.0f, 0.0f});
        for (int axis = 0; axis < 3; ++axis)
        {
            focus.set_player({0.0f, 0.0f, 0.0f});
            std::array<float, 3> center{3.0f, 4.0f, 5.0f};
            center[axis] = bad;
            check(invalid(focus.distance(center, 0.0f)), "nonfinite object center cannot become nearest distance zero");
            std::array<float, 3> origin{};
            origin[axis] = bad;
            focus.set_origin(origin);
            check(invalid(focus.distance({3.0f, 0.0f, 0.0f}, 0.0f)), "nonfinite origin gives no spatial priority");
            focus.set_origin({0.0f, 0.0f, 0.0f});
        }
        focus.set_player({0.0f, 0.0f, 0.0f});
        check(invalid(focus.distance({3.0f, 0.0f, 0.0f}, bad)), "nonfinite object radius gives no spatial priority");
    }

    focus.set_player({0.0f, 0.0f, 0.0f});
    const GenerationSnapshot first = focus.snapshot();
    check(first.window <= kGenerationWindowMask, "snapshot clock is within packed timestamp range");
    focus.set_scene("town");
    check(focus.snapshot().scene == first.scene && near(focus.distance({3.0f, 0.0f, 0.0f}, 0.0f), 3.0f),
          "same scene preserves player focus and scene identity");
    GenerationPriority priority;
    priority.observe(3.0f, first);
    focus.set_scene("island");
    const GenerationSnapshot changed = focus.snapshot();
    check(changed.scene != first.scene && invalid(focus.distance({3.0f, 0.0f, 0.0f}, 0.0f)),
          "scene change immediately invalidates player focus");
    check(std::isinf(priority.distance(changed)), "scene change invalidates observations from previous scene");
    focus.set_scene("");
    const auto empty = focus.snapshot();
    focus.set_scene("");
    check(focus.snapshot().scene != empty.scene, "empty scene notifications invalidate even without a name change");
}

void check_observations()
{
    const GenerationSnapshot now{7, 100};
    GenerationPriority empty;
    check(std::isinf(empty.distance(now)), "unobserved texture has no spatial priority");
    for (const float bad : {-1.0f, std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
        empty.observe(bad, now);
    check(std::isinf(empty.distance(now)), "invalid observations do not create a priority sample");

    // All draw orders for three instances of one shared texture must select the nearest instance.
    const std::array<std::array<float, 3>, 6> orders{{{30.0f, 2.25f, 14.0f}, {30.0f, 14.0f, 2.25f},
                                                   {2.25f, 30.0f, 14.0f}, {2.25f, 14.0f, 30.0f},
                                                   {14.0f, 30.0f, 2.25f}, {14.0f, 2.25f, 30.0f}}};
    for (const auto &distances : orders)
    {
        GenerationPriority shared;
        for (float distance : distances)
            shared.observe(distance, now);
        check(near(shared.distance(now), 2.25f), "shared instance priority is independent of draw order");
    }

    GenerationPriority moving;
    moving.observe(2.25f, now);
    moving.observe(std::numeric_limits<float>::quiet_NaN(), now);
    check(near(moving.distance(now), 2.25f), "invalid observation cannot overwrite an existing valid sample");
    moving.observe(50.0f, {7, 101});
    check(near(moving.distance({7, 101}), 50.0f), "next 500 ms window discards old near instance after movement");
    moving.observe(4.5f, {7, 101});
    check(near(moving.distance({7, 101}), 4.5f), "new window still merges nearest shared instance");
    check(near(moving.distance({7, 104}), 4.5f), "observation remains live before two-second stale boundary");
    check(std::isinf(moving.distance({7, 105})), "four 500 ms windows expire an unrenewed observation at two seconds");
    check(std::isinf(moving.distance({8, 101})), "another scene cannot inherit spatial priority");
    moving.observe(40.0f, {8, 101});
    check(near(moving.distance({8, 101}), 40.0f), "new scene observation does not retain old scene's nearer distance");
    moving.observe(std::numeric_limits<float>::max(), {8, 102});
    check(std::isfinite(moving.distance({8, 102})) && moving.distance({8, 102}) > 1000.0f,
          "huge finite distance saturates without integer overflow or becoming near");

    GenerationPriority wrapped;
    wrapped.observe(6.0f, {9, kGenerationWindowMask - 1});
    check(near(wrapped.distance({9, 0}), 6.0f), "packed timestamp wrap preserves live observation age");
    check(std::isinf(wrapped.distance({9, 2})), "stale boundary also applies across packed timestamp wrap");
    wrapped.observe(24.0f, {9, 0});
    check(near(wrapped.distance({9, 0}), 24.0f), "post-wrap observation starts a new nearest-instance window");
    check(std::isinf(wrapped.distance({9, kGenerationWindowMask})), "future sample cannot appear fresh across wrap");
    GenerationPriority sentinel;
    sentinel.observe(std::numeric_limits<float>::max(), {UINT16_MAX, kGenerationWindowMask});
    check(std::isfinite(sentinel.distance({UINT16_MAX, kGenerationWindowMask})),
          "maximum scene and timestamp cannot collide with unobserved sentinel");
}

void check_ranking()
{
    const GenerationSnapshot now{11, 200};
    GenerationPriority near_texture, same_band, far_texture, boundary;
    near_texture.observe(2.0f, now);
    same_band.observe(9.99f, now);
    boundary.observe(10.0f, now);
    far_texture.observe(30.0f, now);
    check(generation_rank(near_texture, now, 0).first == 0 && generation_rank(same_band, now, 0).first == 0,
          "distances below 10 m share nearest priority tier");
    check(generation_rank(boundary, now, 0).first == 1 && generation_rank(far_texture, now, 0).first == 3,
          "10 m boundaries produce distinct near/far tiers");
    check(generation_rank(near_texture, now, 0) < generation_rank(same_band, now, 0) &&
              generation_rank(same_band, now, 0) < generation_rank(boundary, now, 0),
          "distance orders jobs within and between bands before ageing");
    check(generation_rank(far_texture, now, 15).first == 3, "15 admitted jobs do not yet award fairness turn");
    check(generation_rank(far_texture, now, 16) < generation_rank(near_texture, now, 0),
          "16 admitted jobs award still-live distant work a fairness turn");
    check(generation_rank(far_texture, now, 17) == generation_rank(far_texture, now, 16),
          "later age remains eligible for caller's oldest-first tie break");
    // A paused gate admits no jobs. Advancing the clock while keeping the admission count fixed cannot promote it.
    check(generation_rank(far_texture, {11, 203}, 15) == generation_rank(far_texture, now, 15),
          "elapsed thermal-wait time alone never ages a request into fairness");
    check(generation_rank(far_texture, {11, 204}, 16).first == UINT32_MAX,
          "stale work cannot receive a live-age fairness turn");
    check(generation_rank(far_texture, {12, 200}, UINT64_MAX).first == UINT32_MAX,
          "even maximum admission age cannot promote work from another scene");
    GenerationPriority unseen;
    check(generation_rank(unseen, now, 16).first == UINT32_MAX, "unseen work keeps load-order priority after 16 admissions");
    far_texture.observe(30.0f, {11, 204});
    check(generation_rank(far_texture, {11, 204}, 16).first == 0,
          "renewed live observation can receive its earned admission-age turn");
}
} // namespace

int main()
{
    check_focus();
    check_observations();
    check_ranking();
    std::puts("generation_priority: player origin, movement, shared draws, expiry, wrap, tiers and admission fairness ok");
}
