#include "mobile_alchemy.hpp"
#include <cassert>
#include <iostream>
using namespace storm::mobile;
int main()
{
    // Two table rows of the shown recipe (bullet + powder), as the generic export gives them.
    std::vector<MobileWidget> widgets{{"TABLE_LIST", "choice", "0\tComponent\tBullet\t1\t0", "1", 0, 1, true},
                                      {"TABLE_LIST", "choice", "28\tComponent\tPowder\t1\t0", "0", 1, 2, true}};
    AlchemySource source;
    source.recipes = {{"cartridge", "Cartridge", "ammo", "ITEMS_18", "itm8", 0, 3}, {"potion1", "Potion", "", "ITEMS_1", "itm2", 1, 1}};
    source.current = "cartridge";
    source.description = "Paper cartridge.";
    source.components = {{"bullet", false, 1, 22, 0}, {"gunpowder", false, 1, 28, 0}};
    CollectAlchemy(source, widgets);
    // Real recipes only, the shown one marked; the other can be chosen.
    assert(widgets.size() == 5 && widgets[2].node == "ANDROID_ALCHEMY_RECIPE" && widgets[2].value == "1" && widgets[2].actions.empty());
    assert(widgets[3].rowId == "potion1" && widgets[3].index == 1 && widgets[3].actions == std::vector<std::string>{"choose"});
    assert(widgets[2].images[0].source.group == "ITEMS_18" && widgets[2].images[0].source.image == "itm8" && widgets[2].context.at("yield") == "3");
    assert(widgets[0].context.at("itemId") == "bullet" && widgets[1].context.at("use") == "component");
    const auto &marker = widgets[4];
    assert(marker.node == "ANDROID_ALCHEMY" && marker.context.at("batches") == "22" && marker.context.at("ready") == "1");
    assert(marker.enabled && marker.actions == std::vector<std::string>{"craft"} && marker.unavailableReason.empty());
    // Pot changes for exactly N batches; a catalyst is needed once; too many is refused.
    std::vector<AlchemyComponent> parts{{"herb", false, 2, 10, 3}, {"retort", true, 1, 0, 1}};
    assert(AlchemyBatches(parts) == 6);
    const auto changes = AlchemyPotChanges(parts, 4);
    assert(changes && (*changes)[0] == 5 && (*changes)[1] == 0);
    const auto back = AlchemyPotChanges({{"herb", false, 2, 10, 9}}, 2);
    assert(back && (*back)[0] == -5); // the pot gives back what is beyond 2 batches
    assert(!AlchemyPotChanges(parts, 7) && !AlchemyPotChanges(parts, 0));
    assert(AlchemyBatches({{"herb", false, 2, 10, 3}, {"retort", true, 1, 0, 0}}) == 0); // no catalyst
    // Busy or missing facts: nothing to press, reasons named.
    source.busy = "scrolling";
    source.missing = {"sCurItem"};
    widgets.clear();
    CollectAlchemy(source, widgets);
    assert(widgets[1].actions.empty() && !widgets.back().enabled && widgets.back().context.at("busy") == "scrolling");
    assert(widgets.back().unavailableReason == "alchemy:missing=sCurItem");
    // Actions bound to this opening only.
    const auto craft = ParseAlchemyAction("craft@7:cartridge=4", "craft", 7, true);
    assert(craft && craft->item == "cartridge" && craft->batches == 4);
    assert(!ParseAlchemyAction("craft@6:cartridge=4", "craft", 7, true) && !ParseAlchemyAction("craft@7:cartridge", "craft", 7, true));
    assert(!ParseAlchemyAction("craft@7:cartridge=-1", "craft", 7, true) && !ParseAlchemyAction("craft@7:=1", "craft", 7, true));
    const auto choose = ParseAlchemyAction("choose@7:potion1", "choose", 7, false);
    assert(choose && choose->item == "potion1" && !ParseAlchemyAction("choose@7:", "choose", 7, false));
    std::cout << MobileWidgetJson("alchemy", widgets, 7) << "\n";
}
