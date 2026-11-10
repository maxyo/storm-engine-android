// Which implementation draws which game interface. This table is the migration switch:
// porting a zone = adding its screens to kPorted and removing their ids from kHostOwned.
#include "screen.h"

namespace storm::ui::screens
{

std::unique_ptr<Screen> CreateStoreScreen();
std::unique_ptr<Screen> CreateDialogueScreen();
std::unique_ptr<StateConsumer> CreateDialogueConsumer();

namespace
{

const ScreenEntry kPorted[] = {
    {"store", CreateStoreScreen},
    {"dialogue", CreateDialogueScreen},
};

// Interfaces the platform host still draws with its own forms. Anything not listed here
// and not ported above gets the generic layout.
const char *const kHostOwned[] = {
    "mainmenu",       "option_screen", "game_menu", "save_load", "selectcharacter",           // menus, saves, settings
    "character",      "lscchar_all",   "items",     "ship",      "questbook", "nationrelation", // hero sections
    "hirecrew",       "shipyard",                                                              // trade zone
    "map",            "dirsailenc",    "leavebattle", "salary",  "about", "boal_frame", "quick_save", // reading menus
};

} // namespace

std::span<const ScreenEntry> PortedScreens()
{
    return kPorted;
}

bool HostOwned(std::string_view screen)
{
    for (const char *id : kHostOwned)
        if (screen == id)
            return true;
    return false;
}

std::vector<std::unique_ptr<StateConsumer>> CreateStateConsumers()
{
    std::vector<std::unique_ptr<StateConsumer>> consumers;
    consumers.push_back(CreateDialogueConsumer());
    return consumers;
}

} // namespace storm::ui::screens
