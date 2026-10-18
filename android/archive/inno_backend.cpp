#include "internal.hpp"
#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <vector>
#include <sstream>
#include <mutex>
#include <boost/iostreams/read.hpp>
#include "loader/offsets.hpp"
#include "setup/info.hpp"
#include "setup/file.hpp"
#include "setup/data.hpp"
#include "setup/version.hpp"
#include "stream/slice.hpp"
#include "stream/chunk.hpp"
#include "stream/file.hpp"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
static thread_local uint64_t metadata_bytes=0;
void storm_inno_charge(std::size_t size) {
 if(size>128ULL*1024*1024-metadata_bytes)throw storm_archive::Failure(STORM_ARCHIVE_LIMIT,"Inno metadata limit exceeded");
 metadata_bytes+=size;
}
namespace storm_archive {
static std::mutex inno_mutex;
bool inno_probe(const std::string &input) {
 std::lock_guard<std::mutex> lock(inno_mutex);
 metadata_bytes=0;
 std::ifstream is(input,std::ios::binary);loader::offsets offsets;offsets.load(is);
 return offsets.found_magic && offsets.header_offset!=0;
}
void inno_extract(const std::string &input,Context &ctx) {
 std::lock_guard<std::mutex> lock(inno_mutex);
 metadata_bytes=0;
 std::ifstream is(input,std::ios::binary);is.exceptions(std::ios::badbit|std::ios::failbit);
 loader::offsets offsets;offsets.load(is);is.seekg(offsets.header_offset);
 setup::info info;
 info.load(is,setup::info::Files|setup::info::DataEntries|setup::info::NoUnknownVersion,0);
 if(!info.version.is_unicode()) throw Failure(STORM_ARCHIVE_UNSUPPORTED,"ANSI Inno installers are not supported by this build");
 if(info.files.size()>ctx.limits.max_entries||info.data_entries.size()>ctx.limits.max_entries)
  throw Failure(STORM_ARCHIVE_LIMIT,"Too many Inno entries");
 std::vector<std::vector<std::string>> selected(info.data_entries.size());
 for(const auto &file:info.files) {
  std::string path=file.destination;std::replace(path.begin(),path.end(),'\\','/');
  // Only literal {app} content; never evaluate installer constants or code.
  if(path.rfind("{app}/",0)!=0)continue;
  path=ctx.select(path.substr(6));if(path.empty())continue;
  if(file.location>=info.data_entries.size()||!file.additional_locations.empty())
   throw Failure(STORM_ARCHIVE_UNSUPPORTED,"Unsupported external or multipart Inno file");
  if(file.type!=setup::file_entry::UserFile)continue;
  selected[file.location].push_back(path);
 }
 using Files=std::map<stream::file,size_t>;
 std::map<stream::chunk,Files> chunks;
 uint64_t expanded=0;
 for(size_t i=0;i<info.data_entries.size();++i) {
  const auto &d=info.data_entries[i];
  if(d.chunk.encryption!=stream::Plaintext)throw Failure(STORM_ARCHIVE_ENCRYPTED,"Encrypted Inno installers are not supported");
  if(d.file.size>ctx.limits.max_file_bytes||d.uncompressed_size>ctx.limits.max_file_bytes||d.file.offset>ctx.limits.max_total_bytes)
   throw Failure(STORM_ARCHIVE_LIMIT,"Inno file exceeds limit");
  if(d.uncompressed_size>ctx.limits.max_total_bytes-expanded)throw Failure(STORM_ARCHIVE_LIMIT,"Inno total exceeds limit");
  expanded+=d.uncompressed_size;
  if(!selected[i].empty()) chunks[d.chunk][d.file]=i;
 }
 auto slash=input.rfind('/');std::string directory=slash==input.npos?".":input.substr(0,slash);
 std::string basename=input.substr(slash==input.npos?0:slash+1);auto dot=basename.rfind('.');if(dot!=basename.npos)basename.resize(dot);
 std::unique_ptr<stream::slice_reader> slices;
 if(offsets.data_offset)slices.reset(new stream::slice_reader(&is,offsets.data_offset));
 else {
  if(info.header.slices_per_disk<1||info.header.slices_per_disk>26)throw Failure(STORM_ARCHIVE_INVALID,"Invalid Inno volume count");
  // Standard sibling names only; no fallback header-controlled path, no symlinks.
  std::set<uint32_t> needed;
  for(const auto &item:chunks) {
   if(item.first.last_slice<item.first.first_slice||item.first.last_slice>4096)
    throw Failure(STORM_ARCHIVE_LIMIT,"Invalid Inno slice range");
   for(uint32_t i=item.first.first_slice;i<=item.first.last_slice;++i)needed.insert(i);
  }
  for(auto i:needed) {
   std::string path=directory+"/"+stream::slice_reader::slice_filename(basename,i,info.header.slices_per_disk);
   Fd fd(open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC));struct stat st{};
   if(fd.value<0)throw Failure(STORM_ARCHIVE_MISSING_VOLUME,"Missing installer volume: "+path);
   if(fstat(fd.value,&st)<0||!S_ISREG(st.st_mode))throw Failure(STORM_ARCHIVE_INVALID,"Installer volume is not a regular file");
  }
  slices.reset(new stream::slice_reader(directory,basename,"",info.header.slices_per_disk));
 }
 char buffer[65536];
 for(const auto &item:chunks) {
  ctx.check();auto chunk=stream::chunk_reader::get(*slices,item.first,"");uint64_t offset=0;
  for(const auto &part:item.second) {
   const auto &f=part.first;const auto &data=info.data_entries[part.second];
   if(f.offset<offset)throw Failure(STORM_ARCHIVE_INVALID,"Overlapping Inno files");
   while(offset<f.offset) {
    auto n=boost::iostreams::read(*chunk,buffer,std::min<uint64_t>(sizeof(buffer),f.offset-offset));
    if(n<=0)throw Failure(STORM_ARCHIVE_INVALID,"Truncated solid Inno stream");
    offset+=n;ctx.account(n,"");
   }
   crypto::checksum sum;auto source=stream::file_reader::get(*chunk,f,&sum);
   std::vector<std::unique_ptr<Fd>> outputs;
   for(const auto &path:selected[part.second])outputs.emplace_back(new Fd(ctx.create(path)));
   uint64_t written=0;
   while(true) {
    source->read(buffer,sizeof(buffer));auto n=source->gcount();
    if(n>0) {
     if(static_cast<uint64_t>(n)>data.uncompressed_size-written)throw Failure(STORM_ARCHIVE_INVALID,"Inno output exceeds declared size");
     written+=n;ctx.account(n,selected[part.second].front());
     for(size_t output_index=0;output_index<outputs.size();++output_index) {
      if(output_index)ctx.account(n,selected[part.second][output_index]);
      ctx.write(outputs[output_index]->value,buffer,n);
     }
    }
    if(source->eof())break;
    if(source->fail()||!n)throw Failure(STORM_ARCHIVE_INVALID,"Truncated Inno file");
   }
   if(written!=data.uncompressed_size||sum!=f.checksum)throw Failure(STORM_ARCHIVE_INVALID,"Inno file checksum or size mismatch");
   offset=f.offset+f.size;
  }
 }
}
Manifest inno_manifest(const std::string &input) {
 std::lock_guard<std::mutex> lock(inno_mutex);
 metadata_bytes=0;
 Context ctx(nullptr,nullptr,nullptr,nullptr);Manifest result;result.format="inno";
 std::ifstream is(input,std::ios::binary);is.exceptions(std::ios::badbit|std::ios::failbit);
 loader::offsets offsets;offsets.load(is);is.seekg(offsets.header_offset);
 setup::info info;info.load(is,setup::info::Files|setup::info::DataEntries|setup::info::NoUnknownVersion,0);
 if(!info.version.is_unicode())throw Failure(STORM_ARCHIVE_UNSUPPORTED,"ANSI Inno installers are not supported");
 std::ostringstream version;version<<info.version;result.version=version.str();
 if(info.header.slices_per_disk<1||info.header.slices_per_disk>26)throw Failure(STORM_ARCHIVE_INVALID,"Invalid Inno volume count");
 uint64_t total=0;
 for(const auto &d:info.data_entries) {
  if(d.chunk.encryption!=stream::Plaintext)throw Failure(STORM_ARCHIVE_ENCRYPTED,"Encrypted Inno installers are not supported");
  if(d.file.size>ctx.limits.max_file_bytes||d.uncompressed_size>ctx.limits.max_file_bytes||d.file.offset>ctx.limits.max_total_bytes||d.uncompressed_size>ctx.limits.max_total_bytes-total)
   throw Failure(STORM_ARCHIVE_LIMIT,"Inno metadata exceeds limits");
  total+=d.uncompressed_size;
 }
 std::set<uint32_t> slices;
 for(const auto &file:info.files) {
  std::string raw=file.destination;std::replace(raw.begin(),raw.end(),'\\','/');
  if(raw.rfind("{app}/",0)!=0)continue;
  auto path=ctx.select(raw.substr(6));if(path.empty()||file.type!=setup::file_entry::UserFile)continue;
  if(file.location>=info.data_entries.size()||!file.additional_locations.empty())throw Failure(STORM_ARCHIVE_UNSUPPORTED,"Unsupported external or multipart Inno file");
  std::string key=path;for(char &c:key)if(c>='A'&&c<='Z')c+=32;
  if(!ctx.paths.insert(key).second)throw Failure(STORM_ARCHIVE_INVALID,"Duplicate Inno output path");
  const auto &data=info.data_entries[file.location];result.add(path,data.uncompressed_size);
  const auto &chunk=data.chunk;
  if(chunk.last_slice<chunk.first_slice||chunk.last_slice>4096)throw Failure(STORM_ARCHIVE_LIMIT,"Invalid Inno slice range");
  if(!offsets.data_offset)for(uint32_t i=chunk.first_slice;i<=chunk.last_slice;++i)slices.insert(i);
 }
 auto slash=input.rfind('/');auto base=input.substr(slash==input.npos?0:slash+1);auto dot=base.rfind('.');if(dot!=base.npos)base.resize(dot);
 for(auto index:slices)result.volumes.push_back(stream::slice_reader::slice_filename(base,index,info.header.slices_per_disk));
 return result;
}

}
