#include "internal.hpp"
#include "memory.hpp"
#include <cstdlib>
#include <cstring>
#include <sstream>
namespace storm_archive {
static std::string quote(const std::string &s) {
 std::string out="\"";
 for(unsigned char c:s) {
  if(c=='"'||c=='\\'){out+='\\';out+=c;}
  else if(c<32){const char *hex="0123456789abcdef";out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
  else out+=c;
 }
 return out+'"';
}
void Manifest::add(const std::string &path,uint64_t size) {
 if(path.empty())return;
 has_resources|=path.rfind("RESOURCE/",0)==0;
 has_program|=path.rfind("PROGRAM/",0)==0;
 has_engine_ini|=path=="engine.ini";
 unpacked_bytes+=size;files.emplace_back(path,size);
}
std::string Manifest::json() const {
 std::ostringstream out;out.exceptions(std::ios::badbit|std::ios::failbit);out<<"{\"format\":"<<quote(format)<<",\"installer_version\":"<<quote(version)
  <<",\"metadata_only\":true,\"compatible_structure\":"<<(has_resources&&has_program&&has_engine_ini?"true":"false")
  <<",\"has_resource\":"<<(has_resources?"true":"false")<<",\"has_program\":"<<(has_program?"true":"false")
  <<",\"has_engine_ini\":"<<(has_engine_ini?"true":"false")<<",\"unpacked_bytes\":"<<unpacked_bytes
  <<",\"file_count\":"<<files.size()<<",\"required_volumes\":[";
 for(size_t i=0;i<volumes.size();++i){if(i)out<<',';out<<quote(volumes[i]);}
 out<<"],\"files\":[";
 for(size_t i=0;i<files.size();++i){if(i)out<<',';out<<"{\"path\":"<<quote(files[i].first)<<",\"size\":"<<files[i].second<<'}';}
 out<<"]}";return out.str();
}
}
extern "C" int storm_archive_manifest(const char *input,char **json,char *error,size_t cap) {
 if(json)*json=nullptr;
 auto fail=[&](int code,const char *message){if(error&&cap){strncpy(error,message,cap-1);error[cap-1]=0;}return code;};
 try {
  if(!json)return fail(STORM_ARCHIVE_INVALID,"Missing manifest output");
  auto path=storm_archive::checked_input(input);char format[64]{};
  int code=storm_archive_inspect(input,format,sizeof(format),error,cap);if(code)return code;
  auto manifest=strcmp(format,"inno")==0?storm_archive::inno_manifest(path):storm_archive::archive_manifest(path);
  if(storm_archive::allocation_failed())throw std::bad_alloc();
  auto text=manifest.json();*json=static_cast<char*>(storm_archive::allocate_result(text.size()+1));if(!*json)throw std::bad_alloc();
  memcpy(*json,text.c_str(),text.size()+1);
  if(!manifest.has_resources||!manifest.has_program||!manifest.has_engine_ini)
   return fail(STORM_ARCHIVE_NO_GAME_DATA,"Metadata does not contain engine.ini, RESOURCE and PROGRAM");
  return fail(STORM_ARCHIVE_OK,"");
 }catch(const storm_archive::Failure &e){return fail(storm_archive::allocation_failed()?STORM_ARCHIVE_LIMIT:e.status,e.what());}
 catch(const std::bad_alloc &){return fail(STORM_ARCHIVE_LIMIT,"Manifest memory limit exceeded");}
 catch(const std::exception &e){return fail(storm_archive::allocation_failed()||strstr(e.what(),"limit exceeded")?STORM_ARCHIVE_LIMIT:STORM_ARCHIVE_INVALID,e.what());}
 catch(...){return fail(STORM_ARCHIVE_INVALID,"Unknown manifest error");}
}
extern "C" void storm_archive_free(char *json){free(json);}
