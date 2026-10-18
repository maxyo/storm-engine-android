#include "mobile_owner_selection.hpp"
#include <cassert>
#include <iostream>
#include <map>
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


int main(){
 using namespace storm::mobile;Codec codec;ATTRIBUTES attrs(codec);
 attrs.SetAttribute("ListSize","3");attrs.SetAttribute("current","0");
 auto *one=attrs.CreateAttribute("pic1",nullptr);one->SetAttribute("companionIndex","7");one->SetAttribute("character","7");
 auto *two=attrs.CreateAttribute("pic2",nullptr);two->SetAttribute("companionIndex","8");two->SetAttribute("character","8");
 auto label=[](uint32_t owner,bool ship){return owner==7||owner==8?std::string(ship?"vessel:":"person:")+std::to_string(owner):std::string();};
 OfficerListIdentity state;std::vector<MobileWidget> rows;OfficerSelection capability;
 auto collect=[&](const char *screen,const char *node,bool vertical=false,uint64_t epoch=17,const char *profile="QA"){
  return OwnerRows(screen,node,vertical,&attrs,3,epoch,profile,true,10,label,"Empty ship slot",state,rows);
 };
 for(const char *screen:{"store","storage","contraband","cannons","hirecrew","repair"}){
  assert(collect(screen,"SHIPS_SCROLL"));assert(rows.size()==2&&rows[0].label=="vessel:7"&&rows[1].index==1);
  assert(state.rows.size()==3);capability.publish(state);
  const auto action="select@17:"+capability.rowId(1);assert(capability.accepts(state,1,action));
  two->SetAttribute("companionIndex","7");assert(collect(screen,"SHIPS_SCROLL"));assert(!capability.accepts(state,1,action));two->SetAttribute("companionIndex","8");
  assert(collect(screen,"SHIPS_SCROLL",false,18));assert(!capability.accepts(state,1,action));
  assert(collect(screen,"SHIPS_SCROLL",false,17,"Other profile"));assert(!capability.accepts(state,1,action));
 }
 for(const char *screen:{"itemstrade","itemsbox"}){assert(collect(screen,"CHARACTERS_SCROLL"));assert(rows[0].label=="person:7");}
 for(const char *screen:{"ship","shipyard"})assert(collect(screen,"SHIPS_SCROLL",true));
 for(const char *screen:{"ship","shipyard","character","lscchar_all","transfer_main"})assert(collect(screen,"PASSENGERSLIST"));
 assert(!collect("save_load","SHIPS_SCROLL"));assert(!collect("items","CHARACTERS_SCROLL"));
 assert(!collect("store","CHARACTERS_SCROLL"));assert(!collect("store","SHIPS_SCROLL",true));
 assert(!collect("store","SHIPS_SCROLL",false,0));assert(!collect("store","SHIPS_SCROLL",false,17,""));
 for(const char *bad:{"-1","+1","7x","4294967296",""}){
  one->SetAttribute("companionIndex",bad);assert(!collect("store","SHIPS_SCROLL"));
 }
 one->SetAttribute("companionIndex","7");
 one->SetAttribute("character","-1");assert(collect("shipyard","SHIPS_SCROLL",true));assert(rows[0].label=="Empty ship slot");
 assert(!collect("ship","SHIPS_SCROLL",true));assert(!collect("itemstrade","CHARACTERS_SCROLL"));
 one->SetAttribute("character","7");attrs.SetAttribute("ListSize","4");assert(!collect("store","SHIPS_SCROLL"));
 attrs.SetAttribute("ListSize","3");attrs.SetAttribute("current","-1");assert(!collect("store","SHIPS_SCROLL"));
 attrs.SetAttribute("current","3");assert(!collect("store","SHIPS_SCROLL"));
 attrs.SetAttribute("current","0");assert(collect("hirecrew","SHIPS_SCROLL"));capability.publish(state);
 const auto guard="guard@17:"+capability.rowId(0)+"|press";
 for(const char *node:{"QTY_OK_BUTTON","QTY_CANCEL_BUTTON","QTY_EDIT","QTY_ADD_BUTTON","QTY_REMOVE_BUTTON","QTY_ADD_ALL_BUTTON","QTY_REMOVE_ALL_BUTTON"})
  assert(HireCrewQuantityValue("hirecrew",node,capability,state,rows,guard)=="press");
 auto accepts=[&](const std::string &value){return HireCrewQuantityValue("hirecrew","QTY_OK_BUTTON",capability,state,rows,value).has_value();};
 assert(!HireCrewQuantityValue("store","QTY_OK_BUTTON",capability,state,rows,guard));
 assert(!HireCrewQuantityValue("hirecrew","QTY_UNKNOWN",capability,state,rows,guard));
 for(const std::string value:std::vector<std::string>{"press","guard@18:"+capability.rowId(0)+"|press",guard+"|press",guard+"\n",guard+std::string(1,'\0'),"guard@17:"+capability.rowId(0)+"|"})assert(!accepts(value));
 assert(HireCrewQuantityValue("hirecrew","QTY_EDIT",capability,state,rows,"guard@17:"+capability.rowId(0)+"|25")=="25");
 attrs.SetAttribute("current","1");assert(collect("hirecrew","SHIPS_SCROLL"));assert(!accepts(guard));
 attrs.SetAttribute("current","2");assert(collect("hirecrew","SHIPS_SCROLL"));assert(!accepts(guard)); // padding
 attrs.SetAttribute("current","0");assert(collect("hirecrew","SHIPS_SCROLL",false,17,"Other"));assert(!accepts(guard));
 assert(collect("hirecrew","SHIPS_SCROLL",false,18));assert(!accepts(guard));
 one->SetAttribute("companionIndex","8");assert(collect("hirecrew","SHIPS_SCROLL"));assert(!accepts(guard));
 one->SetAttribute("companionIndex","7");assert(collect("hirecrew","SHIPS_SCROLL"));assert(accepts(guard));
 rows[0].enabled=false;assert(!accepts(guard));rows[0].enabled=true;
 rows[1].value="1";assert(!accepts(guard));rows.clear();assert(!accepts(guard));
 ATTRIBUTES active(codec);active.SetAttribute("index","7");assert(HireCrewBoundOwner(&attrs,&active));
 attrs.SetAttribute("current","1");assert(!HireCrewBoundOwner(&attrs,&active)); // ProcessFrame has not run
 active.SetAttribute("index","8");assert(HireCrewBoundOwner(&attrs,&active));
 attrs.SetAttribute("current","2");assert(!HireCrewBoundOwner(&attrs,&active));
 attrs.SetAttribute("current","1");active.SetAttribute("index","bad");assert(!HireCrewBoundOwner(&attrs,&active));
 assert(!HireCrewBoundOwner(nullptr,&active));assert(!HireCrewBoundOwner(&attrs,nullptr));
 std::cout<<"PASS real owner selectors, padding, known empty shipyard slots, exact fields, owner/profile/epoch and malformed guards; hirecrew quantity owner/profile/epoch/selection validation\n";
}
