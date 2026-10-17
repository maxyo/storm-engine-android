#include "torrent_session.hpp"
#include <jni.h>
#include <map>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace {
std::mutex lock;
std::map<jlong,std::unique_ptr<storm::torrent::Session>> sessions;
jlong next_id=1;
template<class T> struct Local {
    JNIEnv* env;
    T value;
    ~Local() { if(value) env->DeleteLocalRef(value); }
};
struct StringChars {
    JNIEnv* env;
    jstring value;
    const jchar* data;
    ~StringChars() { if(data) env->ReleaseStringChars(value,data); }
};
std::string utf(JNIEnv* env,jstring value) {
    if(!value) throw std::runtime_error("Missing string argument");
    // JNI modified UTF-8 is unsuitable for arbitrary paths. Convert Java UTF-16.
    auto n=env->GetStringLength(value); auto* p=env->GetStringChars(value,nullptr);
    if(!p) throw std::runtime_error("Cannot read Java string");
    StringChars chars{env,value,p};
    std::string s;
    for(int i=0;i<n;++i) {
        unsigned c=p[i];
        if(c>=0xd800&&c<=0xdbff && i+1<n && p[i+1]>=0xdc00&&p[i+1]<=0xdfff) c=0x10000+((c-0xd800)<<10)+(p[++i]-0xdc00);
        if(c==0 || (c>=0xd800&&c<=0xdfff)) throw std::runtime_error("Invalid string");
        if(c<0x80)s+=char(c);
        else if(c<0x800) { s+=char(0xc0|(c>>6)); s+=char(0x80|(c&63)); }
        else if(c<0x10000) { s+=char(0xe0|(c>>12)); s+=char(0x80|((c>>6)&63)); s+=char(0x80|(c&63)); }
        else { s+=char(0xf0|(c>>18)); s+=char(0x80|((c>>12)&63)); s+=char(0x80|((c>>6)&63)); s+=char(0x80|(c&63)); }
    }
    return s;
}
jstring result(JNIEnv* env,std::string const& s) {
    // Decode standard UTF-8 using Java, including non-BMP filenames.
    if(env->ExceptionCheck())return nullptr;
    if(s.size()>static_cast<size_t>(std::numeric_limits<jsize>::max()))throw std::length_error("Torrent response is too large");
    const auto size=static_cast<jsize>(s.size());
    Local<jbyteArray> bytes{env,env->NewByteArray(size)};
    if(!bytes.value)return nullptr;
    env->SetByteArrayRegion(bytes.value,0,size,reinterpret_cast<jbyte const*>(s.data()));
    if(env->ExceptionCheck())return nullptr;
    Local<jclass> cls{env,env->FindClass("java/lang/String")};
    if(!cls.value)return nullptr;
    auto ctor=env->GetMethodID(cls.value,"<init>","([BLjava/lang/String;)V");
    if(!ctor)return nullptr;
    Local<jstring> encoding{env,env->NewStringUTF("UTF-8")};
    if(!encoding.value)return nullptr;
    return static_cast<jstring>(env->NewObject(cls.value,ctor,bytes.value,encoding.value));
}
void fail(JNIEnv* env,std::exception const& e) noexcept {
    try {
        if(env->ExceptionCheck())return;
        Local<jstring> message{env,result(env,e.what())};
        if(!message.value)return;
        Local<jclass> cls{env,env->FindClass("java/io/IOException")};
        if(!cls.value)return;
        auto ctor=env->GetMethodID(cls.value,"<init>","(Ljava/lang/String;)V");
        if(!ctor)return;
        Local<jthrowable> exception{env,static_cast<jthrowable>(env->NewObject(cls.value,ctor,message.value))};
        if(exception.value)env->Throw(exception.value);
    } catch(...) {
        // Even copying e.what() can exhaust native memory. This fallback makes
        // no C++ allocation and uses only ASCII (valid JNI modified UTF-8).
        if(env->ExceptionCheck())return;
        Local<jclass> cls{env,env->FindClass("java/io/IOException")};
        if(cls.value)env->ThrowNew(cls.value,"Native torrent error (message unavailable)");
    }
}
storm::torrent::Session& get(jlong id) {
    auto it=sessions.find(id);if(it==sessions.end())throw std::runtime_error("Torrent session is closed");return *it->second;
}
}
extern "C" JNIEXPORT jstring JNICALL Java_org_stormengine_port_NativeTorrents_inspect(JNIEnv* env,jclass,jstring path) {
    try{return result(env,storm::torrent::inspect_file(utf(env,path)));}catch(std::exception const& e){fail(env,e);return nullptr;}
}
extern "C" JNIEXPORT jlong JNICALL Java_org_stormengine_port_NativeTorrents_open(JNIEnv* env,jclass,jstring source,jstring staging) {
    std::lock_guard<std::mutex> guard(lock);
    try{auto s=std::make_unique<storm::torrent::Session>(utf(env,source),utf(env,staging));auto id=next_id++;sessions.emplace(id,std::move(s));return id;}
    catch(std::exception const& e){fail(env,e);return 0;}
}
extern "C" JNIEXPORT jstring JNICALL Java_org_stormengine_port_NativeTorrents_poll(JNIEnv* env,jclass,jlong id) {
    std::lock_guard<std::mutex> guard(lock);
    try{return result(env,get(id).poll());}catch(std::exception const& e){fail(env,e);return nullptr;}
}
extern "C" JNIEXPORT void JNICALL Java_org_stormengine_port_NativeTorrents_start(JNIEnv* env,jclass,jlong id,jintArray files) {
    std::lock_guard<std::mutex> guard(lock);
    try{if(!files)throw std::runtime_error("Missing file selection");std::vector<int> ids(env->GetArrayLength(files));env->GetIntArrayRegion(files,0,ids.size(),ids.data());if(!env->ExceptionCheck())get(id).start(ids);}
    catch(std::exception const& e){fail(env,e);}
}
extern "C" JNIEXPORT void JNICALL Java_org_stormengine_port_NativeTorrents_pause(JNIEnv* env,jclass,jlong id) {
    std::lock_guard<std::mutex> guard(lock);
    try{get(id).pause();}catch(std::exception const& e){fail(env,e);}
}
extern "C" JNIEXPORT void JNICALL Java_org_stormengine_port_NativeTorrents_close(JNIEnv*,jclass,jlong id) {
    std::lock_guard<std::mutex> guard(lock);sessions.erase(id);
}
