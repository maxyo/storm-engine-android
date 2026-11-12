#include "platform/android_camera_turn.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
using storm::android::CameraTurn;

std::string down(float x, double t)
{
    return "turn\tdown\t" + std::to_string(x) + "\t" + std::to_string(t) + "\t1280\t2";
}
std::string move(float x, double t)
{
    return "turn\tmove\t" + std::to_string(x) + "\t" + std::to_string(t);
}
std::string up(double t)
{
    return "turn\tup\t" + std::to_string(t);
}

struct Run
{
    std::vector<double> times;  // frame times, ms
    std::vector<float> turned;  // the camera's total turn after each frame (fractions of the width)
    float released = 0.0f;      // the turn at the last frame before the release
    size_t release_frame = 0;   // index of the first frame after the release
};

// A finger from x 200 to the right at `speed` px/s, a sample every 1000/120 ms for `samples`
// samples, released `pause` ms after the last; frames every `frame` ms until 2 s after.
Run Drag(float speed, int samples, double pause, double frame)
{
    CameraTurn turn;
    Run run;
    const double start = 10000.0, step = 1000.0 / 120.0;
    std::vector<std::string> events{down(200.0f, start)};
    std::vector<double> at{start};
    for (int i = 1; i <= samples; ++i)
    {
        events.push_back(move(200.0f + speed * static_cast<float>(i * step / 1000.0), start + i * step));
        at.push_back(start + i * step);
    }
    const double release = start + samples * step + pause;
    events.push_back(up(release));
    at.push_back(release);
    size_t next = 0;
    float total = 0.0f;
    // Frames 1 ms off the sample grid: which samples a frame has seen is the same at both rates.
    for (int k = 1; start + 1.0 + k * frame < release + 2000.0; ++k)
    {
        const double now = start + 1.0 + k * frame;
        while (next < events.size() && at[next] <= now) // the events of this frame, then the frame
            assert(turn.offer(events[next++]));
        total += turn.take(now);
        run.times.push_back(now);
        run.turned.push_back(total);
        if (now < release)
            run.released = total, run.release_frame = run.turned.size();
    }
    assert(!turn.active());
    return run;
}

float At(const Run &run, double time)
{
    for (size_t i = 0; i < run.times.size(); ++i)
        if (std::fabs(run.times[i] - time) < 1e-3)
            return run.turned[i];
    assert(false && "no frame at that time");
    return 0.0f;
}
} // namespace

int main()
{
    using namespace storm::android;
    const float speed = 1600.0f; // px/s at density 2: a quick flick
    const Run fast = Drag(speed, 12, 0.0, 1000.0 / 60.0), slow = Drag(speed, 12, 0.0, 1000.0 / 30.0);
    // While down the camera has turned by exactly the finger's travel so far.
    assert(std::fabs(fast.turned[0] - 2.0f * speed / 120.0f / 1280.0f) < 1e-4f); // two samples by the first frame
    // The same gesture at 30 and 60 fps: the same turn at every frame they share, and at rest.
    for (size_t i = 0; i < slow.times.size(); ++i)
        assert(std::fabs(slow.turned[i] - At(fast, slow.times[i])) < 2e-4f);
    assert(std::fabs(slow.turned.back() - fast.turned.back()) < 1e-4f);
    // The release coasts on at the finger's speed: the first frames after it move about as
    // much as the last frames before it (no jump, no stop), then slow down every frame to rest.
    const float before = fast.turned[fast.release_frame - 1] - fast.turned[fast.release_frame - 2];
    const float after = fast.turned[fast.release_frame] - fast.turned[fast.release_frame - 1];
    std::printf("last frame before the release %.5f, first after %.5f (of the width), coasted %.4f\n", before, after,
                fast.turned.back() - fast.released);
    assert(after > 0.8f * before && after < 1.2f * before);
    assert(fast.turned.back() - fast.released > 0.2f); // it coasted (the drag itself was 0.125)
    for (size_t i = fast.release_frame + 1; i < fast.turned.size(); ++i)
        assert(fast.turned[i] - fast.turned[i - 1] <= fast.turned[i - 1] - fast.turned[i - 2] + 1e-5f);
    // A finger that rested 60 ms before lifting does not coast.
    const Run rested = Drag(speed, 12, 60.0, 1000.0 / 60.0);
    assert(std::fabs(rested.turned.back() - rested.released) < 1e-6f);

    // A finger on a coasting turn stops it where it is; a cancel stops a drag at once.
    {
        CameraTurn turn;
        assert(turn.offer(down(100, 0)));
        for (int i = 1; i <= 12; ++i)
            assert(turn.offer(move(100.0f + 15.0f * i, i * 1000.0 / 120.0)));
        assert(turn.offer(up(100)));
        turn.take(100);
        assert(turn.take(150) > 0.0f && turn.active());
        assert(turn.offer(down(500, 160)));
        assert(turn.take(170) == 0.0f && turn.take(400) == 0.0f);
        assert(turn.offer(move(520, 180)) && std::fabs(turn.take(190) - 20.0f / 1280.0f) < 1e-6f);
        assert(turn.offer("turn\tcancel") && !turn.active() && turn.take(300) == 0.0f);
        assert(!turn.offer(move(600, 310)) && turn.take(320) == 0.0f); // no move without a finger down
    }
    CameraTurn bad;
    for (const char *text : {"turn", "turn\tdown\t1\t2", "turn\tdown\t1\t2\t0\t2", "turn\tdown\tx\t2\t1280\t2", "turn\tmove\t1\t2",
                             "turn\tup\t5", "turn\tspin", "zoom\t1.5", "turn\tdown\tnan\t2\t1280\t2"})
        assert(!bad.offer(text));

    // The angle per travel: the ground at the bottom middle of the view moves on the screen by
    // the finger's travel when the camera turns about its look point, at any height.
    const float w = 1.0f / std::tan(1.285f / 2.0f), h = w * 1280.0f / 720.0f; // the world map's projection (world_map.cpp)
    for (const float pitch : {0.5f, 0.8f, 1.0f}) // the world map looks down by 1 rad (WdmCamera)
        for (const float height : {50.0f, 300.0f})
        {
            const float fraction = 0.001f, a = turnAngle(fraction, pitch, w, h);
            // Camera at the origin's height, looking along +z, down by `pitch`; the pivot on the
            // ground ahead; the bottom ray's ground point P. Turning the camera about the pivot by
            // a is P turning about the pivot by -a in the camera's frame.
            const float pivot = height / std::tan(pitch), bottom = std::atan(1.0f / h) + pitch;
            const float pz = height / std::tan(bottom);
            const float rx = (pz - pivot) * std::sin(-a), rz = pivot + (pz - pivot) * std::cos(-a);
            // Camera space: x right, depth along the pitched line of sight.
            const float depth = rz * std::cos(pitch) + height * std::sin(pitch);
            const float screen = rx / depth * w / 2.0f; // fraction of the width from the middle
            assert(std::fabs(screen - fraction) < 0.01f * fraction); // a small turn: linear
            assert(std::fabs(a - turnAngle(fraction, pitch, w, h)) < 1e-9f);
        }
    return 0;
}
