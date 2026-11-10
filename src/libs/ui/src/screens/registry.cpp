// Which implementation draws which game interface. This table is the migration switch:
// porting a zone = adding its screens to kPorted and removing their ids from kHostOwned.
#include "screen.h"

#include "../hud/hud.h"

namespace storm::ui::screens
{

std::unique_ptr<Screen> CreateStoreScreen();
std::unique_ptr<Screen> CreateStorageScreen();
std::unique_ptr<Screen> CreateContrabandScreen();
std::unique_ptr<Screen> CreateCannonsScreen();
std::unique_ptr<Screen> CreateItemsTradeScreen();
std::unique_ptr<Screen> CreateItemsBoxScreen();
std::unique_ptr<Screen> CreateCrewScreen();
std::unique_ptr<Screen> CreateShipyardScreen();
std::unique_ptr<Screen> CreateDialogueScreen();
std::unique_ptr<StateConsumer> CreateDialogueConsumer();
std::unique_ptr<Screen> CreateGameMenuScreen();
std::unique_ptr<Screen> CreateSavesScreen();
std::unique_ptr<Screen> CreateSettingsScreen();
std::unique_ptr<StateConsumer> CreateAppSettingsConsumer();
std::unique_ptr<Screen> CreateTitleScreen();
std::unique_ptr<Screen> CreateNewGameScreen();
std::unique_ptr<Screen> CreateMapEncounterScreen();
std::unique_ptr<Screen> CreateDirSailScreen();
std::unique_ptr<Screen> CreateLeaveBattleScreen();
std::unique_ptr<Screen> CreateSalaryScreen();
std::unique_ptr<Screen> CreateFrameScreen();
std::unique_ptr<Screen> CreateColonyNewsScreen();
std::unique_ptr<Screen> CreateNationNewsScreen();
std::unique_ptr<Screen> CreateRestScreen();
std::unique_ptr<Screen> CreateTradeBookScreen();
std::unique_ptr<Screen> CreateColonyCaptureScreen();
std::unique_ptr<Screen> CreateFortCaptureScreen();

namespace
{

const ScreenEntry kPorted[] = {
    {"store", CreateStoreScreen},
    {"storage", CreateStorageScreen},
    {"contraband", CreateContrabandScreen},
    {"cannons", CreateCannonsScreen},
    {"itemstrade", CreateItemsTradeScreen},
    {"itemsbox", CreateItemsBoxScreen},
    {"hirecrew", CreateCrewScreen},
    {"shipyard", CreateShipyardScreen},
    {"dialogue", CreateDialogueScreen},
    {"game_menu", CreateGameMenuScreen},
    {"save_load", CreateSavesScreen},
    {"option_screen", CreateSettingsScreen},
    {"mainmenu", CreateTitleScreen},
    {"selectcharacter", CreateNewGameScreen},
    {"map", CreateMapEncounterScreen},
    {"dirsailenc", CreateDirSailScreen},
    {"leavebattle", CreateLeaveBattleScreen},
    {"salary", CreateSalaryScreen},
    {"boal_frame", CreateFrameScreen},
    {"colonyinfo", CreateColonyNewsScreen},
    {"nationlegend", CreateNationNewsScreen},
    {"tavernwait", CreateRestScreen},
    {"tradebook", CreateTradeBookScreen},
    {"colonycapture", CreateColonyCaptureScreen},
    {"fortcapture", CreateFortCaptureScreen},
};

// Interfaces the platform host still draws with its own forms. Anything not listed here
// and not ported above gets the generic layout.
const char *const kHostOwned[] = {
    "character", "lscchar_all", "items", "ship", "questbook", "nationrelation", // hero sections
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
    consumers.push_back(CreateAppSettingsConsumer());
    hud::AddStateConsumers(consumers); // gameplay, combat, vitals, worldInfo, contextAction, landCommands, presentation
    return consumers;
}

} // namespace storm::ui::screens
