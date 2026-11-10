#include "mobile_sails_gerald.hpp"
#include <cassert>
#include <iostream>
using namespace storm::mobile;
int main()
{
    assert(SailEmblemTexture("Gerald_01.tga.tx") == "ships/gerald/Gerald_01");
    const std::vector<std::string> files{"Gerald_01.tga.tx", "Gerald_02.tga.tx"};
    std::vector<MobileWidget> widgets;
    CollectSailEmblems("SCROLL_GERALD", files, 1, true, 5, widgets);
    assert(widgets.size() == 2);
    assert(widgets[0].type == "choice" && widgets[0].label == "Gerald_01" && widgets[0].value == "0" && widgets[0].rowId == files[0]);
    assert(widgets[1].value == "1" && widgets[1].context.at("texture") == "ships/gerald/Gerald_02");
    assert(widgets[1].actions == std::vector<std::string>{"select"});
    // Selection only for this opening and the file the row still shows.
    assert(AcceptSailEmblem("select@4:Gerald_02.tga.tx", 4, files, 1) == std::optional<int>(1));
    assert(!AcceptSailEmblem("select@3:Gerald_02.tga.tx", 4, files, 1));
    assert(!AcceptSailEmblem("select@4:Gerald_01.tga.tx", 4, files, 1) && !AcceptSailEmblem("select@4:x", 4, files, 7));
    widgets.clear();
    CollectSailEmblems("SCROLL_GERALD", files, 0, false, 5, widgets);
    assert(!widgets[0].enabled && widgets[0].actions.empty());
    auto marker = SailsGeraldMarker(1250, std::string("12480"), std::string(""));
    assert(marker.context.at("price") == "1250" && marker.context.at("applied").empty() && marker.unavailableReason.empty());
    marker = SailsGeraldMarker(std::nullopt, std::string("1"), std::nullopt);
    assert(marker.unavailableReason == "sails-gerald:missing=price,applied");
    widgets.push_back(marker);
    std::cout << MobileWidgetJson("sailsgerald", widgets, 4, 1) << "\n";
}
