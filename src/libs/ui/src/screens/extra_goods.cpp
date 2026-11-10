// The treasurer's purchase list (interface "goodstransfer", mockup V2-Extra-GoodsTransfer): what
// the treasurer buys per good for the selected ship. The engine exports one row per good
// (xinterface mobile_goods_transfer.hpp: ANDROID_GOODS_ORDER with the order and the hold) and the
// selected ship (ANDROID_GOODS_SHIP). Fills <s-goods-order>, the trade list of the commerce
// screens: a tap opens a good's quantity, and the order is saved as soon as the amount settles
// (a standing order, no confirmation): one `order@…:<good>=<amount>` engine action that goes
// through the game's own amount window.
#include "controls.h"

#include <algorithm>
#include <cstdlib>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"ANDROID_GOODS_ORDER", "choice", true},
    {"ANDROID_GOODS_SHIP", "text"},
    {"EXIT_BTN", "button"},
    {"BTN_CHOOSESHIP_LEFT", "button"},
    {"BTN_CHOOSESHIP_RIGHT", "button"},
    {"CHECK_BUYCONTRABAND", "check"},
    {"MAIN_BACK", "image"},
    // The game's amount window: the rows set the order through it, it is never drawn.
    {"TG_*", "text|edit|button|image"},
};

int Context(const Widget &widget, const char *key, int fallback)
{
    const auto found = widget.context.find(key);
    return found == widget.context.end() ? fallback : std::atoi(found->second.c_str());
}

class GoodsOrderScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-goods-order";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const std::vector<const Widget *> goods = admitted.All("ANDROID_GOODS_ORDER");
        const Widget *ship = admitted.One("ANDROID_GOODS_SHIP");
        if (ship && !ship->unavailable_reason.empty() && ship->unavailable_reason != reported_)
        {
            reported_ = ship->unavailable_reason;
            bindings.Error("goods transfer: " + reported_);
        }
        // The open row: ours, else the good whose game window is open.
        std::string open = open_;
        for (const Widget *row : goods)
            if (row->value == "1")
                open = row->row_id;

        Json rows = Json::array();
        for (size_t i = 0; i < goods.size(); ++i)
        {
            const Widget *row = goods[i];
            const int order = Context(*row, "order", 0);
            const bool recent = row->row_id == local_good_ && bindings.Now() - local_at_ < 1.5;
            const int shown = recent ? local_amount_ : order;
            const auto hold = row->context.find("hold");
            Json view{{"id", std::to_string(i)},
                      {"name", row->label},
                      {"image", PictureOf(row->FirstImage())},
                      {"cells", Json::array({hold == row->context.end() ? std::string("—") : hold->second, std::to_string(shown)})},
                      {"selected", row->row_id == open},
                      {"disabled", !row->Offers("order")},
                      {"reason", row->unavailable_reason}};
            if (row->row_id == open)
            {
                // The script takes up to 10000; the slider spans what is useful around the order.
                const int top = std::clamp(((std::max(shown, 500) * 2 + 99) / 100) * 100, 1000, 10000);
                view["q"] = Json{{"value", shown},
                                 {"min", 0},
                                 {"max", top},
                                 {"label", bindings.Text("goods.order")},
                                 {"unit", bindings.Text("goods.pieces")},
                                 {"summary", "-"},
                                 {"all", false},
                                 {"disabled", !row->Offers("order")},
                                 {"reason", row->unavailable_reason}};
            }
            rows.push_back(std::move(view));
        }
        if (recent_until_ > bindings.Now())
            bindings.RebuildAfter(recent_until_ - bindings.Now());

        bindings.On("goods", "row", [this, &bindings, goods](const Event &event) {
            const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
            if (index >= goods.size())
                return;
            open_ = open_ == goods[index]->row_id ? std::string() : goods[index]->row_id;
            bindings.Rebuild();
        });
        const Widget *open_row = nullptr;
        for (const Widget *row : goods)
            if (row->row_id == open)
                open_row = row;
        if (open_row)
        {
            const auto set = [this, &bindings, open_row](const Event &event) {
                const int amount = std::clamp(std::atoi(event.value.c_str()), 0, 10000);
                local_good_ = open_row->row_id;
                local_amount_ = amount;
                local_at_ = bindings.Now();
                recent_until_ = local_at_ + 1.5;
                if (!open_row->Offers("order"))
                    return;
                // A standing order applies by itself: when a drag ends, or shortly after the last step.
                const double wait = event.verb == "commit" ? 0.0 : 0.4;
                bindings.SendAfter(wait, *open_row, "order", open_row->row_id + "=" + std::to_string(amount));
                bindings.Rebuild();
            };
            bindings.On("goods", "change", set);
            bindings.On("goods", "commit", set);
        }

        Json view = Json::object();
        view["title"] = bindings.Text("goods.title");
        view["list"] = Json{{"label", bindings.Text("goods.good")},
                            {"cols", Json::array({Json{{"label", bindings.Text("goods.hold")}}, Json{{"label", bindings.Text("goods.order")}}})},
                            {"rows", rows}};

        // The ship the list is for, with the game's arrows; the script skips ships it may not use.
        const Widget *left = admitted.One("BTN_CHOOSESHIP_LEFT");
        const Widget *right = admitted.One("BTN_CHOOSESHIP_RIGHT");
        view["subject"] = Json();
        if (ship && !ship->label.empty())
        {
            const int index = Context(*ship, "index", 0), count = Context(*ship, "count", 1);
            view["subject"] = Json{{"label", ship->label},
                                   {"sub", count > 1 ? std::to_string(index + 1) + " " + bindings.Text("common.of") + " " + std::to_string(count) : ""},
                                   {"prev", left && left->Offers("press") && index > 0},
                                   {"next", right && right->Offers("press") && index + 1 < count},
                                   {"list", false}};
            const auto choose = [this, &bindings](const Widget *button) {
                open_.clear();
                if (button && button->Offers("press"))
                    bindings.Send(*button, "press");
            };
            bindings.On("header", "subject-prev", [choose, left](const Event &) { choose(left); });
            bindings.On("header", "subject-next", [choose, right](const Event &) { choose(right); });
            bindings.Ignore("header", "subject-list");
        }

        view["contraband"] = Json();
        if (const Widget *check = admitted.One("CHECK_BUYCONTRABAND"))
        {
            view["contraband"] = Json{{"label", check->label},
                                      {"on", check->value == "1"},
                                      {"disabled", !check->Offers("toggle")},
                                      {"reason", check->unavailable_reason}};
            bindings.On("contraband", "change", [&bindings, check](const Event &event) {
                bindings.Send(*check, "toggle", event.value == "1" ? "1" : "0");
            });
        }

        const Widget *exit = admitted.One("EXIT_BTN");
        const Widget *cancel = admitted.One("TG_CANCEL_BUTTON");
        bindings.On("header-back", "", [&bindings, exit, cancel](const Event &) {
            // A game amount window left open (opened by the game itself) closes first.
            if (cancel && cancel->Offers("press"))
                bindings.Send(*cancel, "press");
            else
                bindings.Back(exit);
        });
        return view;
    }

  private:
    std::string open_;     // the good whose quantity the player opened
    std::string reported_; // the missing facts already reported
    // The amount the player set last: shown until the game's own order catches up.
    std::string local_good_;
    int local_amount_ = 0;
    double local_at_ = 0.0, recent_until_ = 0.0;
};

} // namespace

std::unique_ptr<Screen> CreateGoodsOrderScreen()
{
    return std::make_unique<GoodsOrderScreen>();
}

} // namespace storm::ui::screens
