#include "storm_archive.h"
#include <jni.h>
#include <string>
#include <memory>
#include <stdexcept>
namespace {
// JNI Modified UTF-8 cannot represent standard UTF-8 supplementary characters.
// Convert at both boundaries through UTF-16, preserving emoji and CJK filenames.
jstring java_string(JNIEnv *env,const char *data) {
 std::u16string out;const auto *p=reinterpret_cast<const unsigned char*>(data);
 while(*p) {
  uint32_t c=*p++;unsigned n=0;
  if(c<0x80){}else if(c>=0xC2&&c<=0xDF){c&=31;n=1;}else if(c>=0xE0&&c<=0xEF){c&=15;n=2;}else if(c>=0xF0&&c<=0xF4){c&=7;n=3;}else {c=0xFFFD;}
  uint32_t minimum=n==1?0x80:n==2?0x800:n==3?0x10000:0;
  bool valid=true;
  for(unsigned i=0;i<n;++i){if((*p&0xC0)!=0x80){valid=false;break;}c=(c<<6)|(*p++&63);}
  if(!valid||c<minimum||c>0x10FFFF||(c>=0xD800&&c<=0xDFFF))c=0xFFFD;
  if(c<=0xFFFF)out.push_back(static_cast<char16_t>(c));
  else {c-=0x10000;out.push_back(static_cast<char16_t>(0xD800+(c>>10)));out.push_back(static_cast<char16_t>(0xDC00+(c&1023)));}
 }
 return env->NewString(reinterpret_cast<const jchar*>(out.data()),static_cast<jsize>(out.size()));
}
struct Text {
 std::string value;
 Text(JNIEnv *env,jstring s) {
  if(!s)return;auto size=env->GetStringLength(s);auto *p=env->GetStringChars(s,nullptr);if(!p)return;
  try {
   for(jsize i=0;i<size;++i){uint32_t c=p[i];
    if(c>=0xD800&&c<=0xDBFF&&i+1<size&&p[i+1]>=0xDC00&&p[i+1]<=0xDFFF)c=0x10000+((c-0xD800)<<10)+(p[++i]-0xDC00);
    else if(c>=0xD800&&c<=0xDFFF)c=0xFFFD;
    if(!c)throw std::invalid_argument("NUL in input path");
    if(c<128)value+=static_cast<char>(c);
    else if(c<2048){value+=static_cast<char>(0xC0|(c>>6));value+=static_cast<char>(0x80|(c&63));}
    else if(c<65536){value+=static_cast<char>(0xE0|(c>>12));value+=static_cast<char>(0x80|((c>>6)&63));value+=static_cast<char>(0x80|(c&63));}
    else {value+=static_cast<char>(0xF0|(c>>18));value+=static_cast<char>(0x80|((c>>12)&63));value+=static_cast<char>(0x80|((c>>6)&63));value+=static_cast<char>(0x80|(c&63));}
   }
  }catch(...){env->ReleaseStringChars(s,p);throw;}
  env->ReleaseStringChars(s,p);
 }
 const char *data()const{return value.c_str();}
};
jobjectArray result(JNIEnv *e,int code,const char *format,const char *error,const char *json=nullptr) {
 auto cls=e->FindClass("java/lang/String");if(!cls)return nullptr;
 auto array=e->NewObjectArray(json?4:3,cls,nullptr);e->DeleteLocalRef(cls);if(!array)return nullptr;
 const std::string number=std::to_string(code);const char *values[]={number.c_str(),format,error,json};
 for(int i=0;i<(json?4:3);++i){auto s=java_string(e,values[i]);if(!s)return nullptr;e->SetObjectArrayElement(array,i,s);e->DeleteLocalRef(s);}
 return array;
}
struct Callback {JNIEnv *env;jobject target;jmethodID method;};
int progress(void *data,const char *path,uint64_t bytes,uint64_t files) {
 auto &c=*static_cast<Callback*>(data);if(c.env->ExceptionCheck())return 1;
 auto text=java_string(c.env,path);if(!text)return 1;
 bool cancel=c.env->CallBooleanMethod(c.target,c.method,text,static_cast<jlong>(bytes),static_cast<jlong>(files));
 bool exception=c.env->ExceptionCheck();c.env->DeleteLocalRef(text);return cancel||exception;
}
template<class F> jobjectArray boundary(JNIEnv *env,F f) {
 try{return f();}
 catch(const std::bad_alloc &){auto cls=env->FindClass("java/lang/OutOfMemoryError");if(cls){env->ThrowNew(cls,"Native archive metadata memory limit exceeded");env->DeleteLocalRef(cls);}}
 catch(const std::exception &){auto cls=env->FindClass("java/lang/IllegalArgumentException");if(cls){env->ThrowNew(cls,"Invalid archive argument");env->DeleteLocalRef(cls);}}
 catch(...){auto cls=env->FindClass("java/lang/IllegalStateException");if(cls){env->ThrowNew(cls,"Native archive failure");env->DeleteLocalRef(cls);}}
 return nullptr;
}
}
extern "C" JNIEXPORT jobjectArray JNICALL
Java_org_stormengine_port_NativeArchives_inspect(JNIEnv *env,jclass,jstring input) {
 return boundary(env,[&]()->jobjectArray{
  Text path(env,input);if(env->ExceptionCheck())return nullptr;
  char format[64]{},error[2048]{};int code=storm_archive_inspect(path.data(),format,sizeof(format),error,sizeof(error));
  return result(env,code,format,error);
 });
}
extern "C" JNIEXPORT jobjectArray JNICALL
Java_org_stormengine_port_NativeArchives_extract(JNIEnv *env,jclass,jstring input,jstring staging,jlongArray bounds,jobject cb) {
 return boundary(env,[&]()->jobjectArray{
  Text path(env,input),output(env,staging);if(env->ExceptionCheck())return nullptr;
  storm_archive_limits limits{32ULL<<30,4ULL<<30,200000,3600};
  if(bounds) {
   if(env->GetArrayLength(bounds)!=4)return result(env,STORM_ARCHIVE_LIMIT,"","Expected four positive extraction limits");
   jlong values[4];env->GetLongArrayRegion(bounds,0,4,values);if(env->ExceptionCheck())return nullptr;
   for(auto value:values)if(value<=0)return result(env,STORM_ARCHIVE_LIMIT,"","Extraction limits must be positive");
   limits={static_cast<uint64_t>(values[0]),static_cast<uint64_t>(values[1]),static_cast<uint64_t>(values[2]),static_cast<uint64_t>(values[3])};
  }
  Callback callback{env,cb,nullptr};
  if(cb) {
   auto cls=env->GetObjectClass(cb);if(!cls)return nullptr;
   callback.method=env->GetMethodID(cls,"onProgress","(Ljava/lang/String;JJ)Z");env->DeleteLocalRef(cls);
   if(!callback.method)return nullptr;
  }
  char error[2048]{};int code=storm_archive_extract(path.data(),output.data(),&limits,cb?progress:nullptr,&callback,error,sizeof(error));
  if(env->ExceptionCheck())return nullptr;
  return result(env,code,"",error);
 });
}
extern "C" JNIEXPORT jobjectArray JNICALL
Java_org_stormengine_port_NativeArchives_manifest(JNIEnv *env,jclass,jstring input) {
 return boundary(env,[&]()->jobjectArray{
  Text path(env,input);if(env->ExceptionCheck())return nullptr;
  char format[64]{},error[2048]{};char *raw=nullptr;
  int code=storm_archive_manifest(path.data(),&raw,error,sizeof(error));
  std::unique_ptr<char,decltype(&storm_archive_free)> json(raw,storm_archive_free);
  char ignored[2048]{};storm_archive_inspect(path.data(),format,sizeof(format),ignored,sizeof(ignored));
  return result(env,code,format,error,json?json.get():"");
 });
}
