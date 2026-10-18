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
    {frame.instruments=worldMapHud.source;frame.instrumentsVisible=frame.instruments.visible;}
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
        if(type && realShips && *type<realShips->GetElementsNum() && std::floor(*type)==*type)
        {
            auto *shipType=realShips->GetAClass(static_cast<int>(*type));
            ship.type=shipCaptionText(shipCanonicalType(shipType));
            if(const auto c=shipNumber(shipType,"Class");c && *c>=1 && *c<=7 && std::floor(*c)==*c)ship.shipClass=static_cast<int>(*c);
        }
        frame.identity+="|"+std::to_string(ship.id.size())+":"+ship.id+":"+std::to_string(ship.name.size())+":"+ship.name;
        frame.ships.push_back(std::move(ship));
    }
    frame.shipsComplete=battle && !battle->GetAttributeClass(("sign"+std::to_string(sign)).c_str());
    readWorldMapCommands(frame);
    // Respect the actual fast-action visibility as well as the command list.
    // Stock deliberately labels the island action EnterToSea in ILogAndActions.
    if(worldMapHud.contextActionOwner!=frame.owner || frame.commands.size()!=1 ||
        (worldMapHud.contextAction!=frame.commands.front().id &&
         !(worldMapHud.contextAction=="EnterToSea"&&frame.commands.front().id=="EnterToIsland")))frame.commandsReady=false;
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
