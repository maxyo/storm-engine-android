#pragma once
#ifdef __ANDROID__
#include "android_gameplay_state.hpp"
#include "platform/android_worldmap.hpp"
namespace storm::android
{
inline std::string mapText(ATTRIBUTES *attributes,const char *path)
{
    auto *value=attributes?attributes->FindAClass(attributes,path):nullptr;
    return value&&value->HasValue()?shipCaptionText(value->GetValue()):std::string{};
}
// A script flag (bool or int attribute): absent or empty is false, as sti() of it; else 0 / 1.
inline std::optional<bool> mapFlag(ATTRIBUTES *attributes,const char *path)
{
    auto *value=attributes?attributes->FindAClass(attributes,path):nullptr;
    if(!value||!value->HasValue()||value->GetValue().empty())return false;
    const auto number=shipNumberValue(value->GetValue());
    if(!number||(*number!=0&&*number!=1))return std::nullopt;
    return *number==1;
}
// The rain the sea will have when the map is left (mapRainState): WeatherParams and the start of
// the rain the scripts keep on the hero (SaveCurrentQuestDateParam("Rain.Duration")).
inline std::optional<int> readMapRainState(ATTRIBUTES *hero,const MapInstrumentFrame &instruments)
{
    auto *weather=static_cast<VDATA *>(core.GetScriptVariable("WeatherParams"));
    auto *params=weather?weather->GetAClass():nullptr;
    if(!params||!instruments.hour)return std::nullopt;
    MapRainPlan plan;
    plan.raining=mapFlag(params,"Rain");plan.today=mapFlag(params,"Rain.ThisDay");
    plan.start=shipNumber(params,"Rain.StartTime");plan.duration=shipNumber(params,"Rain.Duration");
    const auto year=shipNumber(hero,"Rain.Duration.control_year"),month=shipNumber(hero,"Rain.Duration.control_month"),
        day=shipNumber(hero,"Rain.Duration.control_day"),time=shipNumber(hero,"Rain.Duration.control_time");
    if(year&&month&&day&&time)
    {
        const auto began=mapCalendarMinutes(static_cast<int>(*year),static_cast<int>(*month),static_cast<int>(*day),*time),
            now=mapCalendarMinutes(instruments.year,instruments.month,instruments.day,*instruments.hour);
        if(!began||!now)return std::nullopt;
        plan.elapsed=std::max(0.0,*now-*began); // GetPastTime: a start after now counts nothing
    }
    return mapRainState(plan,*instruments.hour);
}
inline WorldMapFrame readWorldMapFrame(const GameplayState &gameplay, bool ready)
{
    WorldMapFrame frame;
    if (!ready || gameplay.snapshot().mode!="worldmap" || readGameplaySnapshot(true).mode!="worldmap") return frame;
    frame.owner=core.GetEntityId("WorldMap");
    auto *main=shipMainCharacter();
    auto *characters=static_cast<VDATA *>(core.GetScriptVariable("Characters"));
    auto *profile=static_cast<VDATA *>(core.GetScriptVariable("PlayerProfile"));
    auto *profileAttrs=profile?profile->GetAClass():nullptr;
    auto *profileValue=profileAttrs?profileAttrs->FindAClass(profileAttrs,"name"):nullptr;
    // Profile identity is exact, not a trimmed display caption.
    const std::string profileName=profileValue&&profileValue->HasValue()?profileValue->GetValue():"";
    if(frame.owner==invalid_entity || !main || !characters || shipCaptionText(profileName).empty())return {};
    frame.available=true;frame.gameplayRevision=gameplay.revision();
    frame.identity=std::to_string(frame.owner)+":"+std::to_string(profileName.size())+":"+profileName+":"+shipText(main,"index");
    frame.context=worldMapContext(frame.owner);
    if(frame.context.empty())return {};
    if(worldMapHud.source.owner==frame.owner)
    {frame.instruments=worldMapHud.source;frame.instrumentsVisible=frame.instruments.visible;
     frame.instruments.rainState=readMapRainState(main,frame.instruments);}
    auto *battle=core.Entity_GetAttributeClass(core.GetEntityId("WM_INTERFACE"),"wm_sign");
    auto *realShips=static_cast<VDATA *>(core.GetScriptVariable("RealShips"));
    unsigned sign=1;
    // Match WM_SetShipData/GetCompanionIndex: TEHO has the player and four
    // companion slots; zero and characters without a ship are not fleet signs.
    for(int slot=0;slot<5;++slot)
    {
        const auto index=slot==0?shipNumber(main,"index"):shipNumber(main,("Fellows.Companions.id"+std::to_string(slot)).c_str());
        if(!index || std::floor(*index)!=*index || *index>=characters->GetElementsNum() || (slot>0&&*index<1))continue;
        auto *character=characters->GetAClass(static_cast<int>(*index));if(!character)continue;
        const auto type=shipNumber(character,"Ship.Type");
        if(slot>0&&(!type||std::floor(*type)!=*type))continue;
        MapShipFrame ship;
        ship.name=mapText(character,"Ship.Name");
        const auto characterId=mapText(character,"id");
        if(!characterId.empty())ship.id=std::to_string(static_cast<int>(*index))+":"+characterId+":"+shipText(character,"Ship.Type");
        auto *badge=battle?battle->GetAttributeClass(("sign"+std::to_string(sign++)).c_str()):nullptr;
        if(const auto v=shipNumber(badge,"leftprogress");v && *v<=1)ship.hull=*v*100;
        if(const auto v=shipNumber(badge,"rightprogress");v && *v<=1)ship.sails=*v*100;
        if(const auto v=shipNumber(badge,"text");v && *v<=100000 && std::floor(*v)==*v)ship.crew=static_cast<int>(*v);
        if(auto *uv=badge?badge->FindAClass(badge,"faceuv"):nullptr;uv&&uv->HasValue()&&battle)
        {
            std::array<float,4> rect{};
            if(std::sscanf(uv->GetValue().c_str(),"%f,%f,%f,%f",&rect[0],&rect[1],&rect[2],&rect[3])==4)
                ship.picture=shipPicture(shipText(battle,"shiptexturename"),rect);
        }
        if(type && realShips && *type<realShips->GetElementsNum() && std::floor(*type)==*type)
        {
            auto *shipType=realShips->GetAClass(static_cast<int>(*type));
            ship.type=shipCaptionText(shipCanonicalType(shipType));
            if(const auto c=shipNumber(shipType,"Class");c && *c>=1 && *c<=7 && std::floor(*c)==*c)ship.shipClass=static_cast<int>(*c);
            if(const auto m=shipNumber(shipType,"MaxCrew");m && *m>=1 && *m<=100000 && std::floor(*m)==*m)ship.crewMax=static_cast<int>(*m);
        }
        frame.identity+="|"+std::to_string(ship.id.size())+":"+ship.id+":"+std::to_string(ship.name.size())+":"+ship.name;
        frame.ships.push_back(std::move(ship));
    }
    frame.shipsComplete=battle && !battle->GetAttributeClass(("sign"+std::to_string(sign)).c_str());
    readWorldMapCommands(frame);
    // Respect the actual fast-action visibility as well as the command list.
    // Stock deliberately labels the island action EnterToSea in ILogAndActions.
    // An encounter offers several commands (attack, enter the storm, go to sea); the native
    // fast action must be one of them.
    const auto offered=[&](const std::string &id){return std::any_of(frame.commands.begin(),frame.commands.end(),
        [&](const MapCommand &c){return c.id==id;});};
    if(worldMapHud.contextActionOwner!=frame.owner || frame.commands.empty() ||
        (!offered(worldMapHud.contextAction) && !(worldMapHud.contextAction=="EnterToSea"&&offered("EnterToIsland"))))frame.commandsReady=false;
    for(const auto *flag:{"wdmLockReload","questwdmLockReload"})
    {
        auto *v=static_cast<VDATA *>(core.GetScriptVariable(flag));int32_t locked=1;
        if(!v || !v->Get(locked) || locked)frame.commandsReady=false;
    }
    if(core.GetEntityId("fader")!=invalid_entity)frame.commandsReady=false;
    return frame;
}
}
#endif
