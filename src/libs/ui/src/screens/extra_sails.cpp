// Sail emblem at the shipyard (interface "sailsgerald", mockup V2-Extra-Sails). The engine exports
// the emblems of the scroll image (xinterface mobile_sails_gerald.hpp: SCROLL_GERALD rows with the
// texture path, the current one marked) and the marker ANDROID_SAILS_GERALD (price, the hero's
// money, the emblem on the sails now). Fills <s-sail-emblem>: a tap chooses an emblem (the game's
// own selection), one contextual primary paints the chosen emblem (B_OK) or takes the one on the
// sails off (B_REMOVE); both cost the price, so it is disabled with a toast when money is short.
// ← is the game's exit.
#include "controls.h"

#include <cstdlib>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"SCROLL_GERALD", "choice", true},
    {"ANDROID_SAILS_GERALD", "sails_gerald", true},
    {"EXIT_BTN", "button"},
    {"B_OK", "button"},
    {"B_REMOVE", "button"},
    {"B_CANCEL", "button"},
    {"INFO_CAPTION", "text"},
    // The game's ‹ › move the scroll image; the screen selects the neighbour itself.
    {"SAILS_GERALD_LEFT_BUTTON", "button"},
    {"SAILS_GERALD_RIGHT_BUTTON", "button"},
};

std::string Context(const Widget &widget, const char *key)
{
    const auto found = widget.context.find(key);
    return found == widget.context.end() ? std::string() : found->second;
}

// 1250 → "1 250".
std::string Spaced(const std::string &digits)
{
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i)
    {
        if (i && (digits.size() - i) % 3 == 0)
            out += ' ';
        out += digits[i];
    }
    return out;
}

class SailEmblemScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-sail-emblem";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const std::vector<const Widget *> emblems = admitted.All("SCROLL_GERALD");
        const Widget &marker = *admitted.One("ANDROID_SAILS_GERALD");
        if (!marker.unavailable_reason.empty() && marker.unavailable_reason != reported_)
        {
            reported_ = marker.unavailable_reason;
            bindings.Error("sail emblem: " + reported_);
        }
        const std::string applied = Context(marker, "applied");
        const std::string price = Context(marker, "price"), money = Context(marker, "money");
        size_t current = 0;
        for (size_t i = 0; i < emblems.size(); ++i)
            if (emblems[i]->value == "1")
                current = i;

        Json items = Json::array();
        for (const Widget *emblem : emblems)
        {
            items.push_back(Json{{"texture", Context(*emblem, "texture")},
                                 {"label", emblem->label},
                                 {"selected", emblem->value == "1"},
                                 {"applied", !applied.empty() && emblem->row_id == applied},
                                 {"disabled", !emblem->Offers("select")}});
        }
        const auto choose = [&bindings, emblems](size_t index) {
            if (index < emblems.size() && emblems[index]->Offers("select") && emblems[index]->value != "1")
                bindings.Send(*emblems[index], "select", emblems[index]->row_id);
        };
        bindings.On("emblems", "emblem", [choose](const Event &event) { choose(static_cast<size_t>(std::atoi(event.value.c_str()))); });
        bindings.On("emblem-prev", "", [choose, current](const Event &) { choose(current - 1); });
        bindings.On("emblem-next", "", [choose, current](const Event &) { choose(current + 1); });

        Json view = Json::object();
        const std::string caption = admitted.Label("INFO_CAPTION");
        view["title"] = caption.empty() ? bindings.Text("sails.title") : caption;
        view["emblems"] = items;
        view["appliedLabel"] = bindings.Text("sails.applied");
        view["prev"] = current > 0;
        view["next"] = current + 1 < emblems.size();
        view["stats"] = money.empty() ? Json::array() : Json::array({Json{{"icon", "money"}, {"value", Spaced(money)}, {"tone", "gold"}}});

        // One primary: the chosen emblem is the one on the sails → take it off, else paint it.
        const bool on_sails = !emblems.empty() && !applied.empty() && emblems[current]->row_id == applied;
        const Widget *button = admitted.One(on_sails ? "B_REMOVE" : "B_OK");
        view["primary"] = Json();
        if (button)
        {
            const bool short_of_money = !price.empty() && !money.empty() && std::atoll(money.c_str()) < std::atoll(price.c_str());
            const std::string reason = short_of_money
                                           ? bindings.Text("sails.short") + " " + Spaced(std::to_string(std::atoll(price.c_str()) - std::atoll(money.c_str())))
                                           : button->unavailable_reason;
            view["primary"] = Json{{"label", bindings.Text(on_sails ? "sails.remove" : "sails.apply")},
                                   {"sub", price.empty() ? std::string() : Spaced(price)},
                                   {"disabled", short_of_money || !button->Offers("press")},
                                   {"reason", reason},
                                   {"busy", bindings.Pending("primary")}};
            bindings.On("primary", "", [&bindings, button, short_of_money](const Event &) {
                if (!short_of_money && button->Offers("press"))
                    bindings.Send(*button, "press");
            });
        }

        const Widget *exit = admitted.One("EXIT_BTN") ? admitted.One("EXIT_BTN") : admitted.One("B_CANCEL");
        bindings.On("header-back", "", [&bindings, exit](const Event &) { bindings.Back(exit); });
        return view;
    }

  private:
    std::string reported_;
};

} // namespace

std::unique_ptr<Screen> CreateSailEmblemScreen()
{
    return std::make_unique<SailEmblemScreen>();
}

} // namespace storm::ui::screens
