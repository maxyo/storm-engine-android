// The treasurer's purchase list (interface "goodstransfer", mockup V2-Extra-GoodsTransfer): what
// the treasurer buys per good for the selected ship. The engine exports one row per good
// (xinterface mobile_goods_transfer.hpp: ANDROID_GOODS_ORDER with the order and the hold) and the
// selected ship (ANDROID_GOODS_SHIP). Fills <s-goods-order>, the trade list of the commerce
// screens: a tap opens a good's quantity; the amount set is a draft (AmountDraft, controls.h) that
// «Сохранить N» saves as one `order@…:<good>=<amount>` engine action (through the game's own amount
// window) and «Отмена» returns to the saved order.
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

        // The open good's draft: the saved order is its origin; the hold has no other side.
        const Widget *open_row = nullptr;
        for (const Widget *row : goods)
            if (row->row_id == open)
                open_row = row;
        if (open_row)
            draft_.Follow(open_row->row_id, Context(*open_row, "order", 0), 0, bindings);
        else
            draft_.Close();

        Json rows = Json::array();
        for (size_t i = 0; i < goods.size(); ++i)
        {
            const Widget *row = goods[i];
            const int shown = row == open_row ? draft_.Value(0, kMost) : Context(*row, "order", 0);
            const auto hold = row->context.find("hold");
            Json view{{"id", std::to_string(i)},
                      {"name", row->label},
                      {"image", PictureOf(row->FirstImage())},
                      {"cells", Json::array({hold == row->context.end() ? std::string("—") : hold->second, std::to_string(shown)})},
                      {"selected", row == open_row},
                      {"disabled", !row->Offers("order")},
                      {"reason", row->unavailable_reason}};
            if (row == open_row)
            {
                // The script takes up to 10000; the slider spans what is useful around the order.
                const int top = std::clamp(((std::max(shown, 500) * 2 + 99) / 100) * 100, 1000, kMost);
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

        bindings.On("goods", "row", [this, &bindings, goods](const Event &event) {
            const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
            if (index >= goods.size())
                return;
            open_ = open_ == goods[index]->row_id ? std::string() : goods[index]->row_id;
            bindings.Rebuild();
        });
        Json actions = Json::array();
        Json primary;
        if (open_row)
        {
            draft_.Bind(bindings, "goods", 0, kMost, std::string());
            primary = draft_.Buttons(bindings, 0, kMost, {StringKey("goods.save"), StringKey("goods.save"), true}, std::string(),
                                     !open_row->Offers("order"), open_row->unavailable_reason,
                                     [&bindings, open_row, origin = draft_.Origin()](int delta) {
                                         bindings.Send(*open_row, "order", open_row->row_id + "=" + std::to_string(origin + delta));
                                     });
            // A standing order is a setting, not a transfer: it keeps its «Отмена» (owner, 2026-11-14).
            draft_.CancelAction(bindings, actions);
        }

        Json view = Json::object();
        view["title"] = bindings.Text("goods.title");
        view["primary"] = std::move(primary);
        view["actions"] = std::move(actions);
        view["list"] = Json{{"label", bindings.Text("goods.good")},
                            {"cols", Json::array({Json{{"label", bindings.Text("goods.hold")}, {"icon", "capacity"}}, Json{{"label", bindings.Text("goods.order")}, {"icon", "scroll"}}})},
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
        bindings.On("header-back", "", [this, &bindings, exit, cancel](const Event &) {
            // A game amount window left open (opened by the game itself) closes first; then a changed order is dropped.
            if (cancel && cancel->Offers("press"))
                bindings.Send(*cancel, "press");
            else if (draft_.Drop())
                bindings.Rebuild();
            else
                bindings.Back(exit);
        });
        return view;
    }

  private:
    std::string open_;     // the good whose quantity the player opened
    std::string reported_; // the missing facts already reported
    static constexpr int kMost = 10000; // the most the script takes
    AmountDraft draft_;    // the open good's order as the player sets it, until saved
};

} // namespace

std::unique_ptr<Screen> CreateGoodsOrderScreen()
{
    return std::make_unique<GoodsOrderScreen>();
}

} // namespace storm::ui::screens
