#include "mobile_goods_transfer.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace storm::mobile;
int main()
{
    std::vector<MobileWidget> widgets{{"EXIT_BTN", "button", "EXIT_BTN", "", -1, 1, true},
                                      {"GOODS_NAMES", "text", "Ball", "", 0, 2, true}};
    GoodsTransferSource source;
    source.goods = {{"Balls", "Ball", 600, 400}, {"Food", "Food", 0, std::nullopt}};
    source.listActive = true;
    source.ship = "Lugger 'Swift'";
    source.shipIndex = 0;
    source.shipCount = 2;
    CollectGoodsTransfer(source, widgets);
    // The collection's texts are replaced by the rows and the ship line.
    assert(widgets.size() == 4 && widgets[0].node == "EXIT_BTN");
    const auto &balls = widgets[1];
    assert(balls.node == "ANDROID_GOODS_ORDER" && balls.type == "choice" && balls.rowId == "Balls" && balls.index == 0);
    assert(balls.value == "0" && balls.enabled && balls.actions == std::vector<std::string>{"open"});
    assert(balls.images.size() == 1 && balls.images[0].source.group == "GOODS" && balls.images[0].source.image == "Balls");
    assert(balls.context.at("order") == "600" && balls.context.at("hold") == "400");
    assert(widgets[2].context.count("hold") == 0);
    assert(widgets[3].node == "ANDROID_GOODS_SHIP" && widgets[3].context.at("count") == "2" && widgets[3].unavailableReason.empty());
    // An open amount window: its row is marked, the list takes no input, a missing fact is named.
    source.open = "Food";
    source.listActive = false;
    source.missing = {"companion"};
    widgets.clear();
    CollectGoodsTransfer(source, widgets);
    assert(widgets[0].value == "0" && widgets[1].value == "1" && !widgets[1].enabled && widgets[1].actions.empty());
    assert(widgets[2].unavailableReason == "goods-transfer:missing=companion");
    // Actions bound to this opening only.
    assert(GoodsTransferOpenTarget("open@7:Balls", 7) == std::optional<std::string>("Balls"));
    assert(!GoodsTransferOpenTarget("open@6:Balls", 7) && !GoodsTransferOpenTarget("open@7:", 7));
    // The click point lands in the script's box of that good and of no neighbour (82 apart
    // vertically, 123 horizontally), also scrolled, scaled and on a wide screen.
    for (const GoodsIconPicture picture : {GoodsIconPicture{}, GoodsIconPicture{40, 25, 0.8f, 0.8f, GoodsWindowOffsetX(2400, 1080)}})
        for (const float gx : {90.0f, 213.0f})
            for (const float gy : {95.0f, 177.0f})
            {
                const auto p = GoodsIconClick(gx, gy, picture);
                assert(GoodsIconHit(p.x, p.y, gx, gy, picture));
                assert(!GoodsIconHit(p.x, p.y, gx, gy + 82, picture) && !GoodsIconHit(p.x, p.y, gx + 123, gy, picture));
            }
    assert(GoodsWindowOffsetX(800, 600) == 0 && std::fabs(GoodsWindowOffsetX(2400, 1080) - 266.6667f) < 0.01f);
    std::cout << MobileWidgetJson("goodstransfer", widgets, 7, 1) << "\n";
}
