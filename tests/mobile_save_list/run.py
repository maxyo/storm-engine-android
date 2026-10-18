#!/usr/bin/env python3
"""Exercise production full-list identity and atomic original-script selection adapter."""
from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parents[2]
source=(root/'src/libs/xinterface/src/mobile_save_list.hpp').read_text()
state=source[source.index('struct SaveListSnapshot'):source.index('inline std::optional<SaveListSnapshot> ReadSaveList')]
cap=source[source.index('class SaveListCapability'):source.index('inline SaveListCapability saveListCapability')]
select=source[source.index('inline bool SelectSaveTarget'):source.index('\n}\nnamespace storm::mobile')]
restore=source[source.index('inline bool RestoreSaveTarget'):source.rindex('\n}')]
code=r'''
#include <string>
#include <vector>
#include <optional>
#include <cassert>
#include <cstdint>
#include <iostream>
struct MobileWidget { std::string node,type,label,value;int index,order;bool enabled;std::string rowId;std::vector<std::string> actions;std::string unavailableReason; };
''' + state + cap + r'''
SaveListSnapshot live;
std::optional<SaveListSnapshot> ReadSaveList(){return live;}
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
int main(){
 SaveListCapability cap;live.revision=4;live.profile="existing";live.saving=true;
 for(int i=0;i<37;++i)live.files.push_back("Save "+std::to_string(i));
 std::vector<MobileWidget> rows;cap.collect(live,true,rows);assert(rows.size()==38&&rows[37].unavailableReason=="new");
 auto token=[&](int index,const char *verb){return std::string(verb)+"@4:"+rows[index].rowId;};
 auto target=cap.accept(live,31,token(31,"delete"));assert(target);assert(SelectSaveTarget(*target));assert(page.value==30&&selection.value==31);
 selection.Set(34); // Delayed 350/500ms initial selection callback while confirmation is open.
 assert(RestoreSaveTarget(*target));assert(selection.value==31);
 live.files[31]="replacement";assert(!RestoreSaveTarget(*target));assert(!cap.accept(live,31,token(31,"delete")));
 live.files[31]="Save 31";live.profile="other";assert(!RestoreSaveTarget(*target));live.profile="existing";
 live.revision=5;assert(!RestoreSaveTarget(*target));live.revision=4;
 target=cap.accept(live,0,token(0,"activate"));assert(target&&SelectSaveTarget(*target)&&page.value==0&&selection.value==0);
 target=cap.accept(live,37,token(37,"activate"));assert(target&&SelectSaveTarget(*target)&&page.value==35&&selection.value==37);
 assert(!cap.accept(live,37,token(37,"delete")));assert(!cap.accept(live,2,token(1,"delete")));
 live.files.erase(live.files.begin());assert(!RestoreSaveTarget(*target));
 rows.clear();cap.collect(live,false,rows);for(const auto &row:rows)assert(!row.enabled&&row.actions.empty());
 live.saving=false;rows.clear();cap.collect(live,true,rows);assert(rows.size()==36);
 std::cout<<"PASS full list, stable identity, distant/earlier/new slot selection, delayed callback restore, profile/list/revision guards\n";
}
'''
with tempfile.TemporaryDirectory(prefix='mobile-saves-') as directory:
    p=Path(directory);(p/'check.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(p/'check.cpp'),'-o',str(p/'check')],check=True)
    subprocess.run([str(p/'check')],check=True)
