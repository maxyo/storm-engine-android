#include "internal.hpp"
#include "memory.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <memory>
#include <exception>
#include <cerrno>
#include <unistd.h>
namespace storm_archive {
namespace {
int classify(const std::string &message,int fallback) {
 std::string text=message;for(char &c:text)if(c>='A'&&c<='Z')c+=32;
 return text.find("encrypt")!=text.npos||text.find("passphrase")!=text.npos?STORM_ARCHIVE_ENCRYPTED:fallback;
}
struct ArchiveDeleter {
 CodecArena *arena;
 void operator()(archive *value)const noexcept {
  if(!value||arena->poisoned)return;
  try {archive_read_free(value);}
  catch(...){mark_allocation_failure();}
 }
};
struct Reader {
 CodecArena arena;
 std::unique_ptr<archive,ArchiveDeleter> owner{nullptr,ArchiveDeleter{&arena}};
 archive *value=nullptr; Fd fd;Context *context;
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
  auto &r=*static_cast<Reader*>(data);if(!r.check(a))return -1;
  ssize_t n;do {n=::read(r.fd.value,r.buffer,sizeof(r.buffer));}while(n<0&&errno==EINTR);
  if(n<0)archive_set_error(a,errno,nullptr);*out=r.buffer;return n;
 }
 static la_int64_t seek_callback(archive *a,void *data,la_int64_t offset,int whence) {
  auto &r=*static_cast<Reader*>(data);if(!r.check(a))return ARCHIVE_FATAL;
  auto pos=lseek(r.fd.value,offset,whence);if(pos<0)archive_set_error(a,errno,nullptr);return pos;
 }
 void checked(int status) {
  if(arena.poisoned||allocation_failed())throw std::bad_alloc();
  if(status!=ARCHIVE_OK)fail();
 }
 explicit Reader(const std::string &input,Context *ctx):fd(open(input.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW)),context(ctx) {
  if(fd.value<0)throw Failure(STORM_ARCHIVE_IO,"Cannot open archive");
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
  checked(archive_read_set_callback_data(value,this));checked(archive_read_set_read_callback(value,read_callback));
  checked(archive_read_set_seek_callback(value,seek_callback));
  checked(archive_read_open1(value));
 }
 void fail(){
  if(arena.poisoned||allocation_failed())throw std::bad_alloc();
  if(callback_error)std::rethrow_exception(callback_error);
  std::string error=archive_error_string(value)?archive_error_string(value):"Invalid archive data";
  throw Failure(classify(error,STORM_ARCHIVE_INVALID),error);
 }
 bool next(archive_entry **e) {
  int rc=archive_read_next_header(value,e);if(arena.poisoned||allocation_failed())throw std::bad_alloc();if(rc==ARCHIVE_EOF)return false;
  if(rc!=ARCHIVE_OK)fail();
  if(archive_entry_is_encrypted(*e)||archive_read_has_encrypted_entries(value)>0)
   throw Failure(STORM_ARCHIVE_ENCRYPTED,"Encrypted archives are not supported");
  return true;
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
std::string archive_probe(const std::string &input) {Context ctx(nullptr,nullptr,nullptr,nullptr);Reader r(input,&ctx);archive_entry *e=nullptr;r.next(&e);return format(r.value);}
void archive_extract(const std::string &input,Context &ctx) {
 Reader r(input,&ctx);archive_entry *e=nullptr;
 while(r.next(&e)) {
  const char *raw=archive_entry_pathname_utf8(e); if(!raw)raw=archive_entry_pathname(e);
  if(!raw)throw Failure(STORM_ARCHIVE_INVALID,"Missing archive path");
  std::string path=ctx.select(raw);
  auto type=archive_entry_filetype(e);
  if(archive_entry_symlink(e)||archive_entry_hardlink(e)||(type!=AE_IFREG&&type!=AE_IFDIR))
   throw Failure(STORM_ARCHIVE_INVALID,"Links and special archive entries are forbidden");
  if(type==AE_IFDIR)continue;
  auto size=archive_entry_size(e);
  if(size<0||static_cast<uint64_t>(size)>ctx.limits.max_file_bytes)
   throw Failure(STORM_ARCHIVE_LIMIT,"Archive entry size exceeds limit");
  Fd output(path.empty()?-1:ctx.create(path));
  uint64_t written=0;char buffer[65536];
  while(true) {
   auto n=archive_read_data(r.value,buffer,sizeof(buffer));if(n<0||allocation_failed())r.fail();if(!n)break;
   if(static_cast<uint64_t>(n)>ctx.limits.max_file_bytes-written)
    throw Failure(STORM_ARCHIVE_LIMIT,"Expanded file exceeds limit");
   written+=n;ctx.account(n,path);if(output.value>=0)ctx.write(output.value,buffer,n);
  }
  if(archive_entry_size_is_set(e)&&written!=static_cast<uint64_t>(size)) r.fail();
 }
}
Manifest archive_manifest(const std::string &input) {
 Context ctx(nullptr,nullptr,nullptr,nullptr);Reader r(input,&ctx);Manifest result;
 archive_entry *e=nullptr;uint64_t total=0;
 while(r.next(&e)) {
  const char *name=archive_entry_pathname_utf8(e);if(!name)name=archive_entry_pathname(e);
  if(!name)throw Failure(STORM_ARCHIVE_INVALID,"Missing archive path");
  std::string path=ctx.select(name);auto type=archive_entry_filetype(e);
  if(archive_entry_symlink(e)||archive_entry_hardlink(e)||(type!=AE_IFREG&&type!=AE_IFDIR))
   throw Failure(STORM_ARCHIVE_INVALID,"Links and special archive entries are forbidden");
  if(type==AE_IFDIR)continue;
  auto size=archive_entry_size(e);
  if(size<0||static_cast<uint64_t>(size)>ctx.limits.max_file_bytes||static_cast<uint64_t>(size)>ctx.limits.max_total_bytes-total)
   throw Failure(STORM_ARCHIVE_LIMIT,"Archive metadata exceeds extraction limits");
  total+=size;
  if(!path.empty()) {
   std::string key=path;for(char &c:key)if(c>='A'&&c<='Z')c+=32;
   if(!ctx.paths.insert(key).second)throw Failure(STORM_ARCHIVE_INVALID,"Duplicate output path");
   result.add(path,size);
  }
 }
 result.format=format(r.value);return result;
}

}
