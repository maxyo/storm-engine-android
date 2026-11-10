#pragma once
#include <string_view>

namespace storm::android
{
// Which game HUDs the portable UI presents, as it declared once it was mounted
// ("hudowner:sea", "hudowner:worldmap"). Game-thread owned; kept across Activities
// because the UI library, not the Activity, decides it.
//
// One coverage model for every native presentation the touch HUD replaces (land
// command sheet, sea navigator, quick action, ship indicators, world map
// instruments): the UI answers each owner revision once, as an event, when its
// frame is drawn: covering or not covering. Nothing is a lease renewed per frame,
// so the contract does not depend on how fast the UI renders. A revision the UI
// has not answered yet is pending: when the UI presents that HUD, native drawing
// stays off until the answer (no native flash after a load or an owner change,
// however slow the UI's first frame is); otherwise native drawing is unaffected.
struct HudPresenters
{
    bool sea = false, worldMap = false;
    bool declare(std::string_view hud)
    {
        if (hud == "sea") return sea = true;
        if (hud == "worldmap") return worldMap = true;
        return false;
    }
};
inline HudPresenters hudPresenters;
} // namespace storm::android
