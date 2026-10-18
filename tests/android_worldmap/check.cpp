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

void modalChecks()
{
    const auto packet=[](WorldMapHud &h,const std::string &verb,const std::string &value){return std::to_string(h.revision())+"\t1\t"+verb+"\t"+value;};
    const auto setup=[&](WorldMapHud &h,uint32_t tick){h.update(complete(),tick);h.resetActivity();assert(h.offer(packet(h,"ack","7:1"),tick+1));};
    const auto draw=[](WorldMapHud &h,uint32_t tick,uint32_t mask=7,bool success=true){h.beginDraw(tick);assert(h.covered(mask,19,tick+1));h.presented(success);h.finishDraw(tick+2);};
    const auto operation=[](WorldMapHud &h,uint64_t token,uint64_t proof,int op,uint32_t now){auto o=h.owner();return h.modal.operate(o.revision,o.activity,o.gameplay,token,proof,op,now);};
    WorldMapHud h;setup(h,100);
    assert(h.offer(packet(h,"activate","EnterToIsland"),102));
    assert(h.offer(packet(h,"prepare","17:7:1"),103));
    assert(!h.take(103));assert(!h.offer(packet(h,"activate","EnterToIsland"),104));
    assert(!h.modal.receipt(104).token); // ACK is not a completed native present
    h.beginDraw(105);assert(h.covered(7,19,106));h.finishDraw(107);assert(!h.modal.receipt(107).token);
    draw(h,108,7,false);assert(!h.modal.receipt(110).token); // failed Present
    draw(h,111,1);assert(!h.modal.receipt(113).token); // partial native suppression
    h.beginDraw(114);assert(h.covered(7,19,115));h.presented(true);h.presented(false);h.finishDraw(116);assert(!h.modal.receipt(116).token); // later progress present invalidates
    draw(h,117);assert(h.modal.receipt(119).token==17&&h.modal.hold(true,119));
    assert(operation(h,17,2,1,120));assert(!h.offer(packet(h,"ack","0:1"),121)); // old polled zero cannot undo a newer UI proof
    assert(h.offer(packet(h,"ack","7:2"),122));assert(h.modal.hold(false,123));
    assert(h.modal.hold(false,5000)); // real paused modal, including run_in_background
    h.clock(5000);assert(!h.covered(7,19)); // coverage expiry was not disabled while paused
    assert(operation(h,17,2,4,5001));
    assert(h.offer(packet(h,"ack","7:2"),5002));assert(!operation(h,17,2,2,5002)); // queued pre-click draw cannot close
    assert(operation(h,17,3,2,5003));assert(h.modal.hold(false,5004));
    assert(operation(h,17,4,3,5005));
    h.resume(true,5006);assert(!h.modal.hold(true,5006));assert(h.covered(7,19,5006)); // current JNI draw proof before first resumed draw
    assert(!h.offer(packet(h,"ack","0:3"),5007));
    assert(!operation(h,17,5,1,5007)); // old receipt cannot reopen

    WorldMapHud timeout;setup(timeout,10);assert(timeout.offer(packet(timeout,"prepare","1:7:1"),20));
    timeout.update(complete(),900);assert(timeout.modal.ownsInput(1019));assert(!timeout.modal.ownsInput(1020));
    assert(timeout.offer(packet(timeout,"ack","7:2"),1021));draw(timeout,1021);assert(!timeout.modal.receipt(1023).token); // numeric heartbeat never extends preparation
    WorldMapHud late;setup(late,10);assert(late.offer(packet(late,"prepare","2:7:1"),20));draw(late,21);
    assert(!operation(late,2,1,1,523));assert(!late.modal.hold(true,523));

    WorldMapHud failed;setup(failed,10);assert(failed.offer(packet(failed,"prepare","3:7:1"),20));draw(failed,21);
    assert(operation(failed,3,1,1,24));assert(failed.modal.hold(true,523));assert(failed.modal.receipt(523).failed==3);
    assert(operation(failed,3,1,0,524));assert(!failed.modal.hold(true,524)); // failed show / hide-fenced terminal cancel
    WorldMapHud close;setup(close,10);assert(close.offer(packet(close,"prepare","4:7:1"),20));draw(close,21);
    assert(operation(close,4,1,1,24));assert(close.modal.hold(false,25));assert(operation(close,4,1,4,30));assert(operation(close,4,2,2,31));
    assert(close.modal.hold(true,531)&&close.modal.receipt(531).failed==4); // no timeout-to-stock resumed frame
    assert(close.offer(packet(close,"ack","7:3"),532));assert(close.modal.hold(true,532)); // recovery is not an automatic late retry
    assert(operation(close,4,3,0,533));assert(!close.modal.hold(true,533));

    for(int invalidation=0;invalidation<4;++invalidation){
        WorldMapHud x;setup(x,100);assert(x.offer(packet(x,"prepare","8:7:1"),102));draw(x,103);
        if(invalidation==0)assert(x.offer(packet(x,"ack","0:2"),106));
        if(invalidation==1)assert(x.offer(packet(x,"ack","3:2"),106));
        if(invalidation==2){auto next=complete();next.identity="new-owner";x.update(next,106);}
        if(invalidation==3)assert(operation(x,8,2,0,106)); // Android layout/view proof invalidation
        assert(!operation(x,8,2,1,107));assert(!x.modal.hold(true,107));
    }
    WorldMapHud changed;setup(changed,100);assert(changed.offer(packet(changed,"prepare","9:7:1"),102));
    changed.beginDraw(103);assert(changed.covered(7,19,104));changed.presented(true);auto next=complete();next.owner=20;changed.update(next,105);changed.finishDraw(106);
    assert(!changed.modal.receipt(106).token); // owner changed after successful present, before publication
    WorldMapHud wrap;setup(wrap,UINT32_MAX-100);assert(wrap.offer(packet(wrap,"prepare","10:7:1"),UINT32_MAX-50));draw(wrap,UINT32_MAX-40);
    assert(operation(wrap,10,1,1,10));assert(wrap.modal.hold(false,11));
    std::cout<<"PASS real clock ordering, successful-present-only receipt, exact owner/proof/token, deadlines/wrap, paused background hold, native pending cancellation and current-proof resume/recovery\n";
}
int main()
{
    modalChecks();
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
    // The real main loop drains Java ACKs before rendering, then publishes the next snapshot.
    // A newly accepted ACK must not underflow against the previous frame's clock.
    WorldMapHud betweenFrames;betweenFrames.update(complete(),100);betweenFrames.resetActivity();
    assert(betweenFrames.offer(std::to_string(betweenFrames.revision())+"\t1\tack\t7",117));
    assert(betweenFrames.covered(7,19));
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
