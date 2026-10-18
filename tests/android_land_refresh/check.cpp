#include "platform/android_land_commands.hpp"
#include "platform/android_script_errors.hpp"
#include <cassert>
#include <charconv>
#include <cstdio>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#define __ANDROID__ 1
namespace storm::android { inline void traceLandAdapter(unsigned,const std::string&) {} }
// Attribute/file/interpreter boundaries only; refresh admission and DATA conversion are extracted production bodies.
struct ATTRIBUTES {
    std::optional<std::string> value;
    std::map<std::string,std::unique_ptr<ATTRIBUTES>> children;
    ATTRIBUTES *GetAttributeClass(const char *name){auto it=children.find(name);return it==children.end()?nullptr:it->second.get();}
    ATTRIBUTES *child(const char *name){auto &v=children[name];if(!v)v=std::make_unique<ATTRIBUTES>();return v.get();}
    void set(const char *name,std::string text){child(name)->value=std::move(text);}
    bool HasValue(){return value.has_value();}const std::string &GetValue(){return *value;}
    const char *GetThisAttr(){return value?value->c_str():nullptr;}
    bool DeleteAttributeClassX(ATTRIBUTES *p){for(auto it=children.begin();it!=children.end();++it)if(it->second.get()==p){children.erase(it);return true;}return false;}
};
namespace fast_float {using std::from_chars;}
static std::string to_string(const char *text){return text;}
enum S_TOKEN_TYPE {VAR_PTR,VAR_INTEGER,VAR_FLOAT,VAR_STRING,VAR_OBJECT,VAR_REFERENCE,VAR_AREFERENCE,NUMBER,FLOAT_NUMBER,STRING};
constexpr const char *UNINIT_REF="reference",*INVALID_CONVERSATION="Invalid conversation";
struct DATA {
    S_TOKEN_TYPE Data_type=VAR_PTR;DATA *pReference=nullptr;bool bArray=false;
    uintptr_t pValue=0;int32_t lValue=0;float fValue=0;std::string sValue;ATTRIBUTES *AttributesClass=nullptr;
    void Error(const char *){++storm::android::scriptErrorEpoch;}
    void Set(const std::string &text){Data_type=VAR_STRING;sValue=text;}
    bool Convert(S_TOKEN_TYPE type);
};
struct Core {std::function<void()> refresh;void Event(const char *event,const char *format,const char *){
    assert(std::string_view(event)=="AndroidLandRefresh"&&std::string_view(format)=="s");if(refresh)refresh();}} core;
struct BIManCommandList {ATTRIBUTES *m_pARoot;std::string m_sCurrentCommandName;storm::android::LandCommandSource RefreshAndroidSource();};
// PRODUCTION_METHODS
using namespace storm::android;
static void populate(ATTRIBUTES &root){auto *m=root.child("AndroidLand");m->set("dialog","-1");m->set("officer","1");m->set("body","-1");m->set("reload","");m->set("pendingAllowed","1");m->set("version","1");}
static std::string action(LandCommandsState &s,const char *verb,const char *id=""){
    return "1\t"+std::to_string(s.sceneEpoch())+"\t"+std::to_string(s.revision())+"\t"+std::to_string(s.activityGeneration())+"\t"+verb+"\t"+id;
}
int main(){
    for(int32_t target:{-1,0,1234}){
        DATA direct;direct.pValue=static_cast<uintptr_t>(target);auto epoch=scriptErrorEpoch;
        assert(!direct.Convert(VAR_STRING)&&scriptErrorEpoch==epoch+1); // exact failing VAR_PTR->attribute conversion
        DATA stock;stock.pValue=static_cast<uintptr_t>(target);epoch=scriptErrorEpoch;
        assert(stock.Convert(VAR_INTEGER)&&stock.lValue==target&&stock.Convert(VAR_STRING));
        assert(stock.sValue==std::to_string(target)&&scriptErrorEpoch==epoch); // stock int-local boundary
    }
    ATTRIBUTES root;BIManCommandList list{&root,{}};
    core.refresh=[&]{populate(root);};assert(list.RefreshAndroidSource()==LandCommandSource::Ready);
    // Native clears old metadata even if the handler disappears before its first statement.
    core.refresh={};assert(list.RefreshAndroidSource()==LandCommandSource::Missing&&!root.GetAttributeClass("AndroidLand"));
    for(const char *missing:{"version","dialog","officer","body","reload","pendingAllowed"}){
        populate(root);core.refresh=[&]{populate(root);auto *m=root.GetAttributeClass("AndroidLand");m->DeleteAttributeClassX(m->GetAttributeClass(missing));};
        assert(list.RefreshAndroidSource()==LandCommandSource::Missing&&!root.GetAttributeClass("AndroidLand"));
    }
    for(const char *bad:{"","-2","2147483648","12junk"}){
        core.refresh=[&]{populate(root);root.GetAttributeClass("AndroidLand")->set("dialog",bad);};assert(list.RefreshAndroidSource()==LandCommandSource::Missing);
    }
    core.refresh=[&]{populate(root);root.GetAttributeClass("AndroidLand")->set("pendingAllowed","true");};assert(list.RefreshAndroidSource()==LandCommandSource::Missing);
    core.refresh=[&]{populate(root);root.GetAttributeClass("AndroidLand")->set("reload","bad\nlocator");};assert(list.RefreshAndroidSource()==LandCommandSource::Missing);
    // A logged error may not abort the interpreter. Even all fields plus a late
    // version1 marker are insufficient if any part of this synchronous event failed.
    core.refresh=[&]{DATA old;old.pValue=123;assert(!old.Convert(VAR_STRING));populate(root);};
    assert(list.RefreshAndroidSource()==LandCommandSource::Missing&&!root.GetAttributeClass("AndroidLand"));
    core.refresh=[&]{populate(root);DATA late;assert(!late.Convert(VAR_STRING));};assert(list.RefreshAndroidSource()==LandCommandSource::Missing);
    core.refresh=[&]{DATA value;value.pValue=UINTPTR_MAX;assert(value.Convert(VAR_INTEGER)&&value.Convert(VAR_STRING));populate(root);root.GetAttributeClass("AndroidLand")->set("dialog",value.sValue);};
    assert(list.RefreshAndroidSource()==LandCommandSource::Ready); // previous failures do not poison a successful fresh event
    core.refresh=[&]{populate(root);root.GetAttributeClass("AndroidLand")->set("pendingAllowed","0");};assert(list.RefreshAndroidSource()==LandCommandSource::Forbidden);
    LandCommandsState state;state.resetActivity();LandCommandsFrame f;f.available=f.active=true;f.ownerIdentity="list";f.selectedItemId="i0";
    f.items={{"i0","command","Talk","BI_DialogStart","target",true,1},{"i1","cancel","Cancel","","cancel",true,1}};
    state.update("scene",f,10);assert(state.offer(action(state,"ack","1"),11));auto oldAck=action(state,"ack","1"),oldAction=action(state,"activateItem","i0");assert(state.offer(oldAction,12));
    core.refresh=[&]{populate(root);DATA invalid;assert(!invalid.Convert(VAR_STRING));};
    assert(!state.releaseUnavailableSource(list.RefreshAndroidSource())); // Missing releases only Android, never requests native Close
    assert(!state.take(13)&&!state.suppressStock(13)&&!state.offer(oldAck,13)&&!state.offer(oldAction,13));
    core.refresh=[&]{populate(root);};assert(list.RefreshAndroidSource()==LandCommandSource::Ready);
    state.update("scene",f,14);assert(!state.offer(oldAck,14)&&!state.offer(oldAction,14));
    std::cout<<"PASS production DATA conversion and refresh admission: int boundary, full fresh metadata, missing/partial/malformed fields, nonfatal errors before/after version1, recovery, Forbidden distinction, stale leases rejected\n";
}
