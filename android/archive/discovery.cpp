// Source-set discovery: every complete game root in loose folders, archives,
// Inno installers, multi-volume archives and nested containers, and
// extraction of exactly one chosen root. Nothing found in a source is ever run.
#include "internal.hpp"
#include "memory.hpp"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace storm_archive {
namespace {
constexpr size_t kHead=32774;              // ISO 9660 descriptor magic ends at 32774.
constexpr uint64_t kMetadataBytes=256*1024; // Version header / language.ini.
constexpr size_t kMaxRoots=4096, kMaxCandidates=64, kMaxIssues=256, kMaxPartial=8;
constexpr uint64_t kMaxDepth=3;

std::string normal(const std::vector<std::string> &parts) {
 std::string out;for(size_t i=0;i<parts.size();++i){if(i)out+='/';out+=parts[i];}return out;
}
std::string basename_of(const std::string &path){auto p=path.rfind('/');return p==path.npos?path:path.substr(p+1);}
std::string dirname_of(const std::string &path){auto p=path.rfind('/');return p==path.npos?std::string{}:path.substr(0,p);}
std::string child(const std::string &dir,const std::string &name){return dir.empty()?name:name.empty()?dir:dir+"/"+name;}
bool digits(const std::string &s){return !s.empty()&&std::all_of(s.begin(),s.end(),[](char c){return c>='0'&&c<='9';});}
bool ends_with(const std::string &s,const std::string &suffix){return s.size()>=suffix.size()&&s.compare(s.size()-suffix.size(),suffix.size(),suffix)==0;}

// Text in old game files is often Windows-1251; JSON carries UTF-8.
bool valid_utf8(const std::string &s) {
 for(size_t i=0;i<s.size();) {
  auto c=static_cast<unsigned char>(s[i]);unsigned n=c<0x80?0:c>=0xC2&&c<=0xDF?1:c>=0xE0&&c<=0xEF?2:c>=0xF0&&c<=0xF4?3:9;
  if(n==9||i+n>=s.size()+(n?0:1))return false;
  for(unsigned k=1;k<=n;++k)if((static_cast<unsigned char>(s[i+k])&0xC0)!=0x80)return false;
  i+=n+1;
 }
 return true;
}
std::string utf8_text(const std::string &s) {
 if(valid_utf8(s))return s;
 static const unsigned short high[64]={0x402,0x403,0x201A,0x453,0x201E,0x2026,0x2020,0x2021,0x20AC,0x2030,0x409,0x2039,0x40A,0x40C,0x40B,0x40F,
  0x452,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,'?',0x2122,0x459,0x203A,0x45A,0x45C,0x45B,0x45F,
  0xA0,0x40E,0x45E,0x408,0xA4,0x490,0xA6,0xA7,0x401,0xA9,0x404,0xAB,0xAC,0xAD,0xAE,0x407,
  0xB0,0xB1,0x406,0x456,0x491,0xB5,0xB6,0xB7,0x451,0x2116,0x454,0xBB,0x458,0x405,0x455,0x457};
 std::string out;
 for(unsigned char c:s) {
  unsigned u=c<0x80?c:c>=0xC0?0x410+(c-0xC0):high[c-0x80];
  if(u<0x80)out+=static_cast<char>(u);
  else if(u<0x800){out+=static_cast<char>(0xC0|(u>>6));out+=static_cast<char>(0x80|(u&63));}
  else{out+=static_cast<char>(0xE0|(u>>12));out+=static_cast<char>(0x80|((u>>6)&63));out+=static_cast<char>(0x80|(u&63));}
 }
 return out;
}
std::string trimmed(std::string s) {
 auto space=[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n';};
 while(!s.empty()&&space(s.back()))s.pop_back();
 size_t i=0;while(i<s.size()&&space(s[i]))++i;s=s.substr(i);
 if(s.size()>128)s.resize(128);
 return s;
}
// #define VERSION_NUMBER2 "ver. 1.7.3 (26.11.21) x64" in PROGRAM/_LSC_on_off.h.
std::string define_string(const std::string &text,const std::string &name) {
 for(size_t at=text.find(name);at!=text.npos;at=text.find(name,at+1)) {
  auto line=text.rfind('\n',at);line=line==text.npos?0:line+1;
  if(text.compare(line,7,"#define")!=0)continue;
  auto open=text.find('"',at),eol=text.find('\n',at);
  if(open==text.npos||(eol!=text.npos&&open>eol))continue;
  auto close=text.find('"',open+1);if(close==text.npos||(eol!=text.npos&&close>eol))continue;
  return trimmed(utf8_text(text.substr(open+1,close-open-1)));
 }
 return {};
}
// defaultLanguage = russian in RESOURCE/INI/texts/language.ini (";" comments).
std::string default_language(const std::string &text) {
 std::istringstream in(text);std::string line;
 while(std::getline(in,line)) {
  auto key=lower_path(trimmed(line));
  if(key.rfind("defaultlanguage",0)!=0)continue;
  auto eq=key.find('=');if(eq==key.npos)continue;
  auto value=trimmed(key.substr(eq+1));auto comment=value.find(';');if(comment!=value.npos)value=trimmed(value.substr(0,comment));
  return utf8_text(value);
 }
 return {};
}
bool patch_marker(const std::string &name) {
 auto l=lower_path(name);
 for(const char *word:{"patch","update","hotfix","fix_","_fix","-fix","fix-"})if(l.find(word)!=l.npos)return true;
 return false;
}

struct RootStats {
 bool engine=false,program=false,resource=false;
 uint64_t files=0,bytes=0;
 std::set<std::string> languages;
 std::string version_member,language_member;
 bool complete()const{return engine&&program&&resource&&files;}
};
// Groups the members of one listing (folder, archive or installer) by every
// prefix that is directly followed by a game anchor. Only prefixes holding
// engine.ini, PROGRAM/ and RESOURCE/ together are game roots.
struct Catalog {
 std::map<std::string,RootStats> roots;
 std::map<std::string,std::vector<std::string>> markers;
 void add(const std::vector<std::string> &parts,uint64_t size) {
  for(size_t i=0;i<parts.size();++i) {
   auto kind=anchor_at(parts,i);if(kind==NO_ANCHOR)continue;
   auto prefix=join_parts(parts,i);auto found=roots.find(prefix);
   if(found==roots.end()) {
    if(roots.size()>=kMaxRoots)throw Failure(STORM_ARCHIVE_LIMIT,"Too many potential game roots in one container");
    found=roots.emplace(prefix,RootStats{}).first;
   }
   auto &root=found->second;
   if(root_output(parts,i).empty())continue;
   ++root.files;root.bytes+=size;
   root.engine|=kind==ENGINE_INI;root.program|=kind==PROGRAM_DIR;root.resource|=kind==RESOURCE_DIR;
   size_t rest=parts.size()-i;
   if(kind==RESOURCE_DIR&&rest>=4&&lower_path(parts[i+1])=="ini"&&lower_path(parts[i+2])=="texts") {
    if(rest==4&&lower_path(parts[i+3])=="language.ini")root.language_member=normal(parts);
    else if(rest>=5&&root.languages.size()<16)root.languages.insert(lower_path(parts[i+3]));
   }
   if(kind==PROGRAM_DIR&&rest==2&&lower_path(parts[i+1])=="_lsc_on_off.h")root.version_member=normal(parts);
  }
  if(patch_marker(parts.back())&&markers.size()<4096) {
   auto &names=markers[join_parts(parts,parts.size()-1)];if(names.size()<8)names.push_back(parts.back());
  }
 }
 bool inside_complete(const std::string &path)const {
  for(const auto &[prefix,root]:roots)if(root.complete()&&path.compare(0,prefix.size(),prefix)==0)return true;
  return false;
 }
 static bool game_data(const std::vector<std::string> &parts) {
  for(size_t i=0;i<parts.size();++i)if(anchor_at(parts,i)!=NO_ANCHOR)return true;
  return false;
 }
};

struct Candidate {
 std::string kind,format,rel,root,title,version,language;
 char base='s';
 std::vector<std::string> location,markers,missing;
 std::set<std::string> languages;
 uint64_t files=0,bytes=0;
};
struct Issue {std::vector<std::string> location;int status;std::string error;std::vector<std::string> missing;};
struct Partial {std::vector<std::string> location;std::string root;bool engine,program,resource;};

std::string hex(const std::string &s){static const char *h="0123456789abcdef";std::string r;for(unsigned char c:s){r+=h[c>>4];r+=h[c&15];}return r;}
std::string unhex(const std::string &s) {
 auto value=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
 if(s.size()%2)throw Failure(STORM_ARCHIVE_INVALID,"Invalid game root id");
 std::string out;
 for(size_t i=0;i<s.size();i+=2){int a=value(s[i]),b=value(s[i+1]);if(a<0||b<0)throw Failure(STORM_ARCHIVE_INVALID,"Invalid game root id");out+=static_cast<char>(a*16+b);}
 return out;
}
// Opaque id: everything extraction needs, re-validated against the source.
std::string candidate_id(const Candidate &c) {
 std::string raw="2";raw+='\0';raw+=c.kind;raw+='\0';raw+=c.base;raw+='\0';raw+=c.rel;raw+='\0';raw+=c.root;
 raw+='\0';raw+=std::to_string(c.files);raw+='\0';raw+=std::to_string(c.bytes);
 return hex(raw);
}

struct LocalFile {std::string rel;uint64_t size;};
void read_head(const std::string &path,std::string &head) {
 Fd fd(open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW));head.assign(kHead,'\0');
 if(fd.value<0)throw Failure(STORM_ARCHIVE_IO,"Cannot read "+basename_of(path));
 size_t got=0;
 while(got<kHead){auto n=read(fd.value,&head[got],kHead-got);if(n<0&&errno==EINTR)continue;if(n<0)throw Failure(STORM_ARCHIVE_IO,"Cannot read "+basename_of(path));if(!n)break;got+=n;}
 head.resize(got);
}
std::string read_small(const std::string &path) {
 Fd fd(open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW));if(fd.value<0)return {};
 std::string text(kMetadataBytes,'\0');size_t got=0;
 while(got<text.size()){auto n=read(fd.value,&text[got],text.size()-got);if(n<0&&errno==EINTR)continue;if(n<=0)break;got+=n;}
 text.resize(got);return text;
}
bool exists_regular(const std::string &path){struct stat st{};return lstat(path.c_str(),&st)==0&&S_ISREG(st.st_mode);}

// Volumes of one logical archive, found by name next to its first volume.
struct Family {std::vector<std::string> volumes,missing;bool split_zip=false;};
std::string numbered(const std::string &prefix,unsigned n,size_t width,const std::string &suffix) {
 auto text=std::to_string(n);while(text.size()<width)text="0"+text;return prefix+text+suffix;
}
// Groups name with the other volumes of its logical archive in names (one
// directory). primary is true for the first present volume, which stands for
// the whole family; missing lists absent volumes that can be named.
bool family_of(const std::string &name,const std::set<std::string> &names,Family &out,bool &primary) {
 auto lower=lower_path(name);
 auto lookup=[&](const std::string &wanted)->std::string{
  for(const auto &n:names)if(lower_path(n)==lower_path(wanted))return n;
  return {};
 };
 auto collect=[&](const std::string &prefix,size_t width,const std::string &suffix,unsigned first,const std::string &leading){
  std::map<unsigned,std::string> parts;
  for(const auto &n:names) {
   auto l=lower_path(n);
   if(l.size()!=prefix.size()+width+suffix.size()||l.compare(0,prefix.size(),lower_path(prefix))!=0||!ends_with(l,lower_path(suffix)))continue;
   auto number=l.substr(prefix.size(),width);if(!digits(number))continue;
   parts[static_cast<unsigned>(std::stoul(number))]=n;
  }
  if(!leading.empty()){auto lead=lookup(leading);if(lead.empty())out.missing.push_back(leading);else out.volumes.push_back(lead);}
  unsigned last=parts.empty()?first:parts.rbegin()->first;
  for(unsigned n=first;n<=last;++n) {
   auto found=parts.find(n);
   if(found==parts.end())out.missing.push_back(numbered(prefix,n,width,suffix));else out.volumes.push_back(found->second);
  }
  primary=!out.volumes.empty()&&out.volumes.front()==name;
 };
 // Raw byte-split set: game.7z.001, game.7z.002, ...
 if(lower.size()>4&&lower[lower.size()-4]=='.'&&digits(lower.substr(lower.size()-3))) {
  collect(name.substr(0,name.size()-3),3,"",1,"");return true;
 }
 // RAR 3+ naming: game.part1.rar, game.part2.rar, ...
 auto part=lower.rfind(".part");
 if(ends_with(lower,".rar")&&part!=lower.npos&&part+5<lower.size()-4&&digits(lower.substr(part+5,lower.size()-4-part-5))) {
  collect(name.substr(0,part+5),lower.size()-4-part-5,name.substr(name.size()-4),1,"");return true;
 }
 // Old RAR naming: game.rar, game.r00, game.r01, ...
 if(lower.size()>4&&lower.compare(lower.size()-4,2,".r")==0&&digits(lower.substr(lower.size()-2))) {
  auto stem=name.substr(0,name.size()-4);collect(name.substr(0,name.size()-2),2,"",0,stem+".rar");return true;
 }
 if(ends_with(lower,".rar")&&!lookup(name.substr(0,name.size()-4)+".r00").empty()) {
  collect(name.substr(0,name.size()-3)+"r",2,"",0,name);return true;
 }
 // Split ZIP (game.z01, ..., game.zip) is not readable by this build.
 if(lower.size()>4&&lower.compare(lower.size()-4,2,".z")==0&&digits(lower.substr(lower.size()-2))) {
  out.split_zip=true;primary=ends_with(lower,".z01")&&lookup(name.substr(0,name.size()-4)+".zip").empty();return true;
 }
 if(ends_with(lower,".zip")&&!lookup(name.substr(0,name.size()-4)+".z01").empty()) {
  out.split_zip=true;primary=true;return true;
 }
 out.volumes.push_back(name);primary=true;return false;
}
bool known_unsupported_archive(const std::string &name) {
 auto l=lower_path(name);auto dot=l.rfind('.');if(dot==l.npos)return false;auto ext=l.substr(dot);
 static const std::set<std::string> listed={".zip",".7z",".rar",".tar",".gz",".tgz",".bz2",".tbz",".xz",".txz",".iso",".cab",
  ".ace",".arj",".lzh",".lha",".zst",".wim",".dmg",".mdf",".mds",".nrg",".img",".msi"};
 return listed.count(ext)!=0;
}

struct Session {
 std::string only; // A single source file inside source (declared first: set while resolving source).
 std::string source,scratch;
 storm_archive_source_limits limits;
 Context budget;
 std::vector<Candidate> candidates;
 std::vector<Issue> issues;
 std::vector<Partial> partials;
 unsigned serial=0;uint64_t scratch_bytes=0;
 std::string failure_json;

 static std::string directory(const char *path,const char *what) {
  if(!path||!*path)throw Failure(STORM_ARCHIVE_IO,std::string("Missing ")+what+" directory");
  char resolved[PATH_MAX];
  if(!realpath(path,resolved))throw Failure(STORM_ARCHIVE_IO,std::string("Cannot open ")+what+" directory");
  struct stat st{};if(stat(resolved,&st)<0||!S_ISDIR(st.st_mode))throw Failure(STORM_ARCHIVE_IO,std::string(what)+" is not a directory");
  return resolved;
 }
 // A source file stands for itself; its directory supplies only its volumes.
 static std::string resolve(const char *path,std::string &only) {
  if(!path||!*path)throw Failure(STORM_ARCHIVE_IO,"Missing source");
  char resolved[PATH_MAX];
  if(!realpath(path,resolved))throw Failure(STORM_ARCHIVE_IO,"Cannot open source");
  struct stat st{};if(stat(resolved,&st)<0)throw Failure(STORM_ARCHIVE_IO,"Cannot open source");
  if(S_ISDIR(st.st_mode))return resolved;
  if(!S_ISREG(st.st_mode))throw Failure(STORM_ARCHIVE_INVALID,"Source must be a directory or a regular file");
  std::string file=resolved;only=basename_of(file);return dirname_of(file).empty()?"/":dirname_of(file);
 }
 static storm_archive_limits legacy(const storm_archive_source_limits &l){return {l.max_total_bytes,l.max_file_bytes,l.max_entries,l.max_seconds};}
 static storm_archive_source_limits checked(const storm_archive_source_limits *l) {
  storm_archive_source_limits v=l?*l:storm_archive_source_limits{64ULL<<30,4ULL<<30,400000,3600,32ULL<<30,kMaxDepth,100,64ULL<<20};
  if(!v.max_total_bytes||!v.max_file_bytes||!v.max_entries||!v.max_seconds||!v.max_scratch_bytes||!v.max_ratio)
   throw Failure(STORM_ARCHIVE_LIMIT,"All source limits must be positive");
  if(v.max_depth<1||v.max_depth>kMaxDepth)throw Failure(STORM_ARCHIVE_LIMIT,"Nested container depth must be 1..3");
  return v;
 }
 Session(const char *src,const char *tmp,const char *password,const storm_archive_source_limits *l,storm_archive_stage_progress cb,void *user)
  :source(resolve(src,only)),scratch(directory(tmp,"scratch")),limits(checked(l)),budget(nullptr,nullptr,nullptr,user) {
  auto inside=[](const std::string &a,const std::string &b){return a==b||(a.size()>b.size()&&a.compare(0,b.size(),b)==0&&a[b.size()]=='/');};
  if(inside(source,scratch)||inside(scratch,source))throw Failure(STORM_ARCHIVE_INVALID,"Source and scratch directories must be separate");
  budget.limits=legacy(limits);budget.staged=cb;budget.max_ratio=limits.max_ratio;budget.reserve=limits.reserve_bytes;
  if(password)budget.password=password;
 }
 std::string base_path(char base)const{return base=='s'?source:scratch;}
 void issue(const std::vector<std::string> &location,int status,const std::string &error,std::vector<std::string> missing={}) {
  if(issues.size()<kMaxIssues)issues.push_back({location,status,error,std::move(missing)});
 }
 std::vector<std::string> with(std::vector<std::string> chain,const std::string &item){chain.push_back(item);return chain;}

 // Regular files under dir (relative paths), without following links.
 std::vector<LocalFile> walk(const std::string &dir,const std::vector<std::string> &chain) {
  std::vector<LocalFile> files;
  struct Pending {std::string rel;};
  std::vector<Pending> stack{{""}};
  while(!stack.empty()) {
   auto next=stack.back();stack.pop_back();
   if(std::count(next.rel.begin(),next.rel.end(),'/')>=32)throw Failure(STORM_ARCHIVE_LIMIT,"Source folders are nested too deeply");
   auto path=child(dir,next.rel);
   std::unique_ptr<DIR,int(*)(DIR*)> handle(nullptr,closedir);
   {int fd=open(path.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)throw Failure(STORM_ARCHIVE_IO,"Cannot list "+(next.rel.empty()?std::string("source"):next.rel));
    handle.reset(fdopendir(fd));if(!handle){close(fd);throw Failure(STORM_ARCHIVE_IO,"Cannot list source folder");}}
   std::vector<std::string> names;
   while(auto *e=readdir(handle.get()))if(strcmp(e->d_name,".")&&strcmp(e->d_name,".."))names.push_back(e->d_name);
   std::sort(names.begin(),names.end());
   for(const auto &name:names) {
    auto rel=child(next.rel,name);budget.check(rel);
    if(++budget.entries>budget.limits.max_entries)throw Failure(STORM_ARCHIVE_LIMIT,"Too many files in the source");
    struct stat st{};
    if(fstatat(dirfd(handle.get()),name.c_str(),&st,AT_SYMLINK_NOFOLLOW)<0)throw Failure(STORM_ARCHIVE_IO,"Cannot inspect "+rel);
    if(S_ISDIR(st.st_mode))stack.push_back({rel});
    else if(S_ISREG(st.st_mode))files.push_back({rel,static_cast<uint64_t>(st.st_size)});
    else issue(with(chain,rel),STORM_ARCHIVE_INVALID,"Links and special files are not read");
   }
  }
  std::sort(files.begin(),files.end(),[](const auto &a,const auto &b){return a.rel<b.rel;});
  return files;
 }
 // Regular files directly in dir (a single-file source and its siblings).
 std::vector<LocalFile> listing(const std::string &dir) {
  std::vector<LocalFile> files;
  std::unique_ptr<DIR,int(*)(DIR*)> handle(opendir(dir.c_str()),closedir);
  if(!handle)throw Failure(STORM_ARCHIVE_IO,"Cannot list the source folder");
  while(auto *e=readdir(handle.get())) {
   struct stat st{};
   if(fstatat(dirfd(handle.get()),e->d_name,&st,AT_SYMLINK_NOFOLLOW)==0&&S_ISREG(st.st_mode))files.push_back({e->d_name,static_cast<uint64_t>(st.st_size)});
  }
  std::sort(files.begin(),files.end(),[](const auto &a,const auto &b){return a.rel<b.rel;});
  return files;
 }
 void add_partials(const Catalog &catalog,const std::vector<std::string> &location) {
  for(const auto &[prefix,root]:catalog.roots)
   if(!root.complete()&&(root.engine||(root.program&&root.resource))&&partials.size()<kMaxPartial)
    partials.push_back({location,prefix,root.engine,root.program,root.resource});
 }
 Candidate candidate(const char *kind,const std::string &format,char base,const std::string &rel,const std::vector<std::string> &location,
                     const std::string &prefix,const RootStats &root,const Catalog &catalog) {
  if(candidates.size()>=kMaxCandidates)throw Failure(STORM_ARCHIVE_LIMIT,"Too many game copies in the source");
  Candidate c;c.kind=kind;c.format=format;c.base=base;c.rel=rel;c.location=location;c.root=prefix;
  c.files=root.files;c.bytes=root.bytes;c.languages=root.languages;
  auto markers=catalog.markers.find(prefix);if(markers!=catalog.markers.end())c.markers=markers->second;
  return c;
 }
 void apply_metadata(Candidate &c,const std::string &version_text,const std::string &language_text) {
  if(!version_text.empty()) {
   auto title=define_string(version_text,"VERSION_NUMBER1"),version=define_string(version_text,"VERSION_NUMBER2");
   if(!title.empty())c.title=title;
   if(!version.empty())c.version=version;
  }
  if(!language_text.empty())c.language=default_language(language_text);
 }

 void scan(char base,const std::string &dir_rel,const std::vector<std::string> &chain,unsigned depth) {
  budget.stage=STORM_ARCHIVE_STAGE_READING;
  auto dir=child(base_path(base),dir_rel);
  const bool single=base=='s'&&dir_rel.empty()&&!only.empty();
  auto files=single?listing(dir):walk(dir,chain);
  // Loose game roots.
  Catalog catalog;
  for(const auto &f:files) {
   if(single)break;
   try {catalog.add(path_parts(f.rel),f.size);}
   catch(const Failure &e){if(e.status!=STORM_ARCHIVE_INVALID)throw;issue(with(chain,f.rel),e.status,e.what());}
  }
  for(const auto &[prefix,root]:catalog.roots) {
   if(!root.complete())continue;
   auto c=candidate("folder","folder",base,dir_rel,chain,prefix,root,catalog);
   apply_metadata(c,root.version_member.empty()?"":read_small(child(dir,root.version_member)),
                  root.language_member.empty()?"":read_small(child(dir,root.language_member)));
   candidates.push_back(std::move(c));
  }
  add_partials(catalog,chain);
  // Installers, archives and their volumes outside the loose roots.
  std::map<std::string,std::set<std::string>> by_dir;
  for(const auto &f:files)if(!catalog.inside_complete(f.rel))by_dir[dirname_of(f.rel)].insert(basename_of(f.rel));
  for(const auto &[folder,names]:by_dir) {
   std::set<std::string> inno_volumes,claimed;
   for(const auto &name:names) {
    if(single&&name!=only)continue;
    Family family;bool primary=false;bool grouped=family_of(name,names,family,primary);
    auto rel=child(folder,name);auto location=with(chain,rel);
    if(!primary)continue; // Read together with the first volume of its family.
    if(family.split_zip){issue(location,STORM_ARCHIVE_UNSUPPORTED,"Split ZIP archives (.z01, .z02, …) are not supported; join them into one ZIP on a computer");continue;}
    if(!family.missing.empty()){issue(location,STORM_ARCHIVE_MISSING_VOLUME,"Parts of this multi-volume archive are missing",family.missing);continue;}
    std::string head;read_head(child(dir,rel),head);
    if(inno_volume_signature(head.data(),head.size())){inno_volumes.insert(name);continue;}
    if(head.size()>=2&&head[0]=='M'&&head[1]=='Z') {
     if(!probe_inno(child(dir,rel))){issue(location,STORM_ARCHIVE_UNSUPPORTED_EXECUTABLE,"Not an Inno Setup installer; it was not run");continue;}
     for(const auto &v:installer(base,child(dir_rel,rel),location,depth))claimed.insert(lower_path(v));
     continue;
    }
    if(!container_signature(head.data(),head.size())) {
     if(known_unsupported_archive(name))issue(location,STORM_ARCHIVE_UNSUPPORTED,"Unsupported archive format");
     continue;
    }
    std::vector<std::string> volumes;for(const auto &v:family.volumes)volumes.push_back(child(dir,child(folder,v)));
    container(base,child(dir_rel,rel),volumes,location,depth,grouped);
   }
   // Installer volumes without their setup executable.
   for(const auto &name:inno_volumes) {
    if(single)break;
    if(claimed.count(lower_path(name)))continue;
    auto dash=name.rfind('-');auto exe=(dash==name.npos?name.substr(0,name.rfind('.')):name.substr(0,dash))+".exe";
    if(names.count(exe))continue; // Reported with the executable itself.
    issue(with(chain,child(folder,name)),STORM_ARCHIVE_MISSING_VOLUME,"Installer volume without its setup executable",{exe});
   }
  }
 }
 bool probe_inno(const std::string &path) {
  try {return inno_probe(path);}
  catch(const Failure &e){if(e.status==STORM_ARCHIVE_LIMIT||e.status==STORM_ARCHIVE_CANCELLED)throw;return false;}
  catch(const std::bad_alloc &){throw;}
  catch(const std::exception &){return false;}
 }
 bool too_deep(const std::vector<std::string> &location,unsigned depth) {
  if(depth<=limits.max_depth)return false;
  issue(location,STORM_ARCHIVE_LIMIT,"Nested more than "+std::to_string(limits.max_depth)+" levels deep");
  return true;
 }
 // Returns the volume names the installer needs (to keep them from being reported).
 std::vector<std::string> installer(char base,const std::string &rel,const std::vector<std::string> &location,unsigned depth) {
  if(too_deep(location,depth))return {};
  auto path=child(base_path(base),rel);
  try {
   budget.check(rel);
   auto listing=inno_listing(path);
   if(listing.files.size()>budget.limits.max_entries-std::min(budget.entries,budget.limits.max_entries))throw Failure(STORM_ARCHIVE_LIMIT,"Too many installer entries");
   budget.entries+=listing.files.size();
   Catalog catalog;
   for(const auto &[file,size]:listing.files)catalog.add(path_parts(file),size);
   std::vector<std::string> missing;
   for(const auto &v:listing.volumes)if(!exists_regular(child(dirname_of(path),v)))missing.push_back(v);
   for(const auto &[prefix,root]:catalog.roots) {
    if(!root.complete())continue;
    auto c=candidate("inno","inno",base,rel,location,prefix,root,catalog);
    c.title=trimmed(utf8_text(listing.title));c.version=trimmed(utf8_text(listing.version));c.missing=missing;
    candidates.push_back(std::move(c));
   }
   add_partials(catalog,location);
   return listing.volumes;
  }catch(const Failure &e){recoverable(e,location,"inno");}
  catch(const std::bad_alloc &){throw;}
  catch(const std::exception &e){if(allocation_failed())throw std::bad_alloc();issue(location,STORM_ARCHIVE_INVALID,e.what());}
  return {};
 }
 void recoverable(const Failure &e,const std::vector<std::string> &location,const std::string &format) {
  if(e.status==STORM_ARCHIVE_ENCRYPTED) {
   bool zip=format=="zip";auto text=lower_path(e.what());
   bool supported=zip&&text.find("unsupported")==text.npos&&text.find("not supported")==text.npos&&text.find("crypto")==text.npos;
   std::ostringstream json;json<<"{\"encrypted\":{\"location\":[";
   for(size_t i=0;i<location.size();++i){if(i)json<<',';json<<json_quote(location[i]);}
   json<<"],\"password_supported\":"<<(supported?"true":"false")<<",\"password_given\":"<<(budget.password.empty()?"false":"true")<<"}}";
   failure_json=json.str();throw e;
  }
  if(e.status==STORM_ARCHIVE_LIMIT||e.status==STORM_ARCHIVE_CANCELLED||e.status==STORM_ARCHIVE_NO_SPACE||allocation_failed())throw e;
  issue(location,e.status,e.what());
 }
 std::string format_of(const std::string &head) {
  auto is=[&](const char *s,size_t n){return head.size()>=n&&!memcmp(head.data(),s,n);};
  if(is("PK",2))return "zip";if(is("7z\274\257\047\034",6))return "7z";if(is("Rar!",4))return "rar";
  return "archive";
 }

 struct Member {std::string path;uint64_t size;};
 struct ListVisitor:ArchiveVisitor {
  Catalog &catalog;std::vector<Member> &members;
  ListVisitor(Catalog &c,std::vector<Member> &m):catalog(c),members(m){}
  bool entry(const std::string &raw,uint64_t size)override {
   auto parts=path_parts(raw);catalog.add(parts,size);
   if(!Catalog::game_data(parts))members.push_back({normal(parts),size});
   return false;
  }
  bool chunk(const char*,size_t)override{return false;}
  void done()override{}
 };
 // Second pass: small metadata files and nested containers (written to scratch).
 struct ReadVisitor:ArchiveVisitor {
  Session &session;
  std::map<std::string,std::string> metadata;   // member -> text
  std::set<std::string> nested;                 // members to sniff
  std::string spill_dir;                        // relative to scratch, created lazily
  uint64_t remaining=0;                         // upper bound of bytes still to spill
  std::string current,head,target;bool sniffing=false,reading_metadata=false;Fd out;uint64_t size=0,written=0;
  std::vector<std::string> spilled;
  explicit ReadVisitor(Session &s):session(s){}
  bool entry(const std::string &raw,uint64_t declared)override {
   current=normal(path_parts(raw));size=declared;written=0;head.clear();
   reading_metadata=metadata.count(current)&&declared<=kMetadataBytes;
   sniffing=nested.count(current)!=0;
   if(sniffing)remaining-=std::min(remaining,declared);
   return reading_metadata||sniffing;
  }
  bool chunk(const char *data,size_t n)override {
   if(reading_metadata){metadata[current].append(data,n);return true;}
   if(out.value<0) {
    head.append(data,n);
    if(head.size()<kHead&&head.size()<size)return true;
    if(!decide())return false;
    return true;
   }
   write(data,n);return true;
  }
  void done()override {
   if(sniffing&&out.value<0&&head.size()==size&&size)decide();
   if(out.value>=0) {
    if(written!=size)throw Failure(STORM_ARCHIVE_INVALID,"Nested container is truncated: "+current);
    close(out.value);out.value=-1;spilled.push_back(current);
   }
   sniffing=reading_metadata=false;
  }
  bool decide() {
   const bool wanted=container_signature(head.data(),head.size())||inno_volume_signature(head.data(),head.size());
   if(!wanted)return false;
   session.budget.stage=STORM_ARCHIVE_STAGE_NESTED;session.budget.check(current);
   if(size>session.limits.max_scratch_bytes-std::min(session.scratch_bytes,session.limits.max_scratch_bytes))
    throw Failure(STORM_ARCHIVE_LIMIT,"Nested containers exceed the scratch limit");
   if(spill_dir.empty()) {
    spill_dir=std::to_string(++session.serial);
    if(mkdir((session.scratch+"/"+spill_dir).c_str(),0700)<0)throw Failure(STORM_ARCHIVE_IO,"Cannot create scratch folder");
   }
   out.value=session.create_scratch(spill_dir,current,size,remaining);
   session.scratch_bytes+=size;
   std::string pending;pending.swap(head);write(pending.data(),pending.size());
   return true;
  }
  void write(const char *data,size_t n) {
   if(n>size-written)throw Failure(STORM_ARCHIVE_INVALID,"Nested container exceeds its declared size");
   written+=n;session.budget.write(out.value,data,n);
  }
 };
 int create_scratch(const std::string &spill_dir,const std::string &member,uint64_t size,uint64_t remaining) {
  Fd parent(open((scratch+"/"+spill_dir).c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
  if(parent.value<0)throw Failure(STORM_ARCHIVE_IO,"Cannot open scratch folder");
  auto available=free_bytes(parent.value),needed=size+remaining+limits.reserve_bytes;
  if(available<size+limits.reserve_bytes) {
   failure_json="{\"no_space\":{\"step\":\"nested\",\"needed_bytes\":"+std::to_string(needed)+",\"available_bytes\":"+std::to_string(available)+"}}";
   throw Failure(STORM_ARCHIVE_NO_SPACE,"Not enough free storage to unpack nested archive "+basename_of(member));
  }
  auto parts=path_parts(member);
  for(size_t i=0;i+1<parts.size();++i) {
   if(mkdirat(parent.value,parts[i].c_str(),0700)<0&&errno!=EEXIST)throw Failure(STORM_ARCHIVE_IO,"Cannot create scratch folder");
   int next=openat(parent.value,parts[i].c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
   if(next<0)throw Failure(STORM_ARCHIVE_INVALID,"Conflicting nested paths: "+member);
   close(parent.value);parent.value=next;
  }
  int fd=openat(parent.value,parts.back().c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
  if(fd<0)throw Failure(errno==EEXIST?STORM_ARCHIVE_INVALID:STORM_ARCHIVE_IO,"Cannot write nested container "+member);
  return fd;
 }
 void container(char base,const std::string &rel,const std::vector<std::string> &volumes,const std::vector<std::string> &location,unsigned depth,bool multi) {
  if(too_deep(location,depth))return;
  std::string format="archive";
  try {
   std::string head;read_head(volumes.front(),head);format=format_of(head);
   budget.stage=STORM_ARCHIVE_STAGE_READING;budget.check(rel);
   Catalog catalog;std::vector<Member> members;
   ListVisitor list(catalog,members);format=archive_walk(volumes,budget,list);
   // Bomb guard on declared sizes before anything is decoded or written.
   uint64_t input=budget.container_input,declared=0;
   for(const auto &m:members)declared+=m.size;
   for(const auto &[prefix,root]:catalog.roots)if(root.complete())declared+=root.bytes;
   if(declared>limits.max_ratio*input+(1ULL<<20))
    throw Failure(STORM_ARCHIVE_LIMIT,"Declared contents exceed 1:"+std::to_string(limits.max_ratio)+" of the archive size (possible archive bomb)");
   ReadVisitor reader(*this);
   const bool random_access=format=="zip"||format=="iso9660"||format=="cab";
   std::vector<std::pair<std::string,const RootStats*>> found;
   for(const auto &[prefix,root]:catalog.roots) {
    if(!root.complete())continue;
    found.emplace_back(prefix,&root);
    if(random_access){if(!root.version_member.empty())reader.metadata[root.version_member];if(!root.language_member.empty())reader.metadata[root.language_member];}
   }
   for(const auto &m:members)if(m.size&&!catalog.inside_complete(m.path)){reader.nested.insert(m.path);reader.remaining+=m.size;}
   budget.stage_total=reader.remaining;
   if(!reader.metadata.empty()||!reader.nested.empty())archive_walk(volumes,budget,reader);
   budget.stage_total=0;
   for(const auto &[prefix,root]:found) {
    auto c=candidate("archive",format,base,rel,location,prefix,*root,catalog);
    apply_metadata(c,root->version_member.empty()?"":reader.metadata[root->version_member],root->language_member.empty()?"":reader.metadata[root->language_member]);
    candidates.push_back(std::move(c));
   }
   add_partials(catalog,location);
   if(!reader.spilled.empty())scan('x',reader.spill_dir,location,depth+1);
  }catch(const Failure &e) {
   if(multi&&e.status==STORM_ARCHIVE_INVALID) {
    // A multi-volume set that ends early is most often missing its last parts.
    issue(location,STORM_ARCHIVE_MISSING_VOLUME,std::string("The archive ends early; a later part may be missing (")+e.what()+")",{next_volume(basename_of(volumes.back()))});
    return;
   }
   recoverable(e,location,format);
  }
  catch(const std::bad_alloc &){throw;}
  catch(const std::exception &e){if(allocation_failed())throw std::bad_alloc();issue(location,STORM_ARCHIVE_INVALID,e.what());}
 }
 static std::string next_volume(const std::string &last) {
  // Increment the last run of digits keeping its width: x.7z.002 -> x.7z.003.
  auto end=last.find_last_of("0123456789");if(end==last.npos)return last;
  auto start=last.find_last_not_of("0123456789",end);start=start==last.npos?0:start+1;
  auto number=std::to_string(std::stoul(last.substr(start,end-start+1))+1);
  while(number.size()<end-start+1)number="0"+number;
  return last.substr(0,start)+number+last.substr(end+1);
 }

 void discover() {scan('s',"",{},0);if(allocation_failed())throw std::bad_alloc();}
 std::string json()const {
  std::ostringstream out;out.exceptions(std::ios::badbit|std::ios::failbit);
  auto list=[&](const auto &values){out<<'[';bool first=true;for(const auto &v:values){if(!first)out<<',';first=false;out<<json_quote(v);}out<<']';};
  out<<"{\"version\":2,\"candidates\":[";
  for(size_t i=0;i<candidates.size();++i) {
   const auto &c=candidates[i];if(i)out<<',';
   out<<"{\"id\":"<<json_quote(candidate_id(c))<<",\"kind\":"<<json_quote(c.kind)<<",\"format\":"<<json_quote(c.format)
      <<",\"location\":";list(c.location);
   out<<",\"root\":"<<json_quote(c.root)<<",\"title\":"<<json_quote(c.title)<<",\"version\":"<<json_quote(c.version)
      <<",\"languages\":";list(c.languages);
   out<<",\"default_language\":"<<json_quote(c.language)<<",\"file_count\":"<<c.files<<",\"unpacked_bytes\":"<<c.bytes
      <<",\"patch_markers\":";list(c.markers);
   out<<",\"ready\":"<<(c.missing.empty()?"true":"false")<<",\"missing_volumes\":";list(c.missing);out<<'}';
  }
  out<<"],\"issues\":[";
  for(size_t i=0;i<issues.size();++i) {
   const auto &e=issues[i];if(i)out<<',';
   out<<"{\"location\":";list(e.location);out<<",\"status\":"<<e.status<<",\"error\":"<<json_quote(e.error)<<",\"missing_volumes\":";list(e.missing);out<<'}';
  }
  out<<"],\"partial_roots\":[";
  for(size_t i=0;i<partials.size();++i) {
   const auto &p=partials[i];if(i)out<<',';
   out<<"{\"location\":";list(p.location);
   out<<",\"root\":"<<json_quote(p.root)<<",\"has_engine_ini\":"<<(p.engine?"true":"false")<<",\"has_program\":"<<(p.program?"true":"false")
      <<",\"has_resource\":"<<(p.resource?"true":"false")<<'}';
  }
  out<<"],\"scratch_bytes\":"<<scratch_bytes<<'}';
  return out.str();
 }

 // ---- Extraction of one chosen root ----
 void extract(const std::string &id,const char *staging) {
  std::vector<std::string> fields;std::string raw=unhex(id),field;
  for(char c:raw){if(c){field+=c;}else{fields.push_back(field);field.clear();}}
  fields.push_back(field);
  if(fields.size()!=7||fields[0]!="2"||fields[2].size()!=1||(fields[2][0]!='s'&&fields[2][0]!='x'))throw Failure(STORM_ARCHIVE_INVALID,"Invalid game root id");
  const auto &kind=fields[1];char base=fields[2][0];const auto &rel=fields[3];const auto &root=fields[4];
  if(!rel.empty())path_parts(rel);
  if(!root.empty())path_parts(root);
  if((!root.empty()&&root.back()!='/'))throw Failure(STORM_ARCHIVE_INVALID,"Invalid game root id");
  uint64_t files=std::stoull(fields[5]),bytes=std::stoull(fields[6]);
  auto l=legacy(limits);
  Context ctx(staging,&l,nullptr,budget.userdata);
  ctx.staged=budget.staged;ctx.stage=STORM_ARCHIVE_STAGE_INSTALL;ctx.stage_total=bytes;ctx.max_ratio=limits.max_ratio;
  ctx.reserve=limits.reserve_bytes;ctx.password=budget.password;ctx.root_selected=true;ctx.selected_root=root;
  auto available=free_bytes(ctx.root.value);
  if(available<bytes+limits.reserve_bytes) {
   failure_json="{\"no_space\":{\"step\":\"install\",\"needed_bytes\":"+std::to_string(bytes+limits.reserve_bytes)+",\"available_bytes\":"+std::to_string(available)+"}}";
   throw Failure(STORM_ARCHIVE_NO_SPACE,"Not enough free storage for the game files");
  }
  auto path=child(base_path(base),rel);
  if(kind=="folder")copy_folder(path,root,ctx);
  else if(kind=="inno")inno_extract(checked_input(path.c_str()),ctx);
  else if(kind=="archive") {
   std::set<std::string> names;auto folder=dirname_of(path);
   std::unique_ptr<DIR,int(*)(DIR*)> handle(opendir(folder.c_str()),closedir);
   if(!handle)throw Failure(STORM_ARCHIVE_IO,"Cannot list archive folder");
   while(auto *e=readdir(handle.get()))names.insert(e->d_name);
   Family family;bool primary=false;family_of(basename_of(path),names,family,primary);
   if(!primary||!family.missing.empty()||family.split_zip)throw Failure(STORM_ARCHIVE_MISSING_VOLUME,"Archive volumes changed since discovery");
   std::vector<std::string> volumes;for(const auto &v:family.volumes)volumes.push_back(child(folder,v));
   archive_extract(volumes,ctx);
  }
  else throw Failure(STORM_ARCHIVE_INVALID,"Invalid game root id");
  ctx.finish();
  if(ctx.extracted!=files)throw Failure(STORM_ARCHIVE_INVALID,"The source changed since it was read; read it again");
 }
 void copy_folder(const std::string &dir,const std::string &root,Context &ctx) {
  ctx.begin_container(0);
  auto top=child(dir,root.empty()?"":root.substr(0,root.size()-1));
  for(const auto &f:walk(top,{})) {
   auto output=ctx.select(root+f.rel);if(output.empty())continue;
   if(f.size>ctx.limits.max_file_bytes)throw Failure(STORM_ARCHIVE_LIMIT,"Game file exceeds limit: "+f.rel);
   Fd in(open(child(top,f.rel).c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC));if(in.value<0)throw Failure(STORM_ARCHIVE_IO,"Cannot open "+f.rel);
   Fd out(ctx.create(output));char buffer[65536];uint64_t copied=0;
   while(true) {
    auto n=read(in.value,buffer,sizeof(buffer));if(n<0&&errno==EINTR)continue;
    if(n<0)throw Failure(STORM_ARCHIVE_IO,"Cannot read "+f.rel);if(!n)break;
    if(static_cast<uint64_t>(n)>f.size-copied)throw Failure(STORM_ARCHIVE_INVALID,"Game file changed while copying: "+f.rel);
    copied+=n;ctx.account(n,output);ctx.write(out.value,buffer,n);
   }
   if(copied!=f.size)throw Failure(STORM_ARCHIVE_INVALID,"Game file changed while copying: "+f.rel);
  }
 }
};

int message(char *out,size_t n,int code,const char *text){if(out&&n){strncpy(out,text,n-1);out[n-1]=0;}return code;}
char *result_json(const std::string &text) {
 auto *p=static_cast<char*>(allocate_result(text.size()+1));if(!p)throw std::bad_alloc();
 memcpy(p,text.c_str(),text.size()+1);return p;
}
template<class F> int boundary(char **json,char *error,size_t cap,F f) {
 if(json)*json=nullptr;
 clear_allocation_failure();
 std::string failure;
 auto finish=[&](int code,const char *text){
  if(json&&!failure.empty()&&!*json){try{*json=result_json(failure);}catch(...){}}
  return message(error,cap,code,text);
 };
 try{if(!json)throw Failure(STORM_ARCHIVE_INVALID,"Missing result output");f(failure);if(allocation_failed())throw std::bad_alloc();return message(error,cap,0,"");}
 catch(const Failure &x){return finish(allocation_failed()?STORM_ARCHIVE_LIMIT:x.status,x.what());}
 catch(const std::bad_alloc &){return finish(STORM_ARCHIVE_LIMIT,"Archive memory limit exceeded");}
 catch(const std::exception &x){return finish(allocation_failed()||strstr(x.what(),"limit exceeded")?STORM_ARCHIVE_LIMIT:STORM_ARCHIVE_INVALID,x.what());}
 catch(...){return finish(STORM_ARCHIVE_INVALID,"Unknown archive failure");}
}
}
}
extern "C" int storm_archive_discover(const char *source,const char *scratch,const char *password,const storm_archive_source_limits *limits,
 storm_archive_stage_progress progress,void *userdata,char **json,char *error,size_t cap) {
 return storm_archive::boundary(json,error,cap,[&](std::string &failure){
  storm_archive::Session session(source,scratch,password,limits,progress,userdata);
  {
   // Discovery owns the scratch layout: it must start empty.
   std::unique_ptr<DIR,int(*)(DIR*)> dir(opendir(session.scratch.c_str()),closedir);
   if(!dir)throw storm_archive::Failure(STORM_ARCHIVE_IO,"Cannot open scratch directory");
   while(auto *e=readdir(dir.get()))if(strcmp(e->d_name,".")&&strcmp(e->d_name,".."))
    throw storm_archive::Failure(STORM_ARCHIVE_INVALID,"Scratch directory must be empty");
  }
  try{session.discover();}catch(...){failure=session.failure_json;throw;}
  *json=storm_archive::result_json(session.json());
 });
}
extern "C" int storm_archive_extract_root(const char *source,const char *scratch,const char *password,const char *id,const char *staging,
 const storm_archive_source_limits *limits,storm_archive_stage_progress progress,void *userdata,char **json,char *error,size_t cap) {
 return storm_archive::boundary(json,error,cap,[&](std::string &failure){
  if(!id||!*id||strlen(id)>16384)throw storm_archive::Failure(STORM_ARCHIVE_INVALID,"Invalid game root id");
  if(!staging)throw storm_archive::Failure(STORM_ARCHIVE_IO,"Missing staging directory");
  storm_archive::Session session(source,scratch,password,limits,progress,userdata);
  try{session.extract(id,staging);}catch(...){failure=session.failure_json;throw;}
 });
}
