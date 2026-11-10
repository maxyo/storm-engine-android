#include "internal.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <vector>
#ifdef STORM_ARCHIVE_TESTING
static uint64_t simulated_free_bytes=0;
extern "C" void storm_archive_test_free_bytes(uint64_t bytes){simulated_free_bytes=bytes;}
#endif
namespace storm_archive {
Fd::~Fd() { if(value>=0) close(value); }
static void io(const char *what) {
 if(errno==ENOSPC||errno==EDQUOT) throw Failure(STORM_ARCHIVE_NO_SPACE,std::string(what)+": no space left on device");
 throw Failure(STORM_ARCHIVE_IO,std::string(what)+": "+strerror(errno));
}
uint64_t free_bytes(int fd) {
#ifdef STORM_ARCHIVE_TESTING
 if(simulated_free_bytes) return simulated_free_bytes;
#endif
 struct statvfs st{};
 if(fstatvfs(fd,&st)<0) io("Read free space");
 return static_cast<uint64_t>(st.f_bavail)*st.f_frsize;
}
std::string checked_input(const char *input) {
 if(!input || !*input) throw Failure(STORM_ARCHIVE_IO,"Missing input path");
 Fd fd(open(input,O_RDONLY|O_CLOEXEC|O_NOFOLLOW)); struct stat st{};
 if(fd.value<0 || fstat(fd.value,&st)<0) io("Open input");
 if(!S_ISREG(st.st_mode)) throw Failure(STORM_ARCHIVE_INVALID,"Input must be a regular local file");
 return input;
}
Context::Context(const char *output,const storm_archive_limits *l,storm_archive_progress cb,void *u)
 : root(open(output?output:"",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)),
 limits{32ULL<<30,4ULL<<30,200000,3600},callback(cb),userdata(u) {
 if(l) limits=*l;
 if(!limits.max_total_bytes||!limits.max_file_bytes||!limits.max_entries||!limits.max_seconds)
  throw Failure(STORM_ARCHIVE_LIMIT,"All extraction limits must be positive");
 if(!output)return;
 if(root.value<0) io("Open staging");
 DIR *dir=fdopendir(dup(root.value)); if(!dir) io("Read staging");
 bool empty=true; while(auto *e=readdir(dir)) if(strcmp(e->d_name,".")&&strcmp(e->d_name,"..")) {empty=false;break;}
 closedir(dir); if(!empty) throw Failure(STORM_ARCHIVE_INVALID,"Staging directory must be empty");
}
void Context::check(const std::string &path) {
 if(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-started).count()>=static_cast<int64_t>(limits.max_seconds))
  throw Failure(STORM_ARCHIVE_LIMIT,"Extraction time limit exceeded");
 bool cancel=staged?staged(userdata,stage,path.c_str(),total,stage_total,extracted):callback&&callback(userdata,path.c_str(),total,extracted);
 if(cancel) throw Failure(STORM_ARCHIVE_CANCELLED,"Import cancelled");
}
void Context::account(uint64_t n,const std::string &path) {
 if(n>limits.max_total_bytes-total) throw Failure(STORM_ARCHIVE_LIMIT,"Expanded data limit exceeded");
 total+=n; container_decoded+=n;
 // Bomb guard: decoded bytes of one container are bounded by its input size.
 if(max_ratio&&container_input&&container_decoded>max_ratio*container_input+(1ULL<<20))
  throw Failure(STORM_ARCHIVE_LIMIT,"Compression ratio exceeds 1:"+std::to_string(max_ratio)+" (possible archive bomb)");
 check(path);
}
std::string lower_path(std::string s) { for(char &c:s) if(c>='A'&&c<='Z') c+=32; return s; }
std::vector<std::string> path_parts(const std::string &raw) {
 std::string s=raw; std::replace(s.begin(),s.end(),'\\','/');
 if(s.empty()||s.size()>1024||s.front()=='/'||s.find(':')!=s.npos)
  throw Failure(STORM_ARCHIVE_INVALID,"Unsafe archive path");
 while(!s.empty()&&s.back()=='/') s.pop_back();
 std::vector<std::string> parts; size_t pos=0;
 while(pos<s.size()) {
  size_t end=s.find('/',pos); if(end==s.npos) end=s.size(); auto part=s.substr(pos,end-pos);
  if(part.empty()||part=="."||part==".."||part.size()>255||part.back()=='.'||part.back()==' ')
   throw Failure(STORM_ARCHIVE_INVALID,"Unsafe archive component");
  for(unsigned char c:part) if(c<32||c==127) throw Failure(STORM_ARCHIVE_INVALID,"Control character in archive path");
  parts.push_back(part); if(parts.size()>32) throw Failure(STORM_ARCHIVE_LIMIT,"Archive path too deep"); pos=end+1;
 }
 if(parts.empty()) throw Failure(STORM_ARCHIVE_INVALID,"Empty archive path");
 return parts;
}
std::string join_parts(const std::vector<std::string> &parts,size_t count) {
 std::string out; for(size_t i=0;i<count&&i<parts.size();++i) out+=parts[i]+"/"; return out;
}
Anchor anchor_at(const std::vector<std::string> &parts,size_t i) {
 auto key=lower_path(parts[i]); bool leaf=i+1==parts.size();
 if(!leaf&&key=="resource") return RESOURCE_DIR;
 if(!leaf&&key=="program") return PROGRAM_DIR;
 if(leaf&&key=="engine.ini") return ENGINE_INI;
 if(leaf&&key=="options") return OPTIONS_FILE;
 return NO_ANCHOR;
}
std::string root_output(const std::vector<std::string> &parts,size_t i) {
 auto kind=anchor_at(parts,i);
 std::string out=kind==RESOURCE_DIR?"RESOURCE":kind==PROGRAM_DIR?"PROGRAM":lower_path(parts[i]);
 for(size_t j=i+1;j<parts.size();++j) out+="/"+parts[j];
 auto ext=lower_path(parts.back()); auto dot=ext.rfind('.'); ext=dot==ext.npos?"":ext.substr(dot);
 static const std::set<std::string> blocked={".exe",".dll",".com",".bat",".cmd",".msi",".scr",".ps1",".sh",".so",".apk",".jar",".lnk"};
 return blocked.count(ext)?std::string{}:out;
}
std::string Context::select(const std::string &raw) {
 check(raw); if(++entries>limits.max_entries) throw Failure(STORM_ARCHIVE_LIMIT,"Too many archive entries");
 auto parts=path_parts(raw);
 if(root_selected) {
  // Exactly the discovered root: members of other roots or wrappers are skipped.
  auto depth=static_cast<size_t>(std::count(selected_root.begin(),selected_root.end(),'/'));
  if(parts.size()<=depth||join_parts(parts,depth)!=selected_root||anchor_at(parts,depth)==NO_ANCHOR) return {};
  return root_output(parts,depth);
 }
 for(size_t i=0;i<parts.size();++i) {
  if(anchor_at(parts,i)==NO_ANCHOR) continue;
  auto candidate=join_parts(parts,i);
  if(prefix_set && lower_path(candidate)!=lower_path(prefix)) throw Failure(STORM_ARCHIVE_INVALID,"Multiple game roots in archive");
  prefix=candidate;prefix_set=true;
  return root_output(parts,i);
 }
 return {};
}
int Context::create(const std::string &path) {
 if(!paths.insert(lower_path(path)).second) throw Failure(STORM_ARCHIVE_INVALID,"Duplicate output path: "+path);
 Fd parent(dup(root.value)); if(parent.value<0) io("Duplicate staging descriptor");
 size_t pos=0;
 while(true) {
  size_t end=path.find('/',pos); auto name=path.substr(pos,end==path.npos?end:end-pos);
  if(end==path.npos) {
   int fd=openat(parent.value,name.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
   if(fd<0) io("Create output");
   if(path.rfind("RESOURCE/",0)==0) resources=true;
   if(path.rfind("PROGRAM/",0)==0) programs=true;
   if(path=="engine.ini") engine=true;
   ++extracted; return fd;
  }
  if(mkdirat(parent.value,name.c_str(),0700)<0&&errno!=EEXIST) io("Create directory");
  int next=openat(parent.value,name.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
  if(next<0) io("Open output directory"); close(parent.value); parent.value=next; pos=end+1;
 }
}
void Context::write(int fd,const char *data,size_t n) {
 if(reserve) {
  // Stop before the device is full instead of failing in the middle of a write.
  written_since_space_check+=n;
  if(written_since_space_check>=(16ULL<<20)) {
   written_since_space_check=0;
   if(free_bytes(fd)<reserve+n) throw Failure(STORM_ARCHIVE_NO_SPACE,"Not enough free storage while writing");
  }
 }
 while(n) { auto done=::write(fd,data,n); if(done<0&&errno==EINTR) continue; if(done<=0) io("Write output");data+=done;n-=done; }
}
void Context::finish() {
 if(!resources||!programs||!engine) throw Failure(STORM_ARCHIVE_NO_GAME_DATA,"Archive must contain engine.ini, RESOURCE and PROGRAM game data");
 check();
}
static bool executable(const std::string &path) { Fd fd(open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW)); char b[2]{};return fd.value>=0&&read(fd.value,b,2)==2&&b[0]=='M'&&b[1]=='Z'; }
static std::string probe(const std::string &path) {
 if(executable(path)) {
  if(inno_probe(path)) return "inno";
  throw Failure(STORM_ARCHIVE_UNSUPPORTED_EXECUTABLE,"Unsupported executable installer; no code was run");
 }
 return archive_probe(path);
}
static void message(char *dest,size_t cap,const char *s) { if(dest&&cap) {strncpy(dest,s,cap-1);dest[cap-1]=0;} }
template<class F> int boundary(char *error,size_t cap,F f) {
 clear_allocation_failure();
 try {f();if(allocation_failed()){message(error,cap,"Archive memory limit exceeded");return STORM_ARCHIVE_LIMIT;}message(error,cap,"");return STORM_ARCHIVE_OK;}
 catch(const Failure &e) {message(error,cap,e.what());return allocation_failed()?STORM_ARCHIVE_LIMIT:e.status;}
 catch(const std::bad_alloc &) {message(error,cap,"Archive memory limit exceeded");return STORM_ARCHIVE_LIMIT;}
 catch(const std::exception &e) {message(error,cap,e.what());return allocation_failed()||strstr(e.what(),"limit exceeded")?STORM_ARCHIVE_LIMIT:STORM_ARCHIVE_INVALID;}
 catch(...) {message(error,cap,"Unknown archive failure");return STORM_ARCHIVE_INVALID;}
}
}
extern "C" int storm_archive_inspect(const char *input,char *format,size_t n,char *error,size_t cap) {
 return storm_archive::boundary(error,cap,[&]{storm_archive::message(format,n,storm_archive::probe(storm_archive::checked_input(input)).c_str());});
}
extern "C" int storm_archive_extract(const char *input,const char *output,const storm_archive_limits *l,storm_archive_progress p,void *u,char *error,size_t cap) {
 return storm_archive::boundary(error,cap,[&]{
  if(!output)throw storm_archive::Failure(STORM_ARCHIVE_IO,"Missing staging directory");
  auto path=storm_archive::checked_input(input);auto format=storm_archive::probe(path);
  storm_archive::Context ctx(output,l,p,u);
  if(format=="inno") storm_archive::inno_extract(path,ctx); else storm_archive::archive_extract({path},ctx);
  ctx.finish();
 });
}
