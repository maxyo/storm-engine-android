// Touch behaviours shared by the components. A component lists them in its spec; the
// template marks the parts they act on with data-* attributes:
//
//   press   data-action="verb" [data-value] [data-disabled data-reason] [data-hold]
//           [data-step="±n" | data-set="n" | data-toggle="prop" | data-select="arrayProp"
//            data-index="i"] [data-prop="value"]
//           Without a marked part the whole component is the button (verb "click") unless
//           the spec sets constants.pressRoot to false.
//   range   data-range="prop" on the track (props min, max, step)
//   stick   data-stick="x|xy" on the travel area, data-knob on the knob
//   lever   data-lever="steps|continuous" on the rail (prop value / dir)
//   sticky  data-sticky on rows inside a scrolling element (which must be positioned, so
//           it is their offset parent): a row scrolled above the top edge stays pinned
//           there (CSS position: sticky; top: 0, which RmlUi lacks)
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

#include <RmlUi/Core/EventListener.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

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

Rml::Rectanglef BorderBox(Rml::Element *element)
{
    const Rml::Vector2f origin = element->GetAbsoluteOffset(Rml::BoxArea::Border);
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
        return self.Spec().constants.value("holdMode", std::string("confirm")) == "tooltip" ? Hold::Tooltip : Hold::Confirm;
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

template <typename T> std::unique_ptr<Behavior> Make()
{
    return std::make_unique<T>();
}

} // namespace

class StickyBehavior final : public Behavior, public Rml::EventListener
{
  public:
    ~StickyBehavior() override
    {
        if (Rml::Element *self = self_.get())
            self->RemoveEventListener(Rml::EventId::Scroll, this);
    }

    void Rebuilt(ComponentElement &self) override
    {
        if (!self_)
        {
            // Scroll events bubble: one listener on the component hears its lists.
            self_ = self.GetObserverPtr();
            self.AddEventListener(Rml::EventId::Scroll, this);
        }
        rows_.clear();
        Collect(&self);
        Place();
    }

    void ProcessEvent(Rml::Event &) override
    {
        Place();
    }

  private:
    void Collect(Rml::Element *element)
    {
        for (int i = 0; i < element->GetNumChildren(); ++i)
        {
            Rml::Element *child = element->GetChild(i);
            if (child->HasAttribute("data-sticky"))
            {
                rows_.push_back(child->GetObserverPtr());
                child->SetProperty(Rml::PropertyId::ZIndex, Rml::Property(1, Rml::Unit::NUMBER));
            }
            else if (!dynamic_cast<ComponentElement *>(child))
                Collect(child);
        }
    }

    void Place()
    {
        for (const auto &entry : rows_)
        {
            Rml::Element *row = entry.get();
            if (!row)
                continue;
            // The nearest scrolling ancestor; it must be positioned so that it ends the
            // chain of offset parents (offsets ignore scroll and transforms).
            Rml::Element *scroller = row->GetParentNode();
            for (; scroller; scroller = scroller->GetParentNode())
            {
                const auto overflow = scroller->GetComputedValues().overflow_y();
                if (overflow == Rml::Style::Overflow::Auto || overflow == Rml::Style::Overflow::Scroll)
                    break;
            }
            float top = 0.0f;
            Rml::Element *at = row;
            for (; at && at != scroller; at = at->GetOffsetParent())
                top += at->GetOffsetTop();
            if (!scroller || !at)
                continue;
            const float shift = std::max(0.0f, scroller->GetScrollTop() - top);
            row->SetProperty("transform", "translateY(" + std::to_string(shift) + "px)");
        }
    }

    Rml::ObserverPtr<Rml::Element> self_;
    std::vector<Rml::ObserverPtr<Rml::Element>> rows_;
};

void RegisterBuiltinBehaviors()
{
    RegisterBehavior("press", &Make<PressBehavior>);
    RegisterBehavior("range", &Make<RangeBehavior>);
    RegisterBehavior("stick", &Make<StickBehavior>);
    RegisterBehavior("lever", &Make<LeverBehavior>);
    RegisterBehavior("sticky", &Make<StickyBehavior>);
    RegisterBehavior("hold", &Make<HoldBehavior>);
    RegisterBehavior("swipe", &Make<SwipeBehavior>);
}

} // namespace storm::ui
