// Exercise the production JNI boundary with deterministic VM failures. Only the
// network/session implementation is substituted: no network or disk is involved.
#include <cstdlib>
#include <new>
#include <iostream>
#include <cstring>
static bool fail_next_allocation=false;
static bool fail_all_allocations=false;
void* operator new(std::size_t n) {
    if(fail_next_allocation||fail_all_allocations) { fail_next_allocation=false; throw std::bad_alloc(); }
    if(void* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
#include "../../android/torrent/jni.cpp"

static int starts=0,opens=0;
namespace storm::torrent {
struct Session::Impl {};
Session::Session(std::string,std::string,bool):impl(std::make_unique<Impl>()){++opens;}
Session::~Session()=default;
std::string Session::poll(){return "{}";}
void Session::start(std::vector<int> const&){++starts;}
void Session::pause(){}
std::string inspect_file(std::string const&){return "{}";}
}
namespace {
struct Fake {
    JNINativeInterface_ table{};
    JNIEnv env{&table};
    bool pending=false,violation=false,throw_after_chars=false;
    int calls=0,fail_at=0,refs=0,releases=0,acquires=0,throws=0;
    bool live[64]{};
    int next_ref=1;
    static Fake* current;
    static constexpr jchar chars[]={u'a',u'b',u'c',u'd',u'e',u'f',u'g',u'h',u'i',u'j',u'k',u'l',u'm',u'n',u'o',u'p',u'q',u'r',u's',u't',u'u',u'v',u'w',u'x',u'y',u'z'};
    bool step() {
        if(pending)violation=true;
        if(++calls==fail_at){pending=true;return false;}
        return true;
    }
    jobject object(){int id=next_ref++;live[id]=true;++refs;return reinterpret_cast<jobject>(static_cast<std::uintptr_t>(id));}
    static jboolean check(JNIEnv*){return current->pending;}
    static void remove(JNIEnv*,jobject ref){
        auto id=reinterpret_cast<std::uintptr_t>(ref);
        if(id==0||id>=64||!current->live[id]){current->violation=true;return;}
        current->live[id]=false;--current->refs;
    }
    static jbyteArray bytes(JNIEnv*,jsize){if(!current->step())return nullptr;return static_cast<jbyteArray>(current->object());}
    static void set_bytes(JNIEnv*,jbyteArray,jsize,jsize,jbyte const*){current->step();}
    static jclass find(JNIEnv*,char const*){if(!current->step())return nullptr;return static_cast<jclass>(current->object());}
    static jmethodID method(JNIEnv*,jclass c,char const*,char const*){if(!c)current->violation=true;if(!current->step())return nullptr;return reinterpret_cast<jmethodID>(1);}
    static jstring string(JNIEnv*,char const*){if(!current->step())return nullptr;return static_cast<jstring>(current->object());}
    static jobject construct(JNIEnv*,jclass c,jmethodID m,va_list){if(!c||!m)current->violation=true;if(!current->step())return nullptr;return current->object();}
    static jint throwing(JNIEnv*,jthrowable){if(current->pending)current->violation=true;current->pending=true;++current->throws;return 0;}
    static jint throwing_new(JNIEnv*,jclass c,char const*){if(!c)current->violation=true;if(!current->step())return -1;current->pending=true;++current->throws;return 0;}
    static jsize length(JNIEnv*,jstring){if(current->pending)current->violation=true;return sizeof(chars)/sizeof(chars[0]);}
    static jchar const* get_chars(JNIEnv*,jstring,jboolean*){
        if(!current->step())return nullptr;
        ++current->acquires;
        if(current->throw_after_chars)fail_next_allocation=true;
        return chars;
    }
    static void release_chars(JNIEnv*,jstring,jchar const*){++current->releases;}
    static jsize array_length(JNIEnv*,jarray){if(current->pending)current->violation=true;return 1;}
    static void get_ints(JNIEnv*,jintArray,jsize,jsize,jint* out){if(current->step())*out=0;}
    Fake(){
        current=this;table.ExceptionCheck=check;table.DeleteLocalRef=remove;
        table.NewByteArray=bytes;table.SetByteArrayRegion=set_bytes;table.FindClass=find;
        table.GetMethodID=method;table.NewStringUTF=string;table.NewObjectV=construct;
        table.Throw=throwing;table.ThrowNew=throwing_new;table.GetStringLength=length;table.GetStringChars=get_chars;
        table.ReleaseStringChars=release_chars;table.GetArrayLength=array_length;table.GetIntArrayRegion=get_ints;
    }
};
Fake* Fake::current=nullptr;
void require(bool value,char const* message){if(!value)throw std::runtime_error(message);}
void clean(Fake const& f){require(!f.violation,"JNI operation under pending exception or invalid local reference");require(f.refs==0,"Leaked JNI local reference");require(f.acquires==f.releases,"Leaked GetStringChars buffer");}
}
int main(){
    try{
        // All six fallible steps in UTF-8 String construction, including the copy.
        for(int i=1;i<=6;++i){Fake f;f.fail_at=i;require(!result(&f.env,"hello"),"Failed String construction must return null");require(f.pending&&f.calls==i,"Original VM exception must survive without further JNI work");clean(f);}
        {Fake f;auto value=result(&f.env,"hello");require(value&&!f.pending,"String success failed");f.env.DeleteLocalRef(value);clean(f);}
        // IOException construction has three additional lookup/construction steps.
        std::runtime_error error("failure");
        for(int i=1;i<=9;++i){Fake f;f.fail_at=i;fail(&f.env,error);require(f.pending&&f.calls==i&&f.throws==0,"VM failure must not be replaced by IOException");clean(f);}
        {Fake f;fail(&f.env,error);require(f.pending&&f.throws==1,"Native failure must translate to one IOException");clean(f);}
        {Fake f;f.pending=true;require(!result(&f.env,"ignored"),"Existing exception must skip result conversion");fail(&f.env,error);require(f.calls==0&&f.throws==0,"Pending exception must be preserved");clean(f);}
        const auto input=reinterpret_cast<jstring>(1);
        for(int i=1;i<=2;++i){Fake f;f.fail_at=i;const int before=opens;auto id=Java_org_stormengine_port_NativeTorrents_open(&f.env,nullptr,input,input);require(id==0&&f.pending&&opens==before,"String acquisition failure must prevent Session creation");clean(f);}
        {Fake f;f.throw_after_chars=true;bool caught=false;try{utf(&f.env,input);}catch(std::bad_alloc const&){caught=true;}require(caught,"Native string allocation fault was not exercised");clean(f);}
        {Fake f;std::runtime_error long_error("A filesystem exception with a long Unicode filename must survive native allocation failure during Java error conversion");fail_next_allocation=true;fail(&f.env,long_error);require(f.pending,"Error reporting allocation failure must remain inside JNI");clean(f);}
        for(int position=0;position<=2;++position){
            Fake f;f.fail_at=position;
            std::runtime_error long_error("A long preexisting native error with memory exhausted throughout its entire Java error reporting path");
            fail_all_allocations=true;fail(&f.env,long_error);fail_all_allocations=false;
            require(f.pending,"Persistent native OOM must yield a Java exception");
            require(f.throws==(position?0:1),"Fallback must preserve VM failures");clean(f);
        }
        // Use a live fake Session: the failure must not cross the native boundary.
        sessions.emplace(42,std::make_unique<storm::torrent::Session>("",""));
        {Fake f;f.fail_at=1;const int before=starts;Java_org_stormengine_port_NativeTorrents_start(&f.env,nullptr,42,reinterpret_cast<jintArray>(1));require(f.pending&&starts==before,"GetIntArrayRegion failure must not dispatch Session.start");clean(f);}
        {Fake f;const int before=starts;Java_org_stormengine_port_NativeTorrents_start(&f.env,nullptr,42,reinterpret_cast<jintArray>(1));require(!f.pending&&starts==before+1,"Successful selection must dispatch Session.start");clean(f);}
        sessions.clear();
        std::cout<<"PASS production JNI: 15 result/exception fault positions, pending preservation, local-reference cleanup, both input strings, native bad_alloc/character release, persistent OOM during error reporting, fallback VM failures, selection-copy dispatch guard\n";
        return 0;
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
