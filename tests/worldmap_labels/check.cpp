#include "wdm_label_layout.hpp"

#include <cassert>
#include <array>
#include <cstdio>
#include <vector>

using storm::worldmap::LabelBox;
using storm::worldmap::LabelLayout;

namespace
{
LabelBox Box(float x, float y, uint32_t weight, bool shown = false)
{
    return {x, y, x + 100.0f, y + 20.0f, weight, shown};
}
std::vector<bool> Shown(const std::vector<LabelBox> &boxes)
{
    std::vector<bool> out;
    for (const LabelBox &box : boxes)
        out.push_back(box.shown);
    return out;
}
} // namespace

int main()
{
    LabelLayout layout;
    constexpr float gap = 8.0f, hold = 6.0f;
    // WM-UI-01: the projected Dominica caption was under the compass. Reserve the
    // measured WHOLE component (date and days too), at both captured viewport sizes.
    for (const auto &viewport : std::vector<std::array<float, 6>>{{960, 540, 702, 15, 240, 219}, {1280, 720, 1022, 15, 240, 219}})
    {
        const auto [w, h, x, y, cw, ch] = viewport;
        const LabelBox compass{x, y, x + cw, y + ch};
        LabelBox name{x - 12, 150, x + 138, 180, 100, true};
        const float anchorX = x + 63, anchorY = 150;
        assert(name.right > compass.left && name.top < compass.bottom); // reproduces the old overlap
        assert(storm::worldmap::AvoidCompass(name, anchorX, anchorY, w, h, compass, gap));
        assert(name.top == compass.bottom + gap && name.left == x - 12); // nearest clear position: below days
        assert(name.bottom <= h && name.right <= w);
        assert(!storm::worldmap::AvoidCompass(name, anchorX, anchorY, w, h, compass, gap));
        // The moved caption still participates in normal weighted name collisions.
        std::vector<LabelBox> boxes{name, Box(name.left, name.top, 50), Box(30, 300, 100)};
        layout.Place(boxes, gap, hold);
        assert((Shown(boxes) == std::vector<bool>{true, false, true}));
        // An offscreen island's partly visible name keeps its projection; no global edge clamp.
        LabelBox offscreen{w - 80, 150, w + 70, 180};
        const auto before = offscreen;
        assert(!storm::worldmap::AvoidCompass(offscreen, w + 10, 150, w, h, compass, gap));
        assert(offscreen.left == before.left && offscreen.top == before.top);
    }
    // A caption close to the compass's left edge moves left when that is shorter.
    {
        LabelBox name{650, 150, 720, 180};
        assert(storm::worldmap::AvoidCompass(name, 685, 150, 960, 540, {702, 15, 942, 234}, gap));
        assert(name.right == 694 && name.top == 150);
    }
    // Apart: all show.
    {
        std::vector<LabelBox> boxes{Box(0, 0, 1), Box(200, 0, 1), Box(0, 100, 50)};
        layout.Place(boxes, gap, hold);
        assert((Shown(boxes) == std::vector<bool>{true, true, true}));
    }
    // Colliding: the heavier shows whatever the order; equal weights: the first in the game's list.
    {
        std::vector<LabelBox> boxes{Box(0, 0, 1), Box(50, 5, 100), Box(60, 10, 50), Box(300, 0, 50), Box(330, 5, 50)};
        layout.Place(boxes, gap, hold);
        assert((Shown(boxes) == std::vector<bool>{false, true, false, true, false}));
    }
    // A chain: the heaviest hides its neighbour, which then hides nothing (a hidden label takes no room).
    {
        std::vector<LabelBox> boxes{Box(0, 0, 100), Box(90, 0, 50), Box(180, 0, 1)};
        layout.Place(boxes, gap, hold);
        assert((Shown(boxes) == std::vector<bool>{true, false, true}));
    }
    // The gap: closer than it collides.
    {
        std::vector<LabelBox> boxes{Box(0, 0, 100), Box(105, 0, 1)};
        layout.Place(boxes, gap, hold);
        assert(!boxes[1].shown);
        boxes = {Box(0, 0, 100), Box(110, 0, 1)};
        layout.Place(boxes, gap, hold);
        assert(boxes[1].shown);
    }
    // No flicker: a camera that jitters by a pixel across the gap's edge (a pan, a zoom) does not
    // swap the labels every frame. A label shown keeps its place down to gap - hold; a hidden one
    // needs the whole gap.
    {
        std::vector<LabelBox> boxes;
        int changes = 0;
        bool last = true;
        for (int frame = 0; frame < 200; ++frame)
        {
            // From 30 px apart to 0 and back, with a +-1 px jitter every frame.
            const float distance = (frame < 100 ? 30.0f - 0.3f * frame : 0.3f * (frame - 100)) + (frame % 2 ? 1.0f : -1.0f);
            const bool shown = boxes.empty() ? true : boxes[1].shown;
            boxes = {Box(0, 0, 100, true), Box(100.0f + distance, 0, 1, shown)};
            layout.Place(boxes, gap, hold);
            changes += boxes[1].shown != last;
            last = boxes[1].shown;
        }
        std::printf("the lighter label changed %d times over the approach and the retreat\n", changes);
        assert(changes == 2); // hidden once on the way in, shown once on the way out
    }
    // Deterministic: the same boxes and last choice give the same result.
    {
        std::vector<LabelBox> a{Box(0, 0, 50), Box(40, 0, 50), Box(80, 0, 50), Box(120, 0, 50)}, b = a;
        layout.Place(a, gap, hold);
        LabelLayout other;
        other.Place(b, gap, hold);
        assert(Shown(a) == Shown(b));
        assert((Shown(a) == std::vector<bool>{true, false, false, true}));
    }
    return 0;
}
