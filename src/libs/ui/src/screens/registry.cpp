// Which implementation draws which game interface: a ported screen by interface id, the
// generic layout for every other one.
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
std::unique_ptr<Screen> CreateDiceScreen();
std::unique_ptr<Screen> CreateCardsScreen();
std::unique_ptr<Screen> CreateRepairScreen();
std::unique_ptr<Screen> CreateTransferScreen();
std::unique_ptr<Screen> CreateAlchemyScreen();
std::unique_ptr<Screen> CreateGoodsOrderScreen();
std::unique_ptr<Screen> CreateSailEmblemScreen();
std::unique_ptr<Screen> CreateGameOverScreen();
std::unique_ptr<StateConsumer> CreateGameOverConsumer();
std::unique_ptr<StateConsumer> CreateLoadFailureConsumer();
std::unique_ptr<Screen> CreateNationsScreen();
std::unique_ptr<Screen> CreateHeroScreen();
std::unique_ptr<Screen> CreateItemsScreen();
std::unique_ptr<Screen> CreateJournalScreen();
std::unique_ptr<Screen> CreateShipScreen();
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
std::unique_ptr<Screen> CreateMapScreen();
std::unique_ptr<Screen> CreateLauncherScreen();
std::unique_ptr<Screen> CreateDownloadsScreen();
std::unique_ptr<Screen> CreateAboutScreen();
std::unique_ptr<Screen> CreateQuickSaveScreen();
std::unique_ptr<StateConsumer> CreateLauncherConsumer();

namespace
{

const ScreenEntry kPorted[] = {
    {"goodstransfer", CreateGoodsOrderScreen},
    {"sailsgerald", CreateSailEmblemScreen},
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
    {"boal_dice", CreateDiceScreen},
    {"boal_cards", CreateCardsScreen},
    {"repair", CreateRepairScreen},
    {"transfer_main", CreateTransferScreen},
    {"alchemy", CreateAlchemyScreen},
    {"game_over", CreateGameOverScreen}, // state channel gameOver, not a game interface
    {"quick_save", CreateQuickSaveScreen},
    {"nationrelation", CreateNationsScreen},
    {"lscchar_all", CreateHeroScreen},
    {"character", CreateHeroScreen},
    {"items", CreateItemsScreen},
    {"questbook", CreateJournalScreen},
    {"ship", CreateShipScreen},
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
    {"mapview", CreateMapScreen},
    {"mapbest", CreateMapScreen},
    {"papermap", CreateMapScreen},
    // The application's own screens, routed from the platform host's "launcher" channel; the
    // "app:" prefix keeps them apart from game interfaces (ids are ini stems, e.g. "about").
    {"app:launcher", CreateLauncherScreen},
    {"app:downloads", CreateDownloadsScreen},
    {"app:about", CreateAboutScreen},
};

// TEHO interfaces with no screen of their own, on purpose (the generic layout draws them if
// they ever open): passengers and selectnation are commented out of BaseInterface.c and have
// no files; disease_alert has no disease_alert.c / .ini in the game, so it never loads;
// ransack_main loads transfer_main.ini (id "transfer_main", ported); pshero opens only from
// the beta-test debug key (Debuger.c, MOD_BETTATESTMODE).

} // namespace

std::span<const ScreenEntry> PortedScreens()
{
    return kPorted;
}

std::vector<std::unique_ptr<StateConsumer>> CreateStateConsumers()
{
    std::vector<std::unique_ptr<StateConsumer>> consumers;
    consumers.push_back(CreateDialogueConsumer());
    consumers.push_back(CreateAppSettingsConsumer());
    consumers.push_back(CreateGameOverConsumer());
    consumers.push_back(CreateLoadFailureConsumer());
    consumers.push_back(CreateLauncherConsumer());
    hud::AddStateConsumers(consumers); // gameplay, combat, vitals, worldInfo, contextAction, landCommands, ship, seaOrders, seaQuickAction, presentation, spyglass
    return consumers;
}

} // namespace storm::ui::screens
