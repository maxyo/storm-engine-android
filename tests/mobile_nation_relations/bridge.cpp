#include "mobile_nation_relations.hpp"
#include <map>
#include <cctype>
#include <cassert>
#include <iostream>
namespace storm {bool iEquals(std::string a,std::string b){for(auto &c:a)c=std::tolower(c);for(auto&c:b)c=std::tolower(c);return a==b;}}
namespace storm::android {std::string mobileScreen="nationrelation";uint64_t mobileScreenRevision=9;}
struct Strings {std::map<std::string,std::string> data;const char *GetAttribute(const char *key){return data.count(key)?data[key].c_str():nullptr;}} strings;
struct Core {int closed=0;Strings *Entity_GetAttributeClass(int,const char*){return &strings;}void Event(const char*){closed++;}} core;
int g_idInterface=1;
struct Service {const char *GetString(const char *key){return key;}} service;
namespace storm::mobile {ImageRef AtlasReference(Service *,const char *group,const char *image){ImageRef ref;ref.source={"atlas",group,image,{},{},-1};return ref;}}
enum {ACTION_MOUSERCLICK=7,ACTION_MOUSECLICK=8,ACTION_ACTIVATE=9};
struct CINODE {
 std::string name;std::vector<MobileWidget> model;bool m_bUse=true,m_bLockedNode=false,m_bLockStatus=false,m_bClickable=true,m_bSelected=true;int m_nDoDelay=0,fired=0,code=0;CINODE *m_next=nullptr,*m_list=nullptr;
 void MobileCollect(std::vector<MobileWidget>&out){out.insert(out.end(),model.begin(),model.end());}
 bool CheckCommandUsed(int){return true;}CINODE *DoAction(int a,bool&,bool){fired++;code=a;return nullptr;}
};
struct XINTERFACE {
 struct IMAGE_Entity {struct {int left,top;}position;const char *sImageName,*sImageListName,*sPicture;IMAGE_Entity *next=nullptr;};
 IMAGE_Entity *m_imgLists=nullptr;CINODE *m_pNodes=nullptr;Service *pPictureService=&service,*pStringService=&service;
 std::string m_mobileNationSignature;uint64_t m_mobileNationGeneration=0;
 CINODE *FindNode(const char *name,void*){for(auto *n=m_pNodes;n;n=n->m_next)if(storm::iEquals(n->name,name))return n;return nullptr;}
 void MobileCollectShipyard(std::vector<MobileWidget>&){}
 void SetCurNode(CINODE*){}void MobileCollectWidgets(std::vector<MobileWidget>&);void MobileCollectNations(std::vector<MobileWidget>&);
 struct Action {std::string screen="nationrelation",node,value;int index=-1;};
 void apply(Action action){for(int once=0;once<1;once++){auto *node=FindNode(action.node.c_str(),nullptr);if(!node)continue;
 #include "dispatch.inc"
 }}
};
#include "collection.inc"
int main(){
 XINTERFACE xi;std::vector<CINODE> nodes;
 auto node=[&](std::string name,std::string type,std::string label){CINODE n;n.name=name;n.model.push_back({name,type,label,"",-1});nodes.push_back(n);};
 node("TITLE","heading","Relations");for(auto name:{"ENG_TEXT","FRA_TEXT","SPA_TEXT","HOL_TEXT"})node(name,"text","Native reputation");
 node("FlagPic","image","");storm::mobile::ImageRef flag;flag.source={"atlas","NATIONS","France",{},{},-1};nodes.back().model[0].images.push_back(flag);
 node("FLAGS","button","Change");node("LEFTCHANGE_NATION","button","<");node("RIGHTCHANGE_NATION","button",">");node("INFO_CLICK","text","");
 for(size_t i=0;i+1<nodes.size();i++)nodes[i].m_next=&nodes[i+1];xi.m_pNodes=&nodes[0];
 std::vector<XINTERFACE::IMAGE_Entity> pictures;std::vector<std::string> names;names.reserve(34);pictures.reserve(34);const char *ids[]={"England","France","Spain","Holland","Pirate"};
 int num=1;auto picture=[&](const char *group,const char*image,int x,int y,const char*name=nullptr){names.push_back(name?name:"pic"+std::to_string(num++));pictures.push_back({{x,y},names.back().c_str(),group,image});};
 for(int i=0;i<5;i++)picture("NATIONS",ids[i],156+76*i,120);for(int i=0;i<5;i++)picture("NATIONS",ids[i],75,200+70*i);
 for(int r=0;r<5;r++)for(int c=0;c<5;c++)if(r!=c)picture("relations",r==0?"Enemy":"Neutral",156+76*c,200+70*r);
 picture("NATIONS","ENGLAND",610,136,"eng");picture("NATIONS","FRANCE",610,192,"FRA");picture("NATIONS","SPAIN",610,248,"spa");picture("NATIONS","HOLLAND",610,304,"hOl");
 for(size_t i=0;i+1<pictures.size();i++)pictures[i].next=&pictures[i+1];xi.m_imgLists=&pictures[0];
 for(auto key:{"EngH","FraH","SpaH","HolH","titul"})strings.data[key]=key;
 auto snapshot=[&](){std::vector<MobileWidget> v;xi.MobileCollectWidgets(v);return v;};
 auto control=[&](std::string name,int index=-1){auto v=snapshot();for(auto &w:v)if(w.node==name&&w.index==index)return w;assert(false);return MobileWidget{};};
 assert(control("ANDROID_NATION_RELATIONS").value=="1");
 strings.data["EngH"]="";auto missing=control("ANDROID_NATION_RELATIONS");assert(missing.value=="0"&&missing.unavailableReason.find("Eng:caption-missing")!=std::string::npos);strings.data["EngH"]="EngH";assert(control("FLAGS").actions.size()==1);
 auto token=control("FLAGS").actions[0];xi.apply({"nationrelation","FLAGS",token,-1});assert(nodes[6].fired==1);xi.apply({"nationrelation","FLAGS",token,-1});assert(nodes[6].fired==1);
 token=control("FLAGS").actions[0];nodes[5].model[0].images[0].source.image="Pirate";xi.apply({"nationrelation","FLAGS",token,-1});assert(nodes[6].fired==1);
 token=control("FLAGS").actions[0];storm::android::mobileScreenRevision++;xi.apply({"nationrelation","FLAGS",token,-1});assert(nodes[6].fired==1);
 token=control("FLAGS").actions[0];nodes[6].m_bSelected=false;xi.apply({"nationrelation","FLAGS",token,-1});assert(nodes[6].fired==1);assert(control("FLAGS").actions.empty());nodes[6].m_bSelected=true;
 token=control("FLAGS").actions[0];nodes[6].m_nDoDelay=150;xi.apply({"nationrelation","FLAGS",token,-1});assert(nodes[6].fired==1);nodes[6].m_nDoDelay=0;
 token=control("INFO_CLICK",-2).actions[0];xi.apply({"nationrelation","INFO_CLICK",token,-2});assert(nodes[9].fired==1&&nodes[9].code==ACTION_MOUSERCLICK);
 token=control("FlagPic",-2).actions[0];nodes[5].m_bLockedNode=true;xi.apply({"nationrelation","FlagPic",token,-2});assert(nodes[5].fired==0);
 std::cout<<"PASS actual collection and native dispatch: source/candidate/epoch updates revoke, disabled/locked/delayed deny, single-use tokens, exact right-click help\n";
}
