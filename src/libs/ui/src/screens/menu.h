// Shared state of the menu screens (menu_*.cpp).
#pragma once

namespace storm::ui::screens
{

// True while the menus run from the title screen (no game in progress): the title screen
// sets it, the pause menu clears it. Loading a record then needs no question (owner
// decision: no load confirmation from the title screen).
inline bool &FromTitleFlag()
{
    static bool from_title = false;
    return from_title;
}
inline bool FromTitle()
{
    return FromTitleFlag();
}
inline void SetFromTitle(bool from_title)
{
    FromTitleFlag() = from_title;
}

} // namespace storm::ui::screens
