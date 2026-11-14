// Rest (interface "tavernwait", mockup V2-Extra-TavernWait): the game's hour slider becomes a
// segment of the usual spans (1, 3, 6, 12 hours, a day) in a wood dialog over the paused
// scene, «Отдыхать» is the game's OK, the close cross its Cancel. Fills <s-rest>.
//
// The game reads the slider as a share of a day and truncates (hours = int(share * 24)), so
// each span is sent as the smallest slider position that still gives that many hours.
#include "controls.h"

#include <cmath>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"HOURS_SLIDE", "slider", true},
    {"OK_BTN", "button", true},
    {"CANCEL_BTN", "button"},
    {"EXIT_BTN", "button"},
    {"INFO_TEXT", "text"},
};

struct Span
{
    int hours;
    const char *key;
};
constexpr Span kSpans[] = {{1, StringKey("rest.h1")},
                           {3, StringKey("rest.h3")},
                           {6, StringKey("rest.h6")},
                           {12, StringKey("rest.h12")},
                           {24, StringKey("rest.day")}};

class RestScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-rest";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const Widget *slider = admitted.One("HOURS_SLIDE");
        const Widget *ok = admitted.One("OK_BTN");
        const Widget *cancel = admitted.One("CANCEL_BTN") ? admitted.One("CANCEL_BTN") : admitted.One("EXIT_BTN");
        const int range = std::max(1, slider->max - slider->min);
        const int position = std::atoi(slider->value.c_str()) - slider->min;
        const int hours = position * 24 / range;

        Json items = Json::array();
        bool preset = false;
        for (const Span &span : kSpans)
        {
            const bool selected = span.hours == hours;
            preset |= selected;
            items.push_back(Json{{"id", std::to_string(span.hours)}, {"label", bindings.Text(span.key)}, {"selected", selected}});
        }
        Json view = Json::object();
        view["title"] = bindings.Text("rest.title");
        view["hours"] = items;
        bindings.On("hours", "select", [&bindings, slider, range](const Event &event) {
            const int wanted = std::atoi(event.value.c_str());
            const int at = static_cast<int>(std::ceil(wanted * range / 24.0)) + slider->min;
            bindings.Send(*slider, "set", std::to_string(std::min(at, slider->max)));
        });

        Json primary = ButtonView(bindings, *ok, "go", "primary");
        primary["label"] = bindings.Text("rest.go");
        // A span the segment does not offer (set by the game) is named by the game's own text.
        if (!preset)
            primary["sub"] = admitted.Label("INFO_TEXT");
        view["actions"] = Json::array({primary});

        const auto close = [&bindings, cancel](const Event &) { bindings.Back(cancel); };
        bindings.On("rest-close", "", close);
        bindings.Modal("rest", close);
        return view;
    }
};

} // namespace

std::unique_ptr<Screen> CreateRestScreen()
{
    return std::make_unique<RestScreen>();
}

} // namespace storm::ui::screens
