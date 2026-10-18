#!/usr/bin/env python3
"""Build pinned archive readers into one library. No game data or installers run.

Downloads only versioned open-source build dependencies after SHA-256 verification.
Use a dedicated build directory; never point this at the engine dependency cache.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

DEPS = [
 ('innoextract-1.9', 'https://constexpr.org/innoextract/files/innoextract-1.9.tar.gz', '6344a69fc1ed847d4ed3e272e0da5998948c6b828cb7af39c6321aba6cf88126'),
 ('libarchive-3.8.10', 'https://www.libarchive.org/downloads/libarchive-3.8.10.tar.xz', '56cad765459d5ee25a3b761f90c47d99379d28297fbfb72d908880d6b875fb96'),
 ('xz-5.8.4', 'https://github.com/tukaani-project/xz/releases/download/v5.8.4/xz-5.8.4.tar.xz', '4ce24038fd4221e0d13bc1a2de7a4db56e90b92b3bf75321f6c14be73f65de4b'),
 ('bzip2-1.0.8', 'https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz', 'ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269'),
 ('boost_1_90_0', 'https://archives.boost.io/release/1.90.0/source/boost_1_90_0.tar.bz2', '49551aff3b22cbc5c5a9ed3dbc92f0e23ea50a0f7325b0d198b705e8ee3fc305'),
 ('zlib-1.3.2', 'https://zlib.net/fossils/zlib-1.3.2.tar.gz', 'bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16'),
]
ROOT=Path(__file__).resolve().parent

def run(args):
 print('+', ' '.join(map(str,args)),flush=True)
 subprocess.run(list(map(str,args)),check=True)

def replace(path, old, new, count=1):
 text=path.read_text()
 if new in text:return
 if text.count(old)!=count:raise RuntimeError(f'Unexpected pinned source: {path}: {old!r}')
 path.write_text(text.replace(old,new))

def patch_inno(source):
 stamp=source/'.storm-archive-patch-v1'
 if stamp.exists():return
 # Altered in build tree only. Never link CLI, GOG helpers, or process execution.
 marker='// Storm Android: bounded, non-executing archive reader adaptation.\n'
 replace(source/'src/util/load.cpp', 'boost::uint32_t length = util::load<boost::uint32_t>(is);',
  marker+'boost::uint32_t length = util::load<boost::uint32_t>(is);\n\tif(length > 16 * 1024 * 1024) throw std::runtime_error("Inno string limit exceeded");',2)
 replace(source/'src/setup/info.cpp', '\tresult.clear();',
  marker+'\tif(count > 200000) throw std::runtime_error("Inno entry limit exceeded");\n\tresult.clear();')
 replace(source/'src/setup/info.cpp', '\tif(entries & (info::WizardImages | info::NoSkip)) {',
  marker+'\tif(count > 1000) throw std::runtime_error("Inno image limit exceeded");\n\tif(entries & (info::WizardImages | info::NoSkip)) {')
 replace(source/'src/stream/lzma.cpp','\toptions.preset_dict = NULL;',
  marker+'\tif(options.dict_size > 512 * 1024 * 1024) throw lzma_error("Inno dictionary limit exceeded", LZMA_MEMLIMIT_ERROR);\n\toptions.preset_dict = NULL;')
 replace(source/'src/stream/slice.cpp', '#include "stream/slice.hpp"', marker+'#include "stream/slice.hpp"\n#include <boost/filesystem/directory.hpp>')
 # Bound total parsed string/vector metadata, including fields later skipped.
 for file in ['src/util/load.cpp','src/setup/info.cpp']:
  replace(source/file, '#include <algorithm>' if file.endswith('load.cpp') else '#include "setup/info.hpp"',
   ('#include <algorithm>' if file.endswith('load.cpp') else '#include "setup/info.hpp"')+'\n#include <cstddef>\nextern void storm_inno_charge(std::size_t);')
 replace(source/'src/util/load.cpp', '\tif(length > 16 * 1024 * 1024)', '\tstorm_inno_charge(length);\n\tif(length > 16 * 1024 * 1024)', 2)
 replace(source/'src/setup/info.cpp', '\tif(count > 200000)', '\tstorm_inno_charge(count * sizeof(Entry));\n\tif(count > 200000)')
 # Upstream 1.9 has an unbounded signature-loop typo; reject bad volume magic.
 replace(source/'src/stream/slice.cpp', 'for(size_t i = 0; boost::size(slice_ids); i++)', 'for(size_t i = 0; i < boost::size(slice_ids); i++)')
 stamp.write_text('Bounded metadata, dictionary, and volume signatures; no process code.\n')


def patch_libarchive(source):
 # Replace libarchive's process-exiting hook, not its NULL-returning allocators.
 # The caller compiles this C library with unwind tables and owns all its C
 # allocations in an arena, so partial initialization is recoverable.
 path=source/'libarchive/archive_util.c'
 old='void\n__archive_errx(int retvalue, const char *msg)\n{\n\tstatic const char msg1[] = "Fatal Internal Error in libarchive: ";\n\tsize_t s;\n\n\ts = write(2, msg1, strlen(msg1));\n\t(void)s; /* UNUSED */\n\ts = write(2, msg, strlen(msg));\n\t(void)s; /* UNUSED */\n\ts = write(2, "\\n", 1);\n\t(void)s; /* UNUSED */\n\texit(retvalue);\n}'
 new='/* Storm Android adaptation: unwind to the owning reader; never exit APK. */\n__LA_NORETURN extern void storm_archive_libarchive_fatal(int, const char *);\nvoid\n__archive_errx(int retvalue, const char *msg)\n{\n\tstorm_archive_libarchive_fatal(retvalue, msg);\n}'
 replace(path,old,new)
 # Another upstream OOM-only fatal branch (mac metadata, disabled by default on
 # Android) must use the same recoverable hook if ever reached.
 replace(source/'libarchive/archive_entry.c',
  'metadata = malloc(s);\n\t\tif (metadata == NULL)\n\t\t\tabort();',
  'metadata = malloc(s);\n\t\tif (metadata == NULL)\n\t\t\t__archive_errx(1, "Out of memory");')

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--build-dir',required=True,type=Path)
 p.add_argument('--sources',type=Path)
 p.add_argument('--ndk',type=Path)
 p.add_argument('--jobs',type=int,default=4)
 a=p.parse_args();build=a.build_dir.resolve();src=(a.sources or build/'sources').resolve();src.mkdir(parents=True,exist_ok=True)
 for name,url,sha in DEPS:
  archive=src/url.rsplit('/',1)[1]
  if not archive.exists():
   temp=archive.with_suffix(archive.suffix+'.part');urllib.request.urlretrieve(url,temp);temp.rename(archive)
  if hashlib.sha256(archive.read_bytes()).hexdigest()!=sha:raise RuntimeError(f'Dependency SHA-256 mismatch: {archive}')
  if not (src/name).exists():
   with tarfile.open(archive) as tf:tf.extractall(src,filter='data')
 patch_inno(src/'innoextract-1.9')
 patch_libarchive(src/'libarchive-3.8.10')
 prefix=build/'prefix';prefix.mkdir(parents=True,exist_ok=True)
 common=['-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_POSITION_INDEPENDENT_CODE=ON','-DBUILD_SHARED_LIBS=OFF',f'-DCMAKE_INSTALL_PREFIX={prefix}']
 if a.ndk:common += [f'-DCMAKE_TOOLCHAIN_FILE={a.ndk.resolve()}/build/cmake/android.toolchain.cmake','-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-24','-DANDROID_STL=c++_shared']
 def cmake(name,source,extra=(),target=None):
  dest=build/name
  run(['cmake','-S',source,'-B',dest,*common,*extra]);run(['cmake','--build',dest,'--parallel',a.jobs,*(['--target',target] if target else [])]);return dest
 cmake('zlib',src/'zlib-1.3.2',['-DZLIB_BUILD_TESTING=OFF','-DZLIB_BUILD_SHARED=OFF','-DZLIB_BUILD_STATIC=ON'],'install')
 cmake('xz',src/'xz-5.8.4',['-DXZ_TOOL_XZ=OFF','-DXZ_TOOL_XZDEC=OFF','-DXZ_TOOL_LZMADEC=OFF','-DXZ_TOOL_LZMAINFO=OFF','-DXZ_TOOL_SCRIPTS=OFF','-DXZ_NLS=OFF','-DBUILD_TESTING=OFF'],'install')
 bz=build/'bzip-source';bz.mkdir(parents=True,exist_ok=True)
 (bz/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(storm_bzip C)\nadd_library(bz2 STATIC '+ ' '.join(str(src/'bzip2-1.0.8'/f) for f in ['blocksort.c','huffman.c','crctable.c','randtable.c','compress.c','decompress.c','bzlib.c'])+')\ninstall(TARGETS bz2 ARCHIVE DESTINATION lib)\ninstall(FILES "'+str(src/'bzip2-1.0.8/bzlib.h')+'" DESTINATION include)\n')
 cmake('bzip',bz,target='install')
 disable=['OPENSSL','MBEDTLS','NETTLE','LIBB2','LZ4','LZO','ZSTD','LIBXML2','BSDXML','EXPAT','PCREPOSIX','PCRE2POSIX','TAR','CPIO','CAT','UNZIP','TEST','ACL','XATTR','ICONV']
 cmake('libarchive',src/'libarchive-3.8.10',['-DCMAKE_C_FLAGS=-fexceptions',f'-DCMAKE_PREFIX_PATH={prefix}',f'-DCMAKE_FIND_ROOT_PATH={prefix}',f'-DZLIB_ROOT={prefix}',f'-DLIBLZMA_LIBRARY={prefix}/lib/liblzma.a',f'-DLIBLZMA_INCLUDE_DIR={prefix}/include',f'-DBZIP2_LIBRARY_RELEASE={prefix}/lib/libbz2.a',f'-DBZIP2_INCLUDE_DIR={prefix}/include',*[f'-DENABLE_{x}=OFF' for x in disable]],'install')
 cmake('reader',ROOT,[f'-DSTORM_ARCHIVE_SOURCES={src}',f'-DSTORM_ARCHIVE_PREFIX={prefix}'])
 notices=build/'licenses';notices.mkdir(exist_ok=True)
 for name,filename in [('innoextract-1.9','LICENSE'),('libarchive-3.8.10','COPYING'),('xz-5.8.4','COPYING.0BSD'),('bzip2-1.0.8','LICENSE'),('boost_1_90_0','LICENSE_1_0.txt'),('zlib-1.3.2','LICENSE')]:
  shutil.copyfile(src/name/filename,notices/(name+'.txt'))
 output=build/'output';output.mkdir(exist_ok=True)
 shutil.copyfile(build/'reader/libstorm_archive.so',output/'libstorm_archive.so')
 if a.ndk:
  run([a.ndk.resolve()/'toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip','--strip-unneeded',output/'libstorm_archive.so'])
 print('Archive library:',output/'libstorm_archive.so')
if __name__=='__main__':main()
