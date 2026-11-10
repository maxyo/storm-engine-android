#include "internal.hpp"
#include "memory.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <memory>
#include <exception>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
namespace storm_archive {
namespace {
int classify(const std::string &message,int fallback) {
 auto text=lower_path(message);
 return text.find("encrypt")!=text.npos||text.find("passphrase")!=text.npos||text.find("password")!=text.npos?STORM_ARCHIVE_ENCRYPTED:fallback;
}
struct ArchiveDeleter {
 CodecArena *arena;
 void operator()(archive *value)const noexcept {
  if(!value||arena->poisoned)return;
  try {archive_read_free(value);}
  catch(...){mark_allocation_failure();}
 }
};
struct Reader;
// One libarchive data node per volume. Multi-volume RAR and raw byte-split
// sets (.001, .002, ...) are read through libarchive's node switching.
struct Volume {Reader *owner;Fd fd;};
struct Reader {
 CodecArena arena;
 std::unique_ptr<archive,ArchiveDeleter> owner{nullptr,ArchiveDeleter{&arena}};
 archive *value=nullptr;
 std::vector<std::unique_ptr<Volume>> volumes;
 Context *context;
 char buffer[65536];std::exception_ptr callback_error;
 bool check(archive *a) noexcept {
  try {
   context->check();auto bytes=archive_filter_bytes(a,0);
   if(bytes>0&&static_cast<uint64_t>(bytes)>context->limits.max_total_bytes)
    throw Failure(STORM_ARCHIVE_LIMIT,"Decoded archive stream exceeds limit");
   return true;
  }catch(...){callback_error=std::current_exception();archive_set_error(a,ECANCELED,nullptr);return false;}
 }
 static la_ssize_t read_callback(archive *a,void *data,const void **out) {
  auto &v=*static_cast<Volume*>(data);auto &r=*v.owner;if(!r.check(a))return -1;
  ssize_t n;do {n=::read(v.fd.value,r.buffer,sizeof(r.buffer));}while(n<0&&errno==EINTR);
  if(n<0)archive_set_error(a,errno,nullptr);*out=r.buffer;return n;
 }
 static la_int64_t seek_callback(archive *a,void *data,la_int64_t offset,int whence) {
  auto &v=*static_cast<Volume*>(data);if(!v.owner->check(a))return ARCHIVE_FATAL;
  auto pos=lseek(v.fd.value,offset,whence);if(pos<0)archive_set_error(a,errno,nullptr);return pos;
 }
 static int switch_callback(archive *a,void *,void *next) {
  // Descriptors stay open; a node read after a switch starts at its beginning.
  auto &v=*static_cast<Volume*>(next);
  if(lseek(v.fd.value,0,SEEK_SET)<0){archive_set_error(a,errno,nullptr);return ARCHIVE_FATAL;}
  return ARCHIVE_OK;
 }
 void checked(int status) {
  if(arena.poisoned||allocation_failed())throw std::bad_alloc();
  if(status!=ARCHIVE_OK)fail();
 }
 Reader(const std::vector<std::string> &inputs,Context *ctx):context(ctx) {
  if(inputs.empty())throw Failure(STORM_ARCHIVE_IO,"Missing archive input");
  uint64_t input_bytes=0;
  for(const auto &input:inputs) {
   auto v=std::make_unique<Volume>();v->owner=this;v->fd.value=open(input.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
   struct stat st{};
   if(v->fd.value<0||fstat(v->fd.value,&st)<0)throw Failure(STORM_ARCHIVE_IO,"Cannot open archive");
   if(!S_ISREG(st.st_mode))throw Failure(STORM_ARCHIVE_INVALID,"Archive volume is not a regular file");
   input_bytes+=static_cast<uint64_t>(st.st_size);volumes.push_back(std::move(v));
  }
  context->begin_container(input_bytes);
  value=archive_read_new();owner.reset(value);
  if(!value)throw std::bad_alloc();
  // Every setup call can fail, including callback_data's dataset allocation.
  // Do not call open1 unless initialization completed successfully.
  checked(archive_read_support_filter_none(value));checked(archive_read_support_filter_gzip(value));
  checked(archive_read_support_filter_bzip2(value));checked(archive_read_support_filter_xz(value));
  checked(archive_read_support_format_zip_seekable(value));checked(archive_read_support_format_7zip(value));
  checked(archive_read_support_format_tar(value));checked(archive_read_support_format_rar(value));
  checked(archive_read_support_format_rar5(value));checked(archive_read_support_format_cab(value));
  checked(archive_read_support_format_iso9660(value));
  if(!context->password.empty())checked(archive_read_add_passphrase(value,context->password.c_str()));
  checked(archive_read_set_callback_data(value,volumes[0].get()));
  for(size_t i=1;i<volumes.size();++i)checked(archive_read_append_callback_data(value,volumes[i].get()));
  checked(archive_read_set_read_callback(value,read_callback));
  checked(archive_read_set_seek_callback(value,seek_callback));
  if(volumes.size()>1)checked(archive_read_set_switch_callback(value,switch_callback));
  checked(archive_read_open1(value));
 }
 [[noreturn]] void fail(){
  if(arena.poisoned||allocation_failed())throw std::bad_alloc();
  if(callback_error)std::rethrow_exception(callback_error);
  std::string error=archive_error_string(value)?archive_error_string(value):"Invalid archive data";
  throw Failure(classify(error,STORM_ARCHIVE_INVALID),error);
 }
 bool next(archive_entry **e) {
  int rc=archive_read_next_header(value,e);if(arena.poisoned||allocation_failed())throw std::bad_alloc();if(rc==ARCHIVE_EOF)return false;
  if(rc!=ARCHIVE_OK)fail();
  // With a password the reader decrypts; a wrong one fails when data is read.
  if(context->password.empty()&&(archive_entry_is_encrypted(*e)||archive_read_has_encrypted_entries(value)>0))
   throw Failure(STORM_ARCHIVE_ENCRYPTED,"Encrypted archives need a password");
  return true;
 }
 std::string path(archive_entry *e) {
  const char *raw=archive_entry_pathname_utf8(e); if(!raw)raw=archive_entry_pathname(e);
  if(!raw)throw Failure(STORM_ARCHIVE_INVALID,"Missing archive path");
  auto type=archive_entry_filetype(e);
  if(archive_entry_symlink(e)||archive_entry_hardlink(e)||(type!=AE_IFREG&&type!=AE_IFDIR))
   throw Failure(STORM_ARCHIVE_INVALID,"Links and special archive entries are forbidden");
  return raw;
 }
};
std::string format(archive *a) {
 switch(archive_format(a)&ARCHIVE_FORMAT_BASE_MASK) {
 case ARCHIVE_FORMAT_ZIP:return "zip";case ARCHIVE_FORMAT_7ZIP:return "7z";
 case ARCHIVE_FORMAT_TAR:return "tar";case ARCHIVE_FORMAT_RAR:case ARCHIVE_FORMAT_RAR_V5:return "rar";
 case ARCHIVE_FORMAT_CAB:return "cab";case ARCHIVE_FORMAT_ISO9660:return "iso9660";
 default:throw Failure(STORM_ARCHIVE_UNSUPPORTED,"Unsupported archive format");
 }
}
}
std::string archive_probe(const std::string &input) {Context ctx(nullptr,nullptr,nullptr,nullptr);Reader r({input},&ctx);archive_entry *e=nullptr;r.next(&e);return format(r.value);}
void archive_extract(const std::vector<std::string> &volumes,Context &ctx) {
 Reader r(volumes,&ctx);archive_entry *e=nullptr;
 while(r.next(&e)) {
  std::string path=ctx.select(r.path(e));
  if(archive_entry_filetype(e)==AE_IFDIR)continue;
  auto size=archive_entry_size(e);
  // A member outside the selected root is skipped (decoded only when solid).
  const auto file_limit=ctx.root_selected&&path.empty()?ctx.limits.max_total_bytes:ctx.limits.max_file_bytes;
  if(size<0||static_cast<uint64_t>(size)>file_limit)
   throw Failure(STORM_ARCHIVE_LIMIT,"Archive entry size exceeds limit");
  if(path.empty()&&ctx.root_selected){if(archive_read_data_skip(r.value)!=ARCHIVE_OK)r.fail();continue;}
  Fd output(path.empty()?-1:ctx.create(path));
  uint64_t written=0;char buffer[65536];
  while(true) {
   auto n=archive_read_data(r.value,buffer,sizeof(buffer));if(n<0||allocation_failed())r.fail();if(!n)break;
   if(static_cast<uint64_t>(n)>file_limit-written)
    throw Failure(STORM_ARCHIVE_LIMIT,"Expanded file exceeds limit");
   written+=n;ctx.account(n,path);if(output.value>=0)ctx.write(output.value,buffer,n);
  }
  if(archive_entry_size_is_set(e)&&written!=static_cast<uint64_t>(size)) r.fail();
 }
}
std::string archive_walk(const std::vector<std::string> &volumes,Context &ctx,ArchiveVisitor &visitor) {
 Reader r(volumes,&ctx);archive_entry *e=nullptr;char buffer[65536];
 while(r.next(&e)) {
  auto raw=r.path(e);ctx.check(raw);
  if(++ctx.entries>ctx.limits.max_entries)throw Failure(STORM_ARCHIVE_LIMIT,"Too many archive entries");
  if(archive_entry_filetype(e)==AE_IFDIR)continue;
  auto size=archive_entry_size(e);
  if(size<0||!archive_entry_size_is_set(e))throw Failure(STORM_ARCHIVE_INVALID,"Archive entry without a size");
  if(!visitor.entry(raw,static_cast<uint64_t>(size)))continue;
  uint64_t read=0;
  while(true) {
   auto n=archive_read_data(r.value,buffer,sizeof(buffer));if(n<0||allocation_failed())r.fail();if(!n)break;
   if(static_cast<uint64_t>(n)>static_cast<uint64_t>(size)-read)throw Failure(STORM_ARCHIVE_INVALID,"Archive member exceeds its declared size");
   read+=n;ctx.account(n,raw);
   if(!visitor.chunk(buffer,static_cast<size_t>(n)))break;
  }
  visitor.done();
 }
 return format(r.value);
}
Manifest archive_manifest(const std::string &input) {
 Context ctx(nullptr,nullptr,nullptr,nullptr);Reader r({input},&ctx);Manifest result;
 archive_entry *e=nullptr;uint64_t total=0;
 while(r.next(&e)) {
  std::string path=ctx.select(r.path(e));
  if(archive_entry_filetype(e)==AE_IFDIR)continue;
  auto size=archive_entry_size(e);
  if(size<0||static_cast<uint64_t>(size)>ctx.limits.max_file_bytes||static_cast<uint64_t>(size)>ctx.limits.max_total_bytes-total)
   throw Failure(STORM_ARCHIVE_LIMIT,"Archive metadata exceeds extraction limits");
  total+=size;
  if(!path.empty()) {
   if(!ctx.paths.insert(lower_path(path)).second)throw Failure(STORM_ARCHIVE_INVALID,"Duplicate output path");
   result.add(path,size);
  }
 }
 result.format=format(r.value);return result;
}
bool inno_volume_signature(const char *p,size_t n) {
 return n>=8&&(!memcmp(p,"idska16\032",8)||!memcmp(p,"idska32\032",8));
}
bool container_signature(const char *p,size_t n) {
 if(n>=512) {
  // POSIX/v7 tar: v7 has no ustar magic, so verify the header checksum.
  unsigned sum=0,stored=0;bool valid=true,digits=false;
  for(size_t i=0;i<512;++i)sum+=(i>=148&&i<156)?32:static_cast<unsigned char>(p[i]);
  for(size_t i=148;i<156;++i){if(p[i]>='0'&&p[i]<='7'){stored=stored*8+(p[i]-'0');digits=true;}else if(p[i]!=0&&p[i]!=' ')valid=false;}
  if(valid&&digits&&stored&&sum==stored)return true;
 }
 auto is=[&](size_t at,const char *s,size_t len){return n>=at+len&&!memcmp(p+at,s,len);};
 return is(0,"MZ",2)||is(0,"PK\003\004",4)||is(0,"PK\005\006",4)||is(0,"7z\274\257\047\034",6)
  ||is(0,"Rar!\032\007",6)||is(0,"MSCF",4)||is(0,"\037\213",2)||is(0,"BZh",3)
  ||is(0,"\3757zXZ\000",6)||is(32769,"CD001",5);
}
}
