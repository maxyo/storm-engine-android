#include "platform/android_worldmap.hpp"
#include "attributes.h"
#include <cassert>
#include <map>
#include <iostream>
class Codec final : public VSTRING_CODEC
{
    std::map<uint32_t, std::string> values;
public:
    uint32_t GetNum() override { return values.size(); }
    uint32_t Convert(const char *value) override { return Convert(value, std::char_traits<char>::length(value)); }
    uint32_t Convert(const char *value, int32_t length) override
    {
        const std::string text(value, length);
        for (const auto &entry : values) if (entry.second == text) return entry.first;
        const auto code = uint32_t(values.size() + 1);
        values[code] = text;
        return code;
    }
    const char *Convert(uint32_t code) override { return values.at(code).c_str(); }
    void VariableChanged() override {}
};


struct VDATA {
 int32_t integer=0;std::vector<ATTRIBUTES *> attrs;
 bool Get(int32_t &value){value=integer;return true;}
 ATTRIBUTES *GetAClass(int index=0){return index>=0&&size_t(index)<attrs.size()?attrs[index]:nullptr;}
 uint32_t GetElementsNum(){return attrs.size();}
};
constexpr uint64_t invalid_entity=0;
struct Core {
 std::map<std::string,VDATA> variables;std::map<std::string,uint64_t> entities{{"WorldMap",19},{"WM_INTERFACE",20}};ATTRIBUTES *battle=nullptr;
 void *GetScriptVariable(const char *name){auto i=variables.find(name);return i==variables.end()?nullptr:&i->second;}
 uint64_t GetEntityId(const char *name){auto i=entities.find(name);return i==entities.end()?0:i->second;}
 ATTRIBUTES *Entity_GetAttributeClass(uint64_t id,const char *name){return id==20?battle:nullptr;}
} core;
struct SHIP_BASE {
 ATTRIBUTES *character,*type;
 ATTRIBUTES *GetACharacter(){return character;}
 ATTRIBUTES *GetAShip(){return type;}
 uint64_t GetId(){return 31;}
};
namespace storm::android {
GameplaySnapshot live;
GameplaySnapshot readGameplaySnapshot(bool){return live;}
std::string context="Martinique";
std::string worldMapContext(uint64_t){return context;}
void readWorldMapCommands(WorldMapFrame &f){f.shipsVisible=true;f.commandsReady=true;f.commands={{"EnterToIsland","Approach island"}};}
#include "helpers.inc"
#include "entry.inc"
#include "snapshot.inc"
}
int main(){
 using namespace storm::android;
 Codec codec;ATTRIBUTES captain(codec),companion(codec),profile(codec),badges(codec),shipType(codec);
 captain.SetAttribute("index","0");captain.SetAttribute("id","hero");
 auto put=[](ATTRIBUTES &root,const char *path,const char *value){root.CreateSubAClass(&root,path)->SetValue(value);};
 put(captain,"Ship.Name","Adelina");put(captain,"Ship.Type","0");put(captain,"Fellows.Companions.id1","1");
 companion.SetAttribute("index","1");companion.SetAttribute("id","friend");put(companion,"Ship.Name","Second ship");put(companion,"Ship.Type","0");
 profile.SetAttribute("name","QA_map");shipType.SetAttribute("Class","6");shipType.SetAttribute("Name","Lugger");
 for(const char *sign:{"sign1","sign2"}){auto *a=badges.CreateAttribute(sign,nullptr);a->SetAttribute("leftprogress","0.8");a->SetAttribute("rightprogress","0.9");a->SetAttribute("text","40");}
 core.battle=&badges;core.variables["Characters"].attrs={&captain,&companion};core.variables["nMainCharacterIndex"].integer=0;
 core.variables["PlayerProfile"].attrs={&profile};core.variables["RealShips"].attrs={&shipType};
 core.variables["wdmLockReload"].integer=0;core.variables["questwdmLockReload"].integer=0;
 live.mode="worldmap";live.scene="map19";GameplayState game;game.update(live);
 worldMapHud.contextActionOwner=19;worldMapHud.contextAction="EnterToSea";
 auto frame=readWorldMapFrame(game,true);assert(frame.available&&frame.commandsReady&&frame.ships.size()==2);
 assert(frame.ships[0].name=="Adelina"&&frame.ships[1].name=="Second ship");assert(frame.ships[0].hull==80&&frame.ships[0].crew==40&&frame.ships[0].shipClass==6);
 assert(frame.ships[0].type=="Lugger"&&frame.ships[1].type=="Lugger");
 WorldMapHud typedHud;typedHud.update(frame,1);
 assert(typedHud.json().find("\"type\":\"Lugger\"")!=std::string::npos);
 SHIP_BASE seaShip{&captain,&shipType};auto sea=shipEntry(&seaShip);
 assert(sea.type=="Lugger"&&sea.shipClass==6&&sea.name=="Adelina");
 // Actual stock generation keeps BaseName and appends "1" to the rendered model.
 shipType.SetAttribute("BaseName","Lugger");shipType.SetAttribute("Name","Lugger1");
 assert(shipEntry(&seaShip).type=="Lugger");
 assert(readWorldMapFrame(game,true).ships[0].type=="Lugger");
 shipType.SetAttribute("BaseName","");shipType.SetAttribute("Name","Lugger");
 for(const char *invalid:{"-1","2.5","8","unknown"}){shipType.SetAttribute("Class",invalid);assert(!shipEntry(&seaShip).shipClass);}
 shipType.SetAttribute("Class","6");
 // The optional model is a live RealShips field, not the vessel's custom name.
 put(captain,"Ship.Name","Frigate");assert(shipEntry(&seaShip).type=="Lugger");
 shipType.SetAttribute("Name","");assert(shipEntry(&seaShip).type.empty());
 auto unknownType=readWorldMapFrame(game,true);assert(unknownType.ships[0].type.empty()&&unknownType.availableMask()==frame.availableMask());
 typedHud.update(unknownType,2);assert(typedHud.json().find("\"type\":")==std::string::npos);
 shipType.SetAttribute("Name","Lugger");put(captain,"Ship.Name","Adelina");
 assert(!frame.instrumentsVisible&&frame.availableMask()==6); // No fake instruments before renderer supplies them.
 // WM_SetShipData omits land-only/stale companions, zero sentinel and slots
 // beyond COMPANION_MAX. They must not consume badge positions or poison all fleet coverage.
 put(captain,"Fellows.Companions.id1","0");
 put(captain,"Fellows.Companions.id2","1");put(companion,"Ship.Type","-1");
 put(captain,"Fellows.Companions.id5","1");
 core.battle=nullptr; // Retain the strict missing-badges fallback.
 assert(!(readWorldMapFrame(game,true).availableMask()&MapShips));
 ATTRIBUTES singleBadge(codec);auto *only=singleBadge.CreateAttribute("sign1",nullptr);
 only->SetAttribute("leftprogress","0.8");only->SetAttribute("rightprogress","0.9");only->SetAttribute("text","40");
 core.battle=&singleBadge;
 auto single=readWorldMapFrame(game,true);
 assert(single.ships.size()==1&&single.shipsComplete&&(single.availableMask()&MapShips));
 assert(single.ships.front().name=="Adelina"&&single.ships.front().crew==40);
 // A valid later slot still consumes the next native sign after skipped slots.
 put(captain,"Fellows.Companions.id2","-1");put(companion,"Ship.Type","0");put(captain,"Fellows.Companions.id4","1");
 core.battle=&badges;auto later=readWorldMapFrame(game,true);
 assert(later.ships.size()==2&&later.shipsComplete&&(later.availableMask()&MapShips));
 assert(later.ships[1].name=="Second ship"&&later.ships[1].crew==40);
 worldMapHud.contextAction="Closed";assert(!readWorldMapFrame(game,true).commandsReady);worldMapHud.contextAction="EnterToSea";
 const auto identity=frame.identity;profile.SetAttribute("name"," QA_map");assert(readWorldMapFrame(game,true).identity!=identity);profile.SetAttribute("name","other");assert(readWorldMapFrame(game,true).identity!=identity);
 core.variables["questwdmLockReload"].integer=1;assert(!readWorldMapFrame(game,true).commandsReady);
 core.variables.erase("questwdmLockReload");assert(!readWorldMapFrame(game,true).commandsReady);
 live.mode="sea";assert(!readWorldMapFrame(game,true).available);live.mode="worldmap";
 assert(!readWorldMapFrame(game,false).available);core.variables.erase("PlayerProfile");assert(!readWorldMapFrame(game,true).available);
 std::cout<<"PASS actual map snapshot: own fleet badges, canonical companion filters/one ship/late slots, identity/profile, missing data, quest guards, gameplay transition\n";
}
