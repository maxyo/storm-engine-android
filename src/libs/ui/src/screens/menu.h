// Shared state of the menu screens (menu_*.cpp).
#pragma once

#include <string>

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

// «Продолжить» on the title screen: the saves screen loads the newest record as it opens.
inline bool &ContinueFlag()
{
    static bool requested = false;
    return requested;
}
inline bool ContinueRequested()
{
    return ContinueFlag();
}
inline void SetContinueRequested(bool requested)
{
    ContinueFlag() = requested;
}

// The death screen's choice (menu_game_over.cpp): the game goes to its title screen after a
// defeat; there the title screen opens what the player chose ("Load", "Options"; empty = the
// title itself) once, as if tapped.
inline std::string &TitleIntent()
{
    static std::string intent;
    return intent;
}

} // namespace storm::ui::screens
