#!/usr/bin/env python3
"""Check full engine artifact identity, architecture, entry point, real audio and entity registrations."""
import argparse
from pathlib import Path
import re
import subprocess
import android_abi
p=argparse.ArgumentParser();p.add_argument('--library',type=Path,required=True);p.add_argument('--ndk',type=Path,required=True);p.add_argument('--allocator-library',type=Path)
p.add_argument('--abi',choices=sorted(android_abi.ABIS),default=android_abi.DEFAULT);a=p.parse_args()
try:android_abi.check_elf(a.library,a.abi)
except RuntimeError as error:raise SystemExit(str(error))
bin=a.ndk/'toolchains/llvm/prebuilt/linux-x86_64/bin'
exported=subprocess.check_output([str(bin/'llvm-nm'),'-D','--defined-only','--demangle',str(a.library)],text=True)
# Release builds use hidden visibility: only the entry points stay in .dynsym,
# the rest is checked in the full symbol table (empty for a stripped library).
local=subprocess.run([str(bin/'llvm-nm'),'--defined-only','--demangle',str(a.library)],text=True,capture_output=True).stdout
symbols=exported+local
if ' SDL_main' not in exported:raise SystemExit('SDL_main is not exported; SDLActivity cannot start the engine')
if 'Java_org_libsdl_app_SDLControllerManager_' in symbols and 'Java_' not in exported:
    raise SystemExit('JNI entry points are not exported')
audio=['SoundService::Init()','storm_audio::Mixer::play(', 'ma_engine_init']
missing=[symbol for symbol in audio if symbol not in symbols]
if missing:
    # LTO may inline these away; the open-audio service still leaves its startup message.
    if b'Open audio: miniaudio' not in a.library.read_bytes():
        raise SystemExit('Missing real engine/audio symbol: '+', '.join(missing))
deps=subprocess.check_output([str(bin/'llvm-readelf'),'-d',str(a.library)],text=True)
# FFmpeg (in-game video) ships beside libengine.so; see tools/ffmpeg/README.md.
for library in ['libSDL2.so','libEGL.so','libGLESv3.so','libfmt.so','libmimalloc.so','libc++_shared.so',
                'libavformat.so','libavcodec.so','libswscale.so','libswresample.so','libavutil.so']:
    if library not in deps:raise SystemExit('Missing runtime dependency: '+library)
if 'fmod' in deps.lower():raise SystemExit('Unexpected FMOD dependency')
print(f'PASS: full engine {a.abi} linked, SDL_main and real SoundService/miniaudio symbols, no FMOD')

# Entities, services and script libraries register only through the static
# <name>vmaci objects of the CREATE_* macros (src/libs/core/include/vma.hpp);
# nothing references them except their static constructor, so a module that
# loses whole-archive linking, or a section/LTO discard, drops them silently and
# the game only fails later when a script creates that class.
macro=re.compile(r'^\s*CREATE_(?:CLASS|SERVICE|SCRIPTLIBRIARY)\((\w+)\)')
# Some registrations exist only with a build option (e.g. SceneSweepLibrary with
# STORM_SCENE_SWEEP): their #ifdef guards are evaluated against the options of the build
# that produced the library, read from its CMakeCache.txt.
cache=next((d/'CMakeCache.txt' for d in a.library.resolve().parents if (d/'CMakeCache.txt').is_file()),None)
if cache is None:raise SystemExit(f'No CMakeCache.txt above {a.library}: the registration check needs the build directory')
options={m[0]:m[1].upper() in ('ON','1','TRUE','YES') for m in re.findall(r'^(STORM_\w+):BOOL=(\w+)$',cache.read_text(errors='replace'),re.M)}
def guard_holds(line,source):
    m=re.fullmatch(r'#\s*(ifdef|ifndef)\s+(\w+)',line) or re.fullmatch(r'#\s*(if)\s+defined\s*\(?\s*(\w+)\s*\)?',line)
    if not m or m[2] not in options:
        raise SystemExit(f'{source}: cannot evaluate the guard of a registration against the build options: {line}')
    return options[m[2]] != (m[1]=='ifndef')
declared,absent=set(),set()
for source in (Path(__file__).resolve().parents[2]/'src').rglob('*.cpp'):
    guards=[]
    for line in source.read_text(errors='replace').splitlines():
        directive=line.strip()
        if re.match(r'#\s*if',directive):guards.append(directive)
        elif re.match(r'#\s*endif',directive):guards.pop()
        elif re.match(r'#\s*el',directive) and guards:guards[-1]=None  # #else/#elif: not evaluated
        elif (name:=macro.match(line)):
            if None in guards:raise SystemExit(f'{source}: registration {name[1]} in an #else/#elif branch is not supported')
            (declared if all(guard_holds(g,source) for g in guards) else absent).add(name[1])
if not declared:raise SystemExit('No CREATE_CLASS/CREATE_SERVICE/CREATE_SCRIPTLIBRIARY registrations found in src')
if not local:raise SystemExit('Registration check needs the unstripped library (build output, not the packaged one)')
# A registration inside a namespace demangles as ns::<name>vmaci.
linked={line.split()[-1].rsplit('::',1)[-1][:-len('vmaci')] for line in local.splitlines() if line.endswith('vmaci')}
missing=sorted(declared-linked)
if missing:raise SystemExit('Registrations missing from libengine.so: '+', '.join(missing))
unexpected=sorted(absent&linked)
if unexpected:raise SystemExit('Registrations linked although their build option is off: '+', '.join(unexpected))
print(f'PASS: all {len(declared)} entity/service/script-library registrations of this configuration are linked'+(f', {len(absent)} option-gated ones absent' if absent else ''))

if a.allocator_library:
    exports=subprocess.check_output([str(bin/'llvm-nm'),'-D','--defined-only',str(a.allocator_library)],text=True)
    names={line.split()[-1] for line in exports.splitlines() if line.split()}
    if 'mi_malloc' not in names:raise SystemExit('Missing real mimalloc allocator')
    if names.intersection({'malloc','calloc','realloc','free'}):
        raise SystemExit('Android mimalloc must not interpose libc allocation: emulated TLS recursion')
    # Placement new is harmless; owning global new/delete must remain in libc++.
    # Exact names: a class's own operator new (RmlUi's layout boxes) is not the global one.
    defined={line.split(' ',2)[2] for line in symbols.splitlines() if line.count(' ')>=2}
    for symbol in ('operator new(unsigned long)', 'operator new[](unsigned long)',
                   'operator delete(void*)', 'operator delete[](void*)'):
        if symbol in defined:raise SystemExit('Android must use the platform C++ allocator: '+symbol)
    print('PASS: mimalloc API retained; Android uses platform C/C++ allocation without heap interposition')
