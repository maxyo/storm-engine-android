#include "mobile_table_game.hpp"
#include <cassert>
#include <iostream>
using namespace storm::mobile;
static TableGameImage Image(const char *node, const char *group, const char *picture, int x, int y)
{
    TableGameImage image{node, x, y};
    image.image.source = {"atlas", group, picture, {}, {}, -1};
    return image;
}
int main()
{
    TableGameSource source;
    source.images = {Image("HeroDice1", "DICE", "dice_3_1", 60, 486), Image("HeroDice4", "", "", 0, 0),
                     Image("PCard18", "CARDS", "blank", 400, 36)};
    source.strings = {{"Money", "7 600"}, {"Beta_TXT", "debug"}};
    source.facts = {{"rate", "200"}, {"open", "0"}, {"opponent", std::nullopt}, {"heroDice", "3,3,3,6,1"}};
    std::vector<MobileWidget> widgets;
    CollectTableGame(source, widgets);
    // The removed picture and the beta line are not exported; the marker comes last.
    assert(widgets.size() == 4);
    assert(widgets[0].node == "HeroDice1" && widgets[0].type == "image" && widgets[0].label == "dice_3_1");
    assert(widgets[0].images.size() == 1 && widgets[0].images[0].source.group == "DICE");
    assert(widgets[0].order == 486 * 8192 + 60);
    assert(widgets[1].node == "PCard18" && widgets[1].label == "blank");
    assert(widgets[2].node == "Money" && widgets[2].type == "text" && widgets[2].label == "7 600");
    const auto &marker = widgets[3];
    assert(marker.node == "ANDROID_TABLE_GAME" && marker.type == "table_game" && !marker.enabled);
    assert(marker.context.at("rate") == "200" && marker.context.at("heroDice") == "3,3,3,6,1");
    assert(marker.context.count("opponent") == 0);
    assert(marker.unavailableReason == "table-game:missing=opponent");
    // A complete source has no reason.
    source.facts = {{"rate", "200"}};
    widgets.clear();
    CollectTableGame(source, widgets);
    assert(widgets.back().unavailableReason.empty());
    std::cout << MobileWidgetJson("boal_dice", widgets, 3, 1) << "\n";
}
