#include "platform/android_worldmap.hpp"
#include <cassert>
#include <iostream>
using namespace storm::android;
struct WMShipCommandList {
    struct Item {std::string sCommandName,sNote;};
    std::vector<Item> m_aUsedCommand;
    std::vector<MapCommand> AndroidMapCommands() const;
    std::string selected;
    bool SelectAndroidMapCommand(const std::string &id);
    bool SelectAndroidItem(const std::string &id){selected=id;return true;}
};
#include "commands.inc"
#include "select.inc"
constexpr int BI_COMMODE_COMMAND_SELECT=1,BI_MSG_COMMAND_ACTIVATE=2;
struct WM_INTERFACE {
    bool m_bVisible=true,active=false;int m_nCommandMode=0,executed=0;
    WMShipCommandList *m_pCommandList=nullptr;
    std::vector<WMShipCommandList::Item> fresh;
    bool IsCommandMenuActive(){return active;}
    void UpdateCommandList(){m_pCommandList->m_aUsedCommand=fresh;}
    void ExecuteCommand(int command){executed=command;}
    bool ActivateAndroidMapCommand(const std::string &id);
};
#include "activate.inc"
WorldMapFrame complete()
{
    WorldMapFrame f;f.available=true;f.owner=19;f.gameplayRevision=4;f.identity="profile:captain:19";f.context="island:Martinique";
    f.instrumentsVisible=f.shipsVisible=f.commandsReady=true;
    auto &i=f.instruments;i.owner=19;i.complete=true;i.visible=true;i.date="18 October 1664";i.hour=12.25;
    i.windForce=.5;i.windAngle=1;i.heading=2;i.morale=50;i.foodDays=17;i.rumDays=20;i.flag=2;i.coordinates="15 N";
    f.ships.push_back({"0:captain:22","Adelina",100,98,40,6});
    f.commands.push_back({"EnterToIsland","Approach island"});return f;
}
int main()
{
    assert(storm::android::mapStormState("0","0") == 0);
    assert(storm::android::mapStormState("1","0") == 1);
    assert(storm::android::mapStormState("1","1") == 2);
    assert(!storm::android::mapStormState(nullptr,"0"));
    assert(!storm::android::mapStormState("1",nullptr));
    assert(!storm::android::mapStormState("bad","0"));
    assert(!storm::android::mapStormState("0","1"));

    WorldMapHud weatherHud;auto weatherFrame=complete();weatherHud.update(weatherFrame,1);
    const auto weatherRevision=weatherHud.revision();
    assert(weatherHud.json().find("\"stormState\":null")!=std::string::npos);
    for(int storm=0;storm<=2;++storm){
        weatherFrame.instruments.stormState=storm;weatherHud.update(weatherFrame,storm+2);
        assert(weatherHud.revision()==weatherRevision); // Weather alone must not revoke held input/commands.
        assert(weatherHud.json().find("\"stormState\":"+std::to_string(storm))!=std::string::npos);
    }
    WMShipCommandList commands;commands.m_aUsedCommand={{"Cancel","Cancel"},{"EnterToIsland","Approach island"}};
    assert(commands.AndroidMapCommands().size()==1&&commands.AndroidMapCommands()[0].id=="EnterToIsland");
    commands.m_aUsedCommand[0].sCommandName="cancel";assert(commands.AndroidMapCommands().size()==1);
    commands.m_aUsedCommand[1].sNote="";assert(commands.AndroidMapCommands().empty());
    WM_INTERFACE interface;interface.m_pCommandList=&commands;
    interface.fresh={{"Cancel","Cancel"},{"EnterToIsland","Approach island"}};
    assert(!interface.ActivateAndroidMapCommand("EnterToSea")&&!interface.executed);
    assert(interface.ActivateAndroidMapCommand("EnterToIsland")&&interface.executed==BI_MSG_COMMAND_ACTIVATE&&commands.selected=="i1");
    interface.active=true;assert(!interface.ActivateAndroidMapCommand("EnterToIsland"));interface.active=false;
    interface.m_bVisible=false;assert(!interface.ActivateAndroidMapCommand("EnterToIsland"));
    WorldMapHud h;auto f=complete();h.update(f,10);h.resetActivity();
    auto request=[&](std::string verb,std::string value){return std::to_string(h.revision())+"\t1\t"+verb+"\t"+value;};
    assert(f.availableMask()==7);assert(!h.covered(7,19));
    assert(h.offer(request("ack","7"),10));assert(h.covered(7,19));assert(!h.covered(7,20));
    const auto old=request("activate","EnterToIsland");
    assert(h.offer(old,12));f.context="island:Guadeloupe";h.update(f,13);
    assert(!h.take(14));assert(!h.offer(old,14));assert(!h.covered(7,19));
    assert(!h.offer(request("activate","EnterToEnemy"),14));
    assert(h.offer(request("activate","EnterToIsland"),14));assert(!h.offer(request("activate","EnterToIsland"),14));
    assert(h.take(15)=="EnterToIsland");assert(!h.take(15));
    auto rev=h.revision();f.instruments.hour=14;h.update(f,20);assert(h.revision()==rev); // Numeric telemetry keeps held controls stable.
    assert(h.offer(request("ack","7"),20));h.update(f,1021);assert(!h.covered(7,19));
    assert(h.offer(request("ack","7"),1021));assert(h.covered(7,19));assert(h.offer(request("ack","0"),1022));assert(!h.covered(1,19));
    f.instruments.foodDays.reset();h.update(f,1030);assert(h.frame().availableMask()==6);
    assert(h.offer(request("ack","7"),1030));assert(!h.covered(1,19)&&h.covered(6,19));
    f.commandsReady=false;h.update(f,1040);assert(!h.offer(request("activate","EnterToIsland"),1040));
    f=complete();h.update(f,1050);assert(h.offer(request("activate","EnterToIsland"),1051));assert(!h.take(2052));
    const auto staleActivity=request("ack","7");h.resetActivity();assert(!h.offer(staleActivity,2053));
    f.identity="other profile";h.update(f,2054);assert(!h.covered(7,19));
    h.update({},2055);assert(!h.offer(staleActivity,2055));assert(!h.take(2055));
    h.update(complete(),UINT32_MAX-500);assert(h.offer(std::to_string(h.revision())+"\t2\tack\t7",UINT32_MAX-500));
    h.update(complete(),10);assert(h.covered(7,19));h.update(complete(),501);assert(!h.covered(7,19));
    auto before=h.revision();auto changed=complete();changed.instruments.flag.reset();h.update(changed,510);assert(h.revision()!=before&&!h.covered(1,19));
    changed.instruments.rumDays.reset();changed.instruments.coordinates.clear();h.update(changed,511);assert(h.frame().availableMask()==7);
    changed.shipsComplete=false;h.update(changed,512);assert(h.frame().availableMask()==5);
    h.update(complete(),513);
    auto json=h.json();assert(json.find("\"foodDays\":17")!=std::string::npos);assert(json.find("Adelina")!=std::string::npos);
    std::cout<<"PASS map leases, command identity, one-shot dispatch, quest lock, coverage expiry/release, activity and tick rollover\n";
}
