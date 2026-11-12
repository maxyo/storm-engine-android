// Touch behaviours shared by the components. A component lists them in its spec; the
// template marks the parts they act on with data-* attributes:
//
//   press   data-action="verb" [data-value] [data-disabled data-reason] [data-hold]
//           [data-step="±n" | data-set="n" | data-toggle="prop" | data-select="arrayProp"
//            data-index="i"] [data-prop="value"]
//           Without a marked part the whole component is the button (verb "click") unless
//           the spec sets constants.pressRoot to false.
//   range   data-range="prop" on the track (props min, max, step)
//   stick   data-stick="x|xy" on the travel area, data-knob on the knob; prop `floating`:
//           the component's box is the zone, the travel area goes under the finger (atX, atY)
//   lever   data-lever="steps|continuous" on the rail (prop value / dir)
//   columns data-col="i" on the head and row cells of column i: every cell of a column gets
//           the width of its widest content (the cells are laid out shrink-to-fit first,
//           then given a common min-width), so numeric columns line up without the
//           screen guessing widths
//
//   panzoom data-panzoom on the viewport (clips), data-plane on the picture plane inside it;
//           props base [w, h] and view [x0, y0, x1, y1] (the part shown first) in the units the
//           game measures the picture in. One finger pans, two pinch; a tap reports
//           `point x,y` in those units; parts marked data-zoom="in|out" zoom one step around
//           the centre. The view is the component's own: nothing goes to the engine for it.
//           A change of the prop `room` (the layout around the view) refits it to the new size.
//
// Two more are given to single elements with data-behavior="…" (HUD layout), on top of
// what their component has:
//   hold    reports `down` and `up`: a control that acts for as long as it is held
//   swipe   a drag from the element picks one of several moves by direction and reports
//           `open <id>`, `preview <id>`, then `commit <id>` or `cancel`:
//           data-swipe-ids="a,b,…" with data-swipe-bounds="deg,deg,…" (one more bound than
//           ids, clockwise from the positive x axis, y down), data-swipe-centre="id" inside
//           data-swipe-dead dp
#include "components.h"
#include "elements.h"
#include "touch_motion.h"

#include <RmlUi/Core/EventListener.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace storm::ui
{

namespace
{

// Timings come from the design tokens (pack.json "settings"); these are the fallbacks.
double HoldSeconds(ComponentElement &self)
{
    return self.Host().Setting("holdConfirmMs", 800.0) / 1000.0; // hold-to-confirm ring
}
double DoneSeconds(ComponentElement &self)
{
    return self.Host().Setting("holdFlashMs", 280.0) / 1000.0; // completion flash
}
double TooltipSeconds(ComponentElement &self)
{
    return self.Host().Setting("tooltipMs", 400.0) / 1000.0; // long press before the tooltip opens
}

Rml::Element *ClosestWith(Rml::Element *from, Rml::Element *root, const char *attribute)
{
    for (Rml::Element *element = from; element; element = element->GetParentNode())
    {
        if (element->HasAttribute(attribute))
            return element;
        if (element == root)
            break;
    }
    return nullptr;
}

Rml::Element *FindWith(Rml::Element *root, const char *attribute)
{
    Rml::ElementList found;
    root->QuerySelectorAll(found, Rml::String("[") + attribute + "]");
    return found.empty() ? nullptr : found.front();
}

std::string Attr(Rml::Element *element, const char *name, const char *fallback = "")
{
    return element->GetAttribute<Rml::String>(name, fallback);
}

// Where the element's border box is on the screen: its layout box moved by the transforms on it
// and its ancestors (a translate exactly, such as the host's move off a display cutout; others
// as at the box's corner), so pointers meet what is drawn.
Rml::Rectanglef BorderBox(Rml::Element *element)
{
    Rml::Vector2f origin = element->GetAbsoluteOffset(Rml::BoxArea::Border);
    if (Rml::Vector2f local = origin; element->GetTransformState() && element->Project(local))
        origin += origin - local;
    return Rml::Rectanglef::FromPositionSize(origin, element->GetBox().GetSize(Rml::BoxArea::Border));
}

bool Inside(Rml::Element *element, Rml::Vector2f point, float slop)
{
    const Rml::Rectanglef box = BorderBox(element);
    return point.x >= box.Left() - slop && point.x <= box.Right() + slop && point.y >= box.Top() - slop && point.y <= box.Bottom() + slop;
}

double Number(const Json &value, double fallback = 0.0)
{
    return value.is_number() ? value.get<double>() : fallback;
}

void AllWith(Rml::Element *root, const char *attribute, std::vector<Rml::Element *> &out)
{
    Rml::ElementList found;
    root->QuerySelectorAll(found, Rml::String("[") + attribute + "]");
    out.insert(out.end(), found.begin(), found.end());
}

std::vector<std::string> Split(const std::string &text)
{
    std::vector<std::string> parts;
    size_t at = 0;
    while (at <= text.size())
    {
        size_t end = text.find(',', at);
        if (end == std::string::npos)
            end = text.size();
        parts.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    return parts;
}

class PressBehavior final : public Behavior
{
  public:
    bool Down(ComponentElement &self, const Pointer &pointer) override
    {
        if (part_ || done_until_ > 0.0)
            return false;
        Rml::Element *part = ClosestWith(pointer.target, &self, "data-action");
        if (!part)
        {
            if (!Truthy(self.Spec().constants.value("pressRoot", Json(true))))
                return false;
            part = &self;
        }
        part_ = part;
        part_alive_ = part->GetObserverPtr();
        pointer_ = pointer.id;
        if (self.Flag("busy"))
            return true;
        SetPressed(self, true);
        down_at_ = self.Host().Now();
        const bool disabled = Disabled(self);
        if (!disabled)
            self.Host().PressStarted(self);
        holding_ = !disabled && HoldMode(self) == Hold::Confirm;
        tooltip_pending_ = HoldMode(self) == Hold::Tooltip;
        tooltip_open_ = false;
        if (holding_ || tooltip_pending_)
            self.Host().RequestTicks(self, true);
        return true;
    }

    void Up(ComponentElement &self, const Pointer &pointer, bool cancelled) override
    {
        if (!part_ || pointer.id != pointer_)
            return;
        Rml::Element *part = part_;
        const bool was_holding = holding_;
        const bool tooltip_was_open = tooltip_open_;
        Release(self);
        if (cancelled || self.Flag("busy") || tooltip_was_open)
            return;
        if (!Inside(part, pointer.position, 12.0f * self.Host().Density()))
            return;
        if (part == &self ? self.Flag("disabled") : part->HasAttribute("data-disabled"))
        {
            const std::string reason =
                part == &self ? ToText(self.Prop("reason")) + ToText(self.Prop("disabledReason")) : Attr(part, "data-reason");
            if (!reason.empty())
                self.Host().Toast(reason, "info");
            return;
        }
        if (was_holding) // released before the ring closed: nothing happens
            return;
        Activate(self, part);
    }

    void Tick(ComponentElement &self, double now) override
    {
        if (done_until_ > 0.0 && now >= done_until_)
        {
            done_until_ = 0.0;
            self.SetLive("done", false);
            SetRing(self, &self, self.Prop("holdProgress"));
            self.Host().RequestTicks(self, false);
            return;
        }
        if (!part_)
            return;
        if (holding_)
        {
            const double progress = std::min(1.0, (now - down_at_) / HoldSeconds(self));
            SetRing(self, part_, progress * 100.0);
            if (progress >= 1.0)
            {
                Rml::Element *part = part_;
                holding_ = false;
                SetPressed(self, false);
                part_ = nullptr;
                self.SetLive("done", true);
                done_until_ = now + DoneSeconds(self);
                Activate(self, part);
            }
        }
        else if (tooltip_pending_ && now - down_at_ >= TooltipSeconds(self))
        {
            tooltip_pending_ = false;
            tooltip_open_ = true;
            std::string text = ToText(self.Prop("tip"));
            if (text.empty())
                text = ToText(self.Prop("title"));
            self.Host().Tooltip(&self, text, ToText(self.Prop("tipPlace")) == "below");
        }
    }

    void Regions(ComponentElement &self, std::vector<Rml::Element *> &out) const override
    {
        const size_t before = out.size();
        AllWith(&self, "data-action", out);
        if (out.size() == before && Truthy(self.Spec().constants.value("pressRoot", Json(true))))
            out.push_back(&self);
    }

    void Rebuilt(ComponentElement &self) override
    {
        // The inner tree is patched in place; a pressed part usually survives. If it was
        // replaced, the press is over.
        if (part_ && part_ != &self && !part_alive_)
        {
            part_ = nullptr;
            holding_ = false;
        }
        if (part_ == &self && holding_)
            return;
        if (done_until_ == 0.0 && !part_)
            self.Host().RequestTicks(self, false);
    }

  private:
    enum class Hold
    {
        None,
        Confirm,
        Tooltip
    };

    Hold HoldMode(ComponentElement &self) const
    {
        if (part_ != &self)
            return part_->HasAttribute("data-hold") ? Hold::Confirm : Hold::None;
        if (!self.Flag("hold"))
            return Hold::None;
        if (self.Spec().constants.value("holdMode", std::string("confirm")) != "tooltip")
            return Hold::Confirm;
        // A tooltip repeats the title only when the title was cut to fit (an explicit tip
        // always opens); otherwise a long press is a plain press.
        if (ToText(self.Prop("tip")).empty() && ClampedTextIn(&self) == 0)
            return Hold::None;
        return Hold::Tooltip;
    }

    bool Disabled(ComponentElement &self) const
    {
        return part_ == &self ? self.Flag("disabled") : part_->HasAttribute("data-disabled");
    }

    void SetPressed(ComponentElement &self, bool on)
    {
        if (!part_)
            return;
        if (part_ == &self)
            self.SetLive("pressed", on);
        else
            part_->SetClass("pressed", on);
    }

    static void SetRing(ComponentElement &self, Rml::Element *part, const Json &percent)
    {
        Rml::Element *ring = part ? part->QuerySelector("ui-ring") : nullptr;
        if (!ring)
            ring = self.QuerySelector("ui-ring");
        if (ring)
            ring->SetAttribute("value", static_cast<float>(Number(percent)));
    }

    void Release(ComponentElement &self)
    {
        if (holding_)
            SetRing(self, part_, self.Prop("holdProgress"));
        if (tooltip_open_)
            self.Host().Tooltip(nullptr, {}, false);
        SetPressed(self, false);
        part_ = nullptr;
        holding_ = false;
        tooltip_pending_ = false;
        tooltip_open_ = false;
        if (done_until_ == 0.0)
            self.Host().RequestTicks(self, false);
    }

    // Applies the optimistic local change the part asks for, then reports to the game.
    static void Activate(ComponentElement &self, Rml::Element *part)
    {
        if (part == &self)
        {
            self.Emit("click");
            return;
        }
        const std::string verb = Attr(part, "data-action", "click");
        std::string value = Attr(part, "data-value");
        const std::string prop = Attr(part, "data-prop", "value");
        if (part->HasAttribute("data-step") || part->HasAttribute("data-set"))
        {
            const double low = Number(self.Prop("min"), 0.0);
            const double high = Number(self.Prop("max"), 100.0);
            double next = part->HasAttribute("data-set") ? part->GetAttribute<float>("data-set", 0.0f)
                                                         : Number(self.Prop(prop)) + part->GetAttribute<float>("data-step", 0.0f);
            next = std::clamp(next, std::min(low, high), std::max(low, high));
            value = ToText(Json(next));
            self.SetProps(Json{{prop, next}});
        }
        else if (part->HasAttribute("data-toggle"))
        {
            const std::string flag = Attr(part, "data-toggle");
            const bool next = !self.Flag(flag);
            value = next ? "1" : "0";
            self.SetProps(Json{{flag, next}});
        }
        else if (part->HasAttribute("data-select"))
        {
            const std::string list = Attr(part, "data-select");
            const int index = part->GetAttribute<int>("data-index", 0);
            Json items = self.Prop(list);
            if (items.is_array())
            {
                for (size_t i = 0; i < items.size(); ++i)
                    if (items[i].is_object() && !items[i].contains("divider"))
                        items[i]["selected"] = static_cast<int>(i) == index;
                self.SetProps(Json{{list, items}});
            }
            if (value.empty())
                value = std::to_string(index);
        }
        self.Emit(verb, value);
    }

    Rml::Element *part_ = nullptr;
    Rml::ObserverPtr<Rml::Element> part_alive_;
    int pointer_ = -1;
    double down_at_ = 0.0;
    double done_until_ = 0.0;
    bool holding_ = false;
    bool tooltip_pending_ = false;
    bool tooltip_open_ = false;
};

// Horizontal slider: the finger position maps straight to the value.
class RangeBehavior final : public Behavior
{
  public:
    bool Exclusive(const Pointer &pointer) const override
    {
        return pointer.target && ClosestWith(pointer.target, nullptr, "data-range") != nullptr;
    }

    bool Down(ComponentElement &self, const Pointer &pointer) override
    {
        Rml::Element *track = ClosestWith(pointer.target, &self, "data-range");
        if (!track || active_)
            return false;
        if (self.Flag("disabled"))
        {
            const std::string reason = ToText(self.Prop("disabledReason")) + ToText(self.Prop("reason"));
            if (!reason.empty())
                self.Host().Toast(reason, "info");
            return true;
        }
        active_ = true;
        pointer_ = pointer.id;
        prop_ = Attr(track, "data-range", "value");
        if (prop_.empty())
            prop_ = "value";
        const Rml::Rectanglef box = BorderBox(track);
        const float inset = track->GetAttribute<float>("data-inset", 0.0f) * self.Host().Density();
        left_ = box.Left() + inset;
        width_ = std::max(1.0f, box.Width() - 2.0f * inset);
        Apply(self, pointer.position.x, true);
        return true;
    }

    void Move(ComponentElement &self, const Pointer &pointer) override
    {
        if (active_ && pointer.id == pointer_)
            Apply(self, pointer.position.x, true);
    }

    void Up(ComponentElement &self, const Pointer &pointer, bool) override
    {
        if (!active_ || pointer.id != pointer_)
            return;
        active_ = false;
        self.SetProps(Json{{"active", false}});
        self.Emit("commit", ToText(self.Prop(prop_)));
    }

    void Regions(ComponentElement &self, std::vector<Rml::Element *> &out) const override
    {
        AllWith(&self, "data-range", out);
    }

  private:
    void Apply(ComponentElement &self, float x, bool active)
    {
        const double low = Number(self.Prop("min"), 0.0);
        const double high = Number(self.Prop("max"), 100.0);
        const double step = std::max(1e-6, Number(self.Prop("step"), 1.0));
        const double t = std::clamp((x - left_) / width_, 0.0f, 1.0f);
        const double value = std::clamp(low + std::round(t * (high - low) / step) * step, std::min(low, high), std::max(low, high));
        const bool changed = Number(self.Prop(prop_), low - 1.0) != value;
        if (changed || self.Flag("active") != active)
            self.SetProps(Json{{prop_, value}, {"active", active}});
        if (changed)
            self.Emit("change", ToText(Json(value)));
    }

    bool active_ = false;
    int pointer_ = -1;
    std::string prop_;
    float left_ = 0.0f;
    float width_ = 1.0f;
};

// Helm capsule (x only) and movement stick (x and y). Springs back on release.
// With the prop `floating` the component's box is a zone: the stick appears under the
// finger that lands anywhere in it and returns to its place of rest on release.
class StickBehavior final : public Behavior
{
  public:
    bool Exclusive(const Pointer &) const override
    {
        return true;
    }

    bool Down(ComponentElement &self, const Pointer &pointer) override
    {
        Rml::Element *area = FindWith(&self, "data-stick");
        if (!area || active_)
            return false;
        active_ = true;
        pointer_ = pointer.id;
        both_ = Attr(area, "data-stick") == "xy";
        const Rml::Rectanglef box = BorderBox(area);
        const float knob = area->GetAttribute<float>("data-knob-size", 52.0f) * self.Host().Density();
        centre_ = box.Center();
        reach_ = {std::max(1.0f, (box.Width() - knob) * 0.5f), std::max(1.0f, (box.Height() - knob) * 0.5f)};
        if (self.Flag("floating"))
        {
            // The base comes to the finger and stays there for this gesture; the template
            // places it by `atX` / `atY`, dp from the top left corner of the zone.
            centre_ = pointer.position;
            const Rml::Vector2f corner = (centre_ - box.Size() * 0.5f - BorderBox(&self).TopLeft()) / self.Host().Density();
            self.SetProps(Json{{"atX", std::round(corner.x)}, {"atY", std::round(corner.y)}});
        }
        Apply(self, pointer.position);
        return true;
    }

    void Move(ComponentElement &self, const Pointer &pointer) override
    {
        if (active_ && pointer.id == pointer_)
            Apply(self, pointer.position);
    }

    void Up(ComponentElement &self, const Pointer &pointer, bool) override
    {
        if (!active_ || pointer.id != pointer_)
            return;
        active_ = false;
        self.SetProps(Json{{"value", 0}, {"valueY", 0}, {"active", false}});
        self.Emit("change", both_ ? "0,0" : "0");
    }

    void Regions(ComponentElement &self, std::vector<Rml::Element *> &out) const override
    {
        if (self.Flag("floating")) // the whole box of the component is the zone the stick appears in
            out.push_back(&self);
        else
            AllWith(&self, "data-stick", out);
    }

  private:
    void Apply(ComponentElement &self, Rml::Vector2f at)
    {
        Rml::Vector2f offset = {(at.x - centre_.x) / reach_.x, both_ ? (at.y - centre_.y) / reach_.y : 0.0f};
        if (both_)
        {
            const float length = offset.Magnitude();
            if (length > 1.0f)
                offset /= length;
        }
        else
            offset.x = std::clamp(offset.x, -1.0f, 1.0f);
        const double x = std::round(offset.x * 100.0f), y = std::round(offset.y * 100.0f);
        if (Number(self.Prop("value")) == x && Number(self.Prop("valueY")) == y && self.Flag("active"))
            return;
        self.SetProps(Json{{"value", x}, {"valueY", y}, {"active", true}});
        self.Emit("change", both_ ? ToText(Json(x)) + "," + ToText(Json(y)) : ToText(Json(x)));
    }

    bool active_ = false;
    bool both_ = false;
    int pointer_ = -1;
    Rml::Vector2f centre_;
    Rml::Vector2f reach_ = {1.0f, 1.0f};
};

// Sail lever. steps: the knob snaps to one of the positions under the finger.
// continuous: pushing up or down from where the finger landed holds SailUp / SailDown.
class LeverBehavior final : public Behavior
{
  public:
    bool Exclusive(const Pointer &) const override
    {
        return true;
    }

    bool Down(ComponentElement &self, const Pointer &pointer) override
    {
        Rml::Element *rail = FindWith(&self, "data-lever");
        if (!rail || active_)
            return false;
        active_ = true;
        pointer_ = pointer.id;
        continuous_ = Attr(rail, "data-lever") == "continuous";
        steps_ = std::max(2, rail->GetAttribute<int>("data-steps", 3));
        const Rml::Rectanglef box = BorderBox(rail);
        const float knob = rail->GetAttribute<float>("data-knob-size", 48.0f) * self.Host().Density();
        top_ = box.Top() + knob * 0.5f;
        travel_ = std::max(1.0f, box.Height() - knob);
        Apply(self, pointer);
        return true;
    }

    void Move(ComponentElement &self, const Pointer &pointer) override
    {
        if (active_ && pointer.id == pointer_)
            Apply(self, pointer);
    }

    void Up(ComponentElement &self, const Pointer &pointer, bool) override
    {
        if (!active_ || pointer.id != pointer_)
            return;
        active_ = false;
        if (continuous_)
        {
            if (!ToText(self.Prop("dir")).empty())
                self.Emit("hold", "");
            self.SetProps(Json{{"dir", ""}, {"active", false}});
        }
        else
            self.SetProps(Json{{"active", false}});
    }

    void Regions(ComponentElement &self, std::vector<Rml::Element *> &out) const override
    {
        AllWith(&self, "data-lever", out);
    }

  private:
    void Apply(ComponentElement &self, const Pointer &pointer)
    {
        if (continuous_)
        {
            const float dead = 10.0f * self.Host().Density();
            const float dy = pointer.position.y - pointer.start.y;
            const std::string dir = dy < -dead ? "up" : dy > dead ? "down" : "";
            if (ToText(self.Prop("dir")) != dir || !self.Flag("active"))
            {
                const bool changed = ToText(self.Prop("dir")) != dir;
                self.SetProps(Json{{"dir", dir}, {"active", true}});
                if (changed)
                    self.Emit("hold", dir);
            }
            return;
        }
        const float t = 1.0f - std::clamp((pointer.position.y - top_) / travel_, 0.0f, 1.0f);
        const double value = std::round(t * static_cast<float>(steps_ - 1));
        const bool changed = Number(self.Prop("value"), -1.0) != value;
        if (changed || !self.Flag("active"))
            self.SetProps(Json{{"value", value}, {"active", true}});
        if (changed)
            self.Emit("change", ToText(Json(value)));
    }

    bool active_ = false;
    bool continuous_ = false;
    int pointer_ = -1;
    int steps_ = 3;
    float top_ = 0.0f;
    float travel_ = 1.0f;
};

// A control that acts while it is held (block): `down` on the press, `up` on the release
// or when the gesture is taken away. The finger may wander; it never becomes a scroll.
class HoldBehavior final : public Behavior
{
  public:
    bool Exclusive(const Pointer &) const override
    {
        return true;
    }

    bool Down(ComponentElement &self, const Pointer &pointer) override
    {
        if (held_ || self.Flag("disabled"))
            return !held_;
        held_ = true;
        pointer_ = pointer.id;
        self.Emit("down");
        return true;
    }

    void Up(ComponentElement &self, const Pointer &pointer, bool) override
    {
        if (!held_ || pointer.id != pointer_)
            return;
        held_ = false;
        self.Emit("up");
    }

    void Regions(ComponentElement &self, std::vector<Rml::Element *> &out) const override
    {
        out.push_back(&self);
    }

  private:
    bool held_ = false;
    int pointer_ = -1;
};

// One finger picks one of several moves: the direction of the drag from where it went
// down selects a sector, staying near that point selects the centre. Only the release
// commits; a cancelled gesture commits nothing.
class SwipeBehavior final : public Behavior
{
  public:
    bool Exclusive(const Pointer &) const override
    {
        return true;
    }

    bool Down(ComponentElement &self, const Pointer &pointer) override
    {
        if (active_ || self.Flag("disabled"))
            return !active_;
        ids_ = Split(Attr(&self, "data-swipe-ids"));
        bounds_.clear();
        for (const std::string &bound : Split(Attr(&self, "data-swipe-bounds")))
            bounds_.push_back(std::strtof(bound.c_str(), nullptr));
        centre_ = Attr(&self, "data-swipe-centre");
        if (ids_.empty() || bounds_.size() != ids_.size() + 1 || centre_.empty())
        {
            Rml::Log::Message(Rml::Log::LT_ERROR, "Behaviour swipe on %s: data-swipe-ids, -bounds (one more than ids) and -centre are required",
                              self.GetAddress().c_str());
            return true;
        }
        dead_ = self.GetAttribute<float>("data-swipe-dead", 20.0f) * self.Host().Density();
        active_ = true;
        pointer_ = pointer.id;
        selected_ = centre_;
        self.Emit("open", selected_);
        return true;
    }

    void Move(ComponentElement &self, const Pointer &pointer) override
    {
        if (!active_ || pointer.id != pointer_)
            return;
        const std::string selected = Selection(pointer.position - pointer.start);
        if (selected == selected_)
            return;
        selected_ = selected;
        self.Emit("preview", selected_);
    }

    void Up(ComponentElement &self, const Pointer &pointer, bool cancelled) override
    {
        if (!active_ || pointer.id != pointer_)
            return;
        active_ = false;
        if (cancelled)
            self.Emit("cancel");
        else
            self.Emit("commit", Selection(pointer.position - pointer.start));
    }

    void Regions(ComponentElement &self, std::vector<Rml::Element *> &out) const override
    {
        out.push_back(&self);
    }

  private:
    std::string Selection(Rml::Vector2f drag) const
    {
        if (drag.Magnitude() < dead_)
            return centre_;
        float angle = std::atan2(drag.y, drag.x) * 180.0f / 3.14159265f;
        while (angle < bounds_.front())
            angle += 360.0f;
        for (size_t i = 0; i < ids_.size(); ++i)
            if (angle >= bounds_[i] && angle < bounds_[i + 1])
                return ids_[i];
        return centre_;
    }

    bool active_ = false;
    int pointer_ = -1;
    float dead_ = 0.0f;
    std::string centre_, selected_;
    std::vector<std::string> ids_;
    std::vector<float> bounds_;
};

class PanZoomBehavior final : public Behavior
{
  public:
    bool Exclusive(const Pointer &) const override
    {
        return true;
    }

    bool Down(ComponentElement &self, const Pointer &pointer) override
    {
        Rml::Element *port = FindWith(&self, "data-panzoom");
        if (!port || fingers_.size() >= 2 || !Ready(self, port))
            return false;
        if (fingers_.empty())
        {
            moved_ = false;
            fling_[0].Stop(), fling_[1].Stop(); // a finger stops a picture in motion
            tracker_.Clear();
            tracker_.Add(pointer.time, {pointer.position.x, pointer.position.y});
            Rml::Element *zoom = ClosestWith(pointer.target, &self, "data-zoom");
            zoom_ = zoom ? Attr(zoom, "data-zoom") : std::string();
            zoom_box_ = zoom ? BorderBox(zoom) : Rml::Rectanglef();
        }
        fingers_.push_back({pointer.id, pointer.position});
        if (fingers_.size() == 2)
        {
            moved_ = true; // a pinch is never a tap
            zoom_.clear();
            pinch_distance_ = std::max(1.0f, (fingers_[0].at - fingers_[1].at).Magnitude());
            pinch_scale_ = scale_;
            pinch_anchor_ = (Middle() - BorderBox(port).TopLeft() - offset_) / scale_; // base point under the fingers
        }
        return true;
    }

    void Move(ComponentElement &self, const Pointer &pointer) override
    {
        Finger *finger = Find(pointer.id);
        Rml::Element *port = FindWith(&self, "data-panzoom");
        if (!finger || !port || !ready_)
            return;
        const Rml::Vector2f last = finger->at;
        finger->at = pointer.position;
        if (fingers_.size() == 2)
        {
            const float distance = std::max(1.0f, (fingers_[0].at - fingers_[1].at).Magnitude());
            scale_ = std::clamp(pinch_scale_ * distance / pinch_distance_, min_scale_, max_scale_);
            offset_ = Middle() - BorderBox(port).TopLeft() - pinch_anchor_ * scale_;
            Apply(self);
            return;
        }
        if (!zoom_.empty())
            return;
        tracker_.Add(pointer.time, {pointer.position.x, pointer.position.y});
        const float slop = 8.0f * self.Host().Density();
        if (!moved_ && (pointer.position - pointer.start).SquaredMagnitude() <= slop * slop)
            return;
        moved_ = true;
        offset_ += pointer.position - last; // 1:1 with the finger at every zoom (offset_ is in screen pixels)
        Apply(self);
    }

    void Up(ComponentElement &self, const Pointer &pointer, bool cancelled) override
    {
        if (!Find(pointer.id))
            return;
        const bool panned = fingers_.size() == 1 && moved_ && zoom_.empty();
        fingers_.erase(std::remove_if(fingers_.begin(), fingers_.end(), [&](const Finger &f) { return f.id == pointer.id; }), fingers_.end());
        if (fingers_.size() == 1) // the pinch ends: the finger left pans on from where it is (no velocity of the pinch)
            tracker_.Clear();
        if (panned && !cancelled && ready_)
            Throw(self, pointer);
        if (!fingers_.empty() || moved_ || cancelled)
            return;
        Rml::Element *port = FindWith(&self, "data-panzoom");
        if (!port || !ready_)
            return;
        if (!zoom_.empty())
        {
            const float slop = 12.0f * self.Host().Density();
            const Rml::Vector2f at = pointer.position;
            if (at.x >= zoom_box_.Left() - slop && at.x <= zoom_box_.Right() + slop && at.y >= zoom_box_.Top() - slop &&
                at.y <= zoom_box_.Bottom() + slop)
            {
                const Rml::Vector2f centre = BorderBox(port).Size() * 0.5f;
                const Rml::Vector2f anchor = (centre - offset_) / scale_;
                scale_ = std::clamp(scale_ * (zoom_ == "out" ? 1.0f / kStep : kStep), min_scale_, max_scale_);
                offset_ = centre - anchor * scale_;
                Apply(self);
            }
            zoom_.clear();
            return;
        }
        const Rml::Vector2f at = (pointer.position - BorderBox(port).TopLeft() - offset_) / scale_;
        if (at.x < 0.0f || at.y < 0.0f || at.x > base_.x || at.y > base_.y)
            return;
        char value[48];
        std::snprintf(value, sizeof value, "%.1f,%.1f", at.x, at.y);
        self.Emit("point", value);
    }

    // Ticks while the picture coasts after a pan (placed where the fling is at this frame's
    // time), and after a rebuild, when the viewport may change size (a sheet opens beside it)
    // once the layout has run: a few frames re-check it.
    void Tick(ComponentElement &self, double now) override
    {
        Rml::Element *port = FindWith(&self, "data-panzoom");
        if (fling_[0].Active() || fling_[1].Active())
        {
            const Rml::Vector2f wanted = {fling_[0].Active() ? fling_[0].Advance(now) : offset_.x, fling_[1].Active() ? fling_[1].Advance(now) : offset_.y};
            offset_ = wanted;
            Apply(self);
            for (int i = 0; i < 2; ++i)
                if (offset_[i] != wanted[i]) // held at an edge: that axis is done
                    fling_[i].Stop();
        }
        const bool settled = !port || (Ready(self, port) && --settle_ <= 0);
        if (settled && !fling_[0].Active() && !fling_[1].Active())
            self.Host().RequestTicks(self, false);
    }

    void Rebuilt(ComponentElement &self) override
    {
        // A new picture (or measure) starts at the game's view; anything else keeps the
        // player's pan and zoom on the rebuilt plane.
        const std::string key = ToText(self.Prop("texture")) + "|" + ToText(self.Prop("base"));
        if (key != key_)
        {
            key_ = key;
            ready_ = false;
        }
        if (ready_)
            Apply(self);
        settle_ = 3;
        self.Host().RequestTicks(self, true); // fitted (again) once the viewport has its size
    }

    void Regions(ComponentElement &self, std::vector<Rml::Element *> &out) const override
    {
        AllWith(&self, "data-panzoom", out);
    }

  private:
    static constexpr float kStep = 1.6f;

    // A pan released in motion coasts on (the lists' fling, touch_motion.h).
    void Throw(ComponentElement &self, const Pointer &pointer)
    {
        tracker_.Lift(pointer.time);
        const touch::Point velocity = tracker_.Velocity();
        const float density = self.Host().Density();
        bool thrown = false;
        for (int i = 0; i < 2; ++i)
            if (std::fabs(velocity[i]) >= kMinFlingDp * density)
            {
                fling_[i].Start(pointer.time, offset_[i], std::clamp(velocity[i], -kMaxFlingDp * density, kMaxFlingDp * density), density);
                thrown = true;
            }
        if (thrown)
            self.Host().RequestTicks(self, true);
    }

    struct Finger
    {
        int id;
        Rml::Vector2f at;
    };

    Finger *Find(int id)
    {
        for (Finger &finger : fingers_)
            if (finger.id == id)
                return &finger;
        return nullptr;
    }
    Rml::Vector2f Middle() const
    {
        return (fingers_[0].at + fingers_[1].at) * 0.5f;
    }

    // Fits the game's view into the viewport the first time it has a size (and again when
    // that size changes): the whole picture is the least zoom, eight times that the most.
    bool Ready(ComponentElement &self, Rml::Element *port)
    {
        const Rml::Vector2f size = BorderBox(port).Size();
        if (size.x < 1.0f || size.y < 1.0f)
            return false;
        if (ready_ && size == port_size_)
            return true;
        const Json base = self.Prop("base"), view = self.Prop("view");
        base_ = {static_cast<float>(Number(base.is_array() && base.size() == 2 ? base[0] : Json(), 1.0)),
                 static_cast<float>(Number(base.is_array() && base.size() == 2 ? base[1] : Json(), 1.0))};
        base_ = {std::max(base_.x, 1.0f), std::max(base_.y, 1.0f)};
        Rml::Vector2f lo = {0.0f, 0.0f}, hi = base_;
        if (view.is_array() && view.size() == 4)
        {
            lo = {static_cast<float>(Number(view[0])), static_cast<float>(Number(view[1]))};
            hi = {static_cast<float>(Number(view[2], base_.x)), static_cast<float>(Number(view[3], base_.y))};
        }
        if (ready_) // the viewport changed size: keep the centre and the zoom relative to the fit
        {
            const Rml::Vector2f centre = (port_size_ * 0.5f - offset_) / scale_;
            lo = centre - port_size_ * 0.5f / scale_;
            hi = centre + port_size_ * 0.5f / scale_;
        }
        min_scale_ = std::min(size.x / base_.x, size.y / base_.y);
        max_scale_ = min_scale_ * 8.0f;
        const Rml::Vector2f span = {std::max(hi.x - lo.x, 1.0f), std::max(hi.y - lo.y, 1.0f)};
        scale_ = std::clamp(std::min(size.x / span.x, size.y / span.y), min_scale_, max_scale_);
        offset_ = size * 0.5f - (lo + hi) * 0.5f * scale_;
        port_size_ = size;
        ready_ = true;
        Apply(self);
        return true;
    }

    // Keeps the picture over the viewport (centred while it is smaller) and lays the plane out.
    void Apply(ComponentElement &self)
    {
        const Rml::Vector2f plane = base_ * scale_;
        const auto fit = [](float offset, float plane_size, float port_size) {
            return plane_size <= port_size ? (port_size - plane_size) * 0.5f : std::clamp(offset, port_size - plane_size, 0.0f);
        };
        offset_ = {fit(offset_.x, plane.x, port_size_.x), fit(offset_.y, plane.y, port_size_.y)};
        Rml::Element *element = FindWith(&self, "data-plane");
        if (!element)
            return;
        // The position by a transform: a pan lays nothing out (left / top would lay out the
        // whole document on every frame of it). Only a new zoom sizes the plane.
        char transform[64];
        std::snprintf(transform, sizeof transform, "translate(%.2fpx, %.2fpx)", offset_.x, offset_.y);
        element->SetProperty("transform", transform);
        if (element->GetProperty<float>("width") != plane.x || element->GetProperty<float>("height") != plane.y)
        {
            element->SetProperty(Rml::PropertyId::Width, Rml::Property(plane.x, Rml::Unit::PX));
            element->SetProperty(Rml::PropertyId::Height, Rml::Property(plane.y, Rml::Unit::PX));
        }
    }

    std::vector<Finger> fingers_;
    VelocityTracker tracker_; // the panning finger
    Fling fling_[2];          // the offset coasting along x / y after a pan
    std::string zoom_; // "in" / "out" while the finger is on a zoom part
    Rml::Rectanglef zoom_box_;
    bool moved_ = false;
    bool ready_ = false;
    int settle_ = 0;
    std::string key_;
    Rml::Vector2f base_ = {1.0f, 1.0f};
    Rml::Vector2f port_size_;
    Rml::Vector2f offset_;
    float scale_ = 1.0f, min_scale_ = 1.0f, max_scale_ = 8.0f;
    float pinch_distance_ = 1.0f, pinch_scale_ = 1.0f;
    Rml::Vector2f pinch_anchor_;
};

template <typename T> std::unique_ptr<Behavior> Make()
{
    return std::make_unique<T>();
}

} // namespace


// Columns sized by their content. The cells must be flex columns that do not stretch
// their children (align-items other than stretch), so each child keeps its natural width.
class ColumnsBehavior final : public Behavior
{
  public:
    void Rebuilt(ComponentElement &self) override
    {
        attempts_ = 4; // layout happens after this call: measure on the following frames
        self.Host().RequestTicks(self, true);
    }

    void Tick(ComponentElement &self, double) override
    {
        if (attempts_ <= 0)
            return;
        Rml::ElementList cells;
        self.QuerySelectorAll(cells, "[data-col]");
        std::unordered_map<int, float> widest;
        bool laid_out = !cells.empty();
        for (Rml::Element *cell : cells)
        {
            float natural = 0.0f;
            for (int i = 0; i < cell->GetNumChildren(); ++i)
                natural = std::max(natural, cell->GetChild(i)->GetBox().GetSize(Rml::BoxArea::Border).x);
            if (cell->GetNumChildren() > 0 && natural <= 0.0f)
                laid_out = false;
            float &width = widest[cell->GetAttribute<int>("data-col", 0)];
            width = std::max(width, natural);
        }
        if (laid_out)
        {
            for (Rml::Element *cell : cells)
            {
                const float width = std::ceil(widest[cell->GetAttribute<int>("data-col", 0)]);
                const Rml::Property *current = cell->GetLocalProperty(Rml::PropertyId::MinWidth);
                if (!current || std::fabs(current->Get<float>() - width) > 0.5f)
                    cell->SetProperty(Rml::PropertyId::MinWidth, Rml::Property(width, Rml::Unit::PX));
            }
            attempts_ = 0;
        }
        else
            --attempts_;
        if (attempts_ <= 0)
            self.Host().RequestTicks(self, false);
    }

  private:
    int attempts_ = 0;
};

void RegisterBuiltinBehaviors()
{
    RegisterBehavior("press", &Make<PressBehavior>);
    RegisterBehavior("range", &Make<RangeBehavior>);
    RegisterBehavior("stick", &Make<StickBehavior>);
    RegisterBehavior("lever", &Make<LeverBehavior>);
    RegisterBehavior("columns", &Make<ColumnsBehavior>);
    RegisterBehavior("hold", &Make<HoldBehavior>);
    RegisterBehavior("swipe", &Make<SwipeBehavior>);
    RegisterBehavior("panzoom", &Make<PanZoomBehavior>);
}

} // namespace storm::ui
