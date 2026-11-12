#include "platform/android_camera_zoom.hpp"
#include <cassert>
#include <cmath>
#include <string>

namespace
{
bool near(float a, float b, float tolerance = 1e-4f)
{
    return std::fabs(a - b) <= tolerance;
}
std::string zoom(float scale)
{
    return "zoom\t" + std::to_string(scale);
}
} // namespace

int main()
{
    using namespace storm::android;

    // The mailbox: the events of a frame multiply (the factors of one gesture telescope to the
    // ratio of its spans), the camera in charge takes them once.
    CameraZoom mailbox;
    assert(!mailbox.beginFrame() && mailbox.take() == 1.0f);
    assert(mailbox.offer(zoom(1.25f)) && mailbox.offer(zoom(1.2f)) && mailbox.offer("zoom\t0.5"));
    assert(near(mailbox.take(), 0.75f) && mailbox.take() == 1.0f);
    for (const char *bad : {"zoom\t", "zoom\t0", "zoom\t-2", "zoom\tnan", "zoom\tinf", "zoom\t1.5x", "zoom 1.5", "pan\t1", "zoom\t1e60"})
        assert(!mailbox.offer(bad));
    assert(mailbox.take() == 1.0f);

    // A pinch no camera took (no frame ran, or the camera in charge has no zoom: the deck) is
    // dropped at the next frame and never applied later as a jump; it is reported once until a
    // camera takes one again.
    assert(mailbox.offer(zoom(2.0f)));
    assert(mailbox.beginFrame());
    assert(mailbox.take() == 1.0f);
    assert(mailbox.offer(zoom(2.0f)) && !mailbox.beginFrame()); // already reported
    assert(mailbox.offer(zoom(1.1f)) && near(mailbox.take(), 1.1f) && !mailbox.beginFrame());
    assert(mailbox.offer(zoom(0.9f)) && mailbox.beginFrame()); // reported again after a taken one

    // Following the fingers: fingers spread twice as far, the camera twice as near (and back).
    assert(near(pinchDistance(200.0f, 2.0f, 50.0f, 300.0f), 100.0f));
    assert(near(pinchDistance(pinchDistance(120.0f, 1.5f, 50.0f, 300.0f), 1.0f / 1.5f, 50.0f, 300.0f), 120.0f));

    // World map: the wheel's height range of WdmCamera (WDM_CAMERA_HEIGHT_MIN / _MAX).
    assert(pinchDistance(120.0f, 10.0f, 50.0f, 300.0f) == 50.0f && pinchDistance(120.0f, 0.1f, 50.0f, 300.0f) == 300.0f);

    // Sea, third person: SHIP_CAMERA fDistance stays in the wheel's 0..1; the parameter moves so
    // that the distance divides by the scale (a linear distance: exactly).
    const auto distance = [](float t) { return 20.0f + t * 60.0f; };
    float t = 0.5f;
    t = pinchParameter(t, 1.25f, distance(t), distance(0.0f), distance(1.0f));
    assert(near(distance(t), 50.0f / 1.25f));
    t = pinchParameter(t, 1.0f / 1.25f, distance(t), distance(0.0f), distance(1.0f));
    assert(near(t, 0.5f));
    assert(pinchParameter(0.5f, 8.0f, distance(0.5f), distance(0.0f), distance(1.0f)) == 0.0f);
    assert(pinchParameter(0.5f, 0.125f, distance(0.5f), distance(0.0f), distance(1.0f)) == 1.0f);
    assert(pinchParameter(0.3f, 2.0f, 10.0f, 10.0f, 10.0f) == 0.3f); // a degenerate range keeps the value

    // Many small events of one gesture end where one event of the whole ratio does.
    float stepped = 200.0f;
    for (int i = 0; i < 20; ++i)
        stepped = pinchDistance(stepped, std::pow(1.6f, 1.0f / 20.0f), 50.0f, 300.0f);
    assert(near(stepped, 125.0f, 0.01f));

    // Spyglass: from no magnification to the glass's own (ChangeTelescopeType's zoom scale).
    assert(near(pinchMagnification(4.0f, 0.5f, 1.0f, 4.0f), 2.0f));
    assert(pinchMagnification(4.0f, 2.0f, 1.0f, 4.0f) == 4.0f && pinchMagnification(2.0f, 0.1f, 1.0f, 4.0f) == 1.0f);

    // Land: the game's radius is the default (zoom 1), a third nearer or farther at most.
    assert(near(kLandZoomMin * 3.0f, 2.0f) && near(kLandZoomMax * 3.0f, 4.0f));
    assert(pinchLandZoom(1.0f, 4.0f) == kLandZoomMin && pinchLandZoom(1.0f, 0.25f) == kLandZoomMax);
    assert(near(pinchLandZoom(1.0f, 1.2f), 1.0f / 1.2f));
    assert(mailbox.land == 1.0f);

    // Deck: field-of-view zoom 1x..2x; the view eases to the fingers' target, and leaving the
    // deck puts it back to the game's 1x at once.
    DeckZoom deck;
    assert(deck.step(0.016f) == 1.0f);
    deck.pinch(1.5f);
    assert(near(deck.target(), 1.5f));
    const float first = deck.step(0.016f);
    assert(first > 1.0f && first < 1.5f); // smooth: part of the way in one frame
    for (int frame = 0; frame < 60; ++frame)
        deck.step(0.016f);
    assert(near(deck.step(0.016f), 1.5f, 1e-3f));
    deck.pinch(4.0f);
    assert(deck.target() == kDeckZoomMax);
    deck.pinch(0.01f);
    assert(deck.target() == kDeckZoomMin);
    deck.pinch(1.8f);
    deck.step(1.0f); // a long frame lands on the target, never past it
    assert(near(deck.step(0.0f), 1.8f));
    deck.reset();
    assert(deck.target() == 1.0f && deck.step(0.016f) == 1.0f);
}
