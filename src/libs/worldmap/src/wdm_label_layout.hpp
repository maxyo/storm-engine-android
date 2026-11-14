// Which world-map labels show when their boxes on the screen collide. The game gives each label
// a weight for that (worldmap_init.c: "Weight is use when shake intersection labels": islands
// 100, towns 50, shores 1) but the engine never used it (WdmIslands::LRender: "place the labels
// so that they do not overlap"): zoomed out, the names were drawn over one another.
//
// The labels are taken by weight (heavier first; equal weights in their order in the game's
// list), and one shows when its box keeps `gap` clear of every box already shown. A label shown
// in the last frame keeps its place while it is no closer than `gap - hold` to them: a pan or a
// zoom that brings two labels to the edge of the gap does not swap them every frame. The result
// depends only on the boxes and the last frame's choice, never on the frame's timing.
// Header-only, standard library only (host check tests/worldmap_labels).
#pragma once

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <span>
#include <vector>

namespace storm::worldmap
{

struct LabelBox
{
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f; // px on the screen
    uint32_t weight = 0;
    bool shown = false; // in: shown in the last frame; out: shows in this one
};

class LabelLayout
{
  public:
    void Place(std::span<LabelBox> boxes, float gap, float hold)
    {
        order_.resize(boxes.size());
        std::iota(order_.begin(), order_.end(), size_t{0});
        std::stable_sort(order_.begin(), order_.end(),
                         [&](size_t a, size_t b) { return boxes[a].weight > boxes[b].weight; });
        placed_.clear();
        for (const size_t index : order_)
        {
            LabelBox &box = boxes[index];
            const float clear = box.shown ? gap - hold : gap;
            const bool free = std::none_of(placed_.begin(), placed_.end(), [&](const LabelBox *other) {
                return box.left < other->right + clear && other->left < box.right + clear && box.top < other->bottom + clear &&
                       other->top < box.bottom + clear;
            });
            box.shown = free;
            if (free)
                placed_.push_back(&box);
        }
    }

  private:
    std::vector<size_t> order_;
    std::vector<const LabelBox *> placed_;
};

} // namespace storm::worldmap
