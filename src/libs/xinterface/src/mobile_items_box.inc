// Included inside xinterface.cpp's Android implementation: «Взять всё» of a chest or a barrel
// (PROGRAM/interface/itemsbox.c). The game has its take-all (onGetAllBtnClick: every item it
// can carry, capped by the hero's weight, then one table refresh) but hides its button
// (GETALL_BUTTON) there; the touch UI asks for it through ANDROID_TAKE_ALL, and the engine runs
// the game's own routine through the button's command, in one script call. No rules here.
namespace {
// The chest or barrel kind of the screen (itemsbox.c sGetInterfaceType: sFaceID "" / "Barrel").
bool ItemsBoxIsChest(){
    auto *face=static_cast<VDATA*>(core.GetScriptVariable("sFaceID"));const char *id=nullptr;
    return face&&face->Get(id)&&id&&(!*id||storm::iEquals(id,"Barrel"));
}
}

void XINTERFACE::MobileCollectItemsBox(std::vector<MobileWidget> &widgets)
{
    auto *button=FindNode("GETALL_BUTTON",nullptr);
    if(!button||button->m_bUse||!ItemsBoxIsChest())return; // shown by the game itself, or not a chest
    MobileWidget take{"ANDROID_TAKE_ALL","button","","",-1,button->m_rect.top*8192+button->m_rect.left,true};
    take.actions.push_back("press");
    widgets.push_back(std::move(take));
}

bool XINTERFACE::MobileApplyItemsBoxTakeAll(const std::string &value)
{
    std::vector<MobileWidget> live;MobileCollectWidgets(live);
    const bool offered=value=="press"&&std::any_of(live.begin(),live.end(),[](const auto &w){return w.node=="ANDROID_TAKE_ALL"&&w.enabled;});
    if(!offered)return false;
    core.Event("ievnt_command","ss","click","GETALL_BUTTON"); // itemsbox.c ProcCommand → onGetAllBtnClick
    return true;
}
