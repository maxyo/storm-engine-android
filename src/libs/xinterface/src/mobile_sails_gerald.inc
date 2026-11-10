// Included inside xinterface.cpp's Android implementation: the price, money and applied emblem
// of the sail emblem screen (PROGRAM/interface/SailsGerald.c) for mobile_sails_gerald.hpp.
// The emblem rows themselves come from SCROLL_GERALD (CXI_SCROLLIMAGE::MobileCollect).
void XINTERFACE::MobileCollectSailsGerald(std::vector<MobileWidget> &widgets)
{
    auto *priceVar=static_cast<VDATA*>(core.GetScriptVariable("price"));int32_t value;
    const std::optional<int> price=priceVar&&priceVar->Get(value)?std::optional<int>(value):std::nullopt;
    auto *heroVar=static_cast<VDATA*>(core.GetScriptVariable("pchar"));
    auto *hero=heroVar?heroVar->GetAClass():nullptr;
    const char *money=hero?hero->GetAttribute("Money"):nullptr;
    auto *shipVar=static_cast<VDATA*>(core.GetScriptVariable("shref"));
    auto *ship=shipVar?shipVar->GetAClass():nullptr;
    std::optional<std::string> applied;
    if(ship){auto *a=ship->FindAClass(ship,"ShipSails.Gerald_Name");const char *s=a?a->GetThisAttr():nullptr;applied=std::string(s?s:"");}
    widgets.push_back(storm::mobile::SailsGeraldMarker(price,money?std::optional<std::string>(money):std::nullopt,applied));
}
