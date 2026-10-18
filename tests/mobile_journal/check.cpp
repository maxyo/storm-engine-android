#include "mobile_widget.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <cstdarg>
#include <cstdint>
#include <stdexcept>

// Attribute and quest-reader services are boundaries; production code is included below.
struct ATTRIBUTES {
    std::string id, value;
    std::map<std::string,std::string> fields;
    std::vector<std::unique_ptr<ATTRIBUTES>> children;
    int GetAttributesNum() const { return children.size(); }
    ATTRIBUTES *GetAttributeClass(int i) { return i<0 || i>=int(children.size())?nullptr:children[i].get(); }
    ATTRIBUTES *GetAttributeClass(const char *id) { for(auto &p:children)if(p->id==id)return p.get();return nullptr; }
    const char *GetAttribute(const char *key) { auto at=fields.find(key);return at==fields.end()?nullptr:at->second.c_str(); }
    uint32_t GetAttributeAsDword(const char *key,uint32_t fallback) { auto p=GetAttribute(key);return p?std::stoul(p):fallback; }
    const char *GetThisName() const { return id.c_str(); }
    const char *GetThisAttr() const { return value.c_str(); }
    ATTRIBUTES &add(std::string name,std::string text={}) { children.push_back(std::make_unique<ATTRIBUTES>());auto &a=*children.back();a.id=name;a.value=text;return a; }
};
struct Reader {
    bool GetQuestTitle(const char *log,const char *id,std::string &out) {
        out=std::string(log)+" / Полный заголовок "+id;out.push_back(0);return std::string(id)!="missing";
    }
};
struct Owner { Reader reader;Reader *QuestFileReader(){return &reader;} } owner;
namespace storm::android { uint64_t mobileScreenRevision=17; }
struct Core {
    std::vector<int> activated;
    void Event(const char *event,const char *format,...) {
        va_list args;va_start(args,format);if(std::string(event)=="QuestActivate")activated.push_back(va_arg(args,int));va_end(args);
    }
} core;
struct CXI_QUESTTITLE {
    const char *m_nodeName="QUEST_TITLE";
    Owner *ptrOwner=&owner;
    struct {int top=1,left=1;} m_rect;
    std::vector<MobileWidget> m_mobileQuests;
    int m_mobileTop=0;
    bool m_bUse=true,m_bClickable=true,m_bLockedNode=false,m_bLockStatus=false;
    void RefreshMobileQuests(ATTRIBUTES *,int);
    void MobileCollect(std::vector<MobileWidget> &) const;
    bool MobileApply(int,const std::string &);
};
#define STORM_DELETE(ptr) do { delete ptr;ptr=nullptr; } while(false)
struct CXI_QUESTTEXTS {
    struct Line {
        std::string text;char *lineStr=nullptr;bool complete=false;Line *next=nullptr;int strNum=0;
        Line(const char *s):text(s){}
        Line *Add(const char *s,bool done){next=new Line(s);next->strNum=strNum+1;next->complete=done;return next;}
    };
    // Native drawing is outside this boundary; linked rows retain their own text.
    Line *m_listRoot=nullptr,*m_listCur=nullptr;
    int m_nAllTextStrings=0,m_idFont=0;
    std::string m_mobileText;
    const char *m_nodeName="QUEST_TEXT";Owner *ptrOwner=&owner;
    bool m_bUse=true;struct {int top=1,left=1;} m_rect;
    void StartQuestShow(ATTRIBUTES *,int);
    void MobileCollect(std::vector<MobileWidget> &) const;
    void GetStringListForQuestRecord(std::vector<std::string> &out,const char *text,const char *) const {out={"18 октября 2026",text};out.back().push_back(0);}
    bool GetLineNext(int,char *&in,char *out,int) const {
        if(!*in)return false;std::strcpy(out,in);in+=std::strlen(in);return true;
    }
    ~CXI_QUESTTEXTS(){while(m_listRoot){auto *next=m_listRoot->next;delete m_listRoot;m_listRoot=next;}}
    using STRING_DESCRIBER=Line;
};
#include "journal.inc"

int main(){
    ATTRIBUTES quests;for(int i=0;i<40;i++){auto &q=quests.add("quest"+std::to_string(i));q.fields["LogName"]="OtherLog";q.fields["Complete"]=i%2?"1":"0";}
    quests.add("missing");
    CXI_QUESTTITLE titles;titles.RefreshMobileQuests(&quests,12);
    std::vector<MobileWidget> widgets;titles.MobileCollect(widgets);
    assert(widgets.size()==40);assert(widgets.back().index==39&&widgets.back().rowId=="quest39");
    assert(widgets.back().label=="OtherLog / Полный заголовок quest39"&&widgets.back().value=="1");
    assert(widgets[0].actions==std::vector<std::string>{"open"});
    assert(titles.MobileApply(39,"open@17:quest39")&&core.activated.back()==27);
    assert(titles.MobileApply(0,"open@17:quest0")&&core.activated.back()==-12);
    assert(!titles.MobileApply(39,"open@16:quest39")&&!titles.MobileApply(39,"open@17:quest0"));
    assert(!titles.MobileApply(-1,"press")&&!titles.MobileApply(90,"open@17:quest90"));
    titles.m_bLockedNode=true;assert(!titles.MobileApply(0,"open@17:quest0"));widgets.clear();titles.MobileCollect(widgets);assert(!widgets[0].enabled&&widgets[0].actions.empty());
    titles.m_bUse=false;widgets.clear();titles.MobileCollect(widgets);assert(widgets.empty());
    titles.m_bUse=true;titles.RefreshMobileQuests(nullptr,0);widgets.clear();titles.MobileCollect(widgets);assert(widgets.size()==1&&widgets[0].label.empty());

    // Execute production StartQuestShow: reverse chronological records, dates,
    // paragraph boundaries and full text independent of native line wrapping.
    ATTRIBUTES log;auto &quest=log.add("story");auto &records=quest.add("Text");records.add("old","Старая запись");records.add("new","Новая запись целиком");
    CXI_QUESTTEXTS text;text.StartQuestShow(&log,0);widgets.clear();text.MobileCollect(widgets);
    assert(widgets.size()==1&&widgets[0].label=="18 октября 2026\nНовая запись целиком\n\n18 октября 2026\nСтарая запись");
    text.StartQuestShow(nullptr,0);assert(text.m_mobileText.empty());
    text.m_bUse=false;widgets.clear();text.MobileCollect(widgets);assert(widgets.empty());
    std::cout<<"PASS actual journal full list, original script index, stale/locked actions and complete dated records\n";
}
