#!/usr/bin/env python3
"""Exercise production full-list identity across profiles, the record facts parser and atomic original-script selection adapter."""
from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parents[2]
source=(root/'src/libs/xinterface/src/mobile_save_list.hpp').read_text()
state=source[source.index('// One record:'):source.index('// The save list of the open save_load interface')]
facts=source[source.index('// What the save list shows of a record'):source.index('// Identity is bound to the complete current list')]
cap=source[source.index('class SaveListCapability'):source.index('inline SaveListCapability saveListCapability')]
select=source[source.index('inline bool SelectSaveTarget'):source.index('\n}\nnamespace storm::mobile')]
restore=source[source.index('inline bool RestoreSaveTarget'):source.rindex('\n}')]
code=r'''
#include <string>
#include <vector>
#include <optional>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <cassert>
#include <cstdint>
#include <iostream>
struct MobileWidget { std::string node,type,label,value;int index,order;bool enabled;std::string rowId;std::vector<std::string> actions;std::string unavailableReason;std::map<std::string,std::string> context; };
''' + state + facts + cap + r'''
SaveListSnapshot live;
std::map<std::string,std::vector<std::string>> profiles; // the profile folders on disk
std::optional<SaveListSnapshot> ReadSaveList(){return live;}
int switches=0;
bool ChooseSaveProfile(const std::string &profile){
 if(!profiles.count(profile))return false;
 ++switches;live.profile=profile;live.files=profiles[profile];live.selected=0;return true;
}
struct VDATA {int value;bool Get(int32_t &out){out=value;return true;}void Set(int32_t in){value=in;live.selected=in;}} page{0},selection{0};
constexpr int ACTION_MOUSECLICK=1;
struct Core {
 void *GetScriptVariable(const char *name){return std::string(name)=="g_nFirstSaveIndex"?&page:&selection;}
 void Event(const char *name,const char *,float position,const char *){
  assert(std::string(name)=="ScrollPosChange");page.value=int((live.files.size()/5)*position)*5;selection.Set(page.value);
 }
 void Event(const char *name,const char *,int,const char *node){
  assert(std::string(name)=="eventSaveClick");selection.Set(page.value+std::stoi(std::string(node).substr(7))-1);
 }
} core;
''' + select + restore + r'''
auto rowFacts=[](size_t index){return std::map<std::string,std::string>{{"row",std::to_string(index)}};};
int main(){
 // A description of the game ("label@faces@time  date@play time@language") and this port's "@hero@location".
 SaveFacts f=ParseSaveDescription("Сен-Пьер магазин 1@face_1,*@13:25  01.10.1654@0 hours 25 min.@russian@Шарль де Мор@Сен-Пьер магазин");
 assert(f.hero=="Шарль де Мор"&&f.location=="Сен-Пьер магазин"&&f.gameDate=="1654-10-01"&&f.gameTime=="13:25"&&f.playMinutes==25);
 f=ParseSaveDescription("Мартиника - море@*@09:05  07.10.1654@2days - 50 hours 3 min.@russian");
 assert(f.hero.empty()&&f.location=="Мартиника - море"&&f.gameDate=="1654-10-07"&&f.playMinutes==50*60+3);
 assert(SavePlayMinutes("1days - 2 hours 0 min.")==26*60&&SavePlayMinutes("garbage")==-1);
 f=ParseSaveDescription("broken");assert(f.location.empty()&&f.playMinutes==-1);
 f=ParseSaveDescription("x@*@99:99  45.13.1654@1 hours 1 min.@russian");assert(f.gameDate.empty()&&f.location=="x");
 // Saving: the current profile's records and the new slot.
 SaveListCapability cap;live.revision=4;live.profile="existing";live.saving=true;
 for(int i=0;i<37;++i)live.files.push_back("Save "+std::to_string(i));
 profiles["existing"]=live.files;
 auto own=[&]{live.records.clear();for(const auto &f:live.files)live.records.push_back({live.profile,f});};own();
 std::vector<MobileWidget> rows;cap.collect(live,true,rows,rowFacts);
 assert(rows.size()==39&&rows[0].node=="ANDROID_SAVES"&&rows[0].value=="37");rows.erase(rows.begin()); // the list, then the records
 assert(rows[37].unavailableReason=="new"&&rows[37].context.at("row")=="37"&&rows[3].context.at("row")=="3");
 auto token=[&](int index,const char *verb){return std::string(verb)+"@4:"+rows[index].rowId;};
 auto target=cap.accept(live,31,token(31,"delete"));assert(target);assert(SelectSaveTarget(*target));assert(page.value==30&&selection.value==31);
 selection.Set(34); // Delayed 350/500ms initial selection callback while confirmation is open.
 assert(RestoreSaveTarget(*target));assert(selection.value==31);
 live.records[31].file="replacement";assert(!RestoreSaveTarget(*target));assert(!cap.accept(live,31,token(31,"delete")));own();
 live.revision=5;assert(!RestoreSaveTarget(*target));live.revision=4;
 target=cap.accept(live,0,token(0,"activate"));assert(target&&SelectSaveTarget(*target)&&page.value==0&&selection.value==0);
 target=cap.accept(live,37,token(37,"activate"));assert(target&&SelectSaveTarget(*target)&&page.value==35&&selection.value==37);
 assert(!cap.accept(live,37,token(37,"delete")));assert(!cap.accept(live,2,token(1,"delete")));
 assert(switches==0);
 rows.clear();cap.collect(live,false,rows,rowFacts);for(const auto &row:rows)assert(!row.enabled&&row.actions.empty());
 // Loading: every profile's records in one list, newest first; no profile is shown or chosen.
 profiles["old build"]={"Old 0","Old 1","Old 2"};
 live.saving=false;live.revision=6;selection.Set(0);live.records={{"old build","Old 0"},{"existing","Save 0"},{"old build","Old 1"},{"existing","Save 1"},{"old build","Old 2"}};
 rows.clear();cap.collect(live,true,rows,rowFacts);rows.erase(rows.begin());
 assert(rows.size()==5&&rows[0].label=="Old 0"&&rows[1].label=="Save 0"&&rows[1].value=="1"&&rows[0].value=="0");
 auto token6=[&](int index,const char *verb){return std::string(verb)+"@6:"+rows[index].rowId;};
 // A record of another profile: that profile becomes current, then its slot is clicked.
 target=cap.accept(live,2,token6(2,"activate"));assert(target&&SelectSaveTarget(*target));
 assert(switches==1&&live.profile=="old build"&&selection.value==1);
 assert(RestoreSaveTarget(*target)&&selection.value==1);
 // Its own profile current again (the list is idle): the same rows, a record of it, no switch back needed.
 ChooseSaveProfile("existing");switches=0;
 target=cap.accept(live,3,token6(3,"delete"));assert(target&&SelectSaveTarget(*target)&&switches==0&&selection.value==1);
 assert(cap.accept(live,4,token6(4,"delete"))&&!cap.accept(live,5,token6(4,"activate")));
 // The game's profile name is spelled unlike its folder ("Player" for the folder "player",
 // the game's paths fold ASCII case): its records are its own, no switch, the same slot.
 assert(SameProfile("Player","player")&&!SameProfile("Player","player2")&&!SameProfile("Плеер","плеер"));
 live.profile="EXISTING";switches=0;
 target=cap.accept(live,3,token6(3,"delete"));assert(target&&SelectSaveTarget(*target)&&switches==0&&selection.value==1);
 assert(RestoreSaveTarget(*target));live.profile="existing";
 // A record whose profile folder is gone cannot be chosen.
 live.records.push_back({"vanished","X"});rows.clear();cap.collect(live,true,rows,rowFacts);rows.erase(rows.begin());
 auto token7=[&](int index,const char *verb){return std::string(verb)+"@6:"+rows[index].rowId;};
 target=cap.accept(live,5,token7(5,"activate"));assert(target&&!SelectSaveTarget(*target));
 // No records at all: the empty list.
 live.records.clear();live.files.clear();rows.clear();cap.collect(live,true,rows,rowFacts);assert(rows.size()==1&&rows[0].node=="ANDROID_SAVES"&&rows[0].value=="0");
 std::cout<<"PASS record facts, every profile in one list, records of other profiles chosen through the game, stable identity, distant/earlier/new slot selection, delayed callback restore, profile/list/revision guards, profile names compared the game's way (ASCII case)\n";
}
'''
with tempfile.TemporaryDirectory(prefix='mobile-saves-') as directory:
    p=Path(directory);(p/'check.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(p/'check.cpp'),'-o',str(p/'check')],check=True)
    subprocess.run([str(p/'check')],check=True)
