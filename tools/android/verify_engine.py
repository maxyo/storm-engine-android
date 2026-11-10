#!/usr/bin/env python3
"""Check full engine artifact identity, architecture, entry point and real audio."""
import argparse
from pathlib import Path
import subprocess
p=argparse.ArgumentParser();p.add_argument('--library',type=Path,required=True);p.add_argument('--ndk',type=Path,required=True);p.add_argument('--allocator-library',type=Path);a=p.parse_args()
header=a.library.read_bytes()[:20]
if header[:6]!=b'\x7fELF\x02\x01' or int.from_bytes(header[18:20],'little')!=183:raise SystemExit('Not ARM64 ELF')
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
for library in ['libSDL2.so','libdxvk_d3d9.so','libfmt.so','libmimalloc.so','libc++_shared.so']:
    if library not in deps:raise SystemExit('Missing runtime dependency: '+library)
if 'fmod' in deps.lower():raise SystemExit('Unexpected FMOD dependency')
print('PASS: full engine ARM64 linked, SDL_main and real SoundService/miniaudio symbols, no FMOD')

if a.allocator_library:
    exports=subprocess.check_output([str(bin/'llvm-nm'),'-D','--defined-only',str(a.allocator_library)],text=True)
    names={line.split()[-1] for line in exports.splitlines() if line.split()}
    if 'mi_malloc' not in names:raise SystemExit('Missing real mimalloc allocator')
    if names.intersection({'malloc','calloc','realloc','free'}):
        raise SystemExit('Android mimalloc must not interpose libc allocation: emulated TLS recursion')
    # Placement new is harmless; owning global new/delete must remain in libc++.
    for symbol in ('operator new(unsigned long)', 'operator new[](unsigned long)',
                   'operator delete(void*)', 'operator delete[](void*)'):
        if symbol in symbols:raise SystemExit('Android must use the platform C++ allocator: '+symbol)
    print('PASS: mimalloc API retained; Android uses platform C/C++ allocation without heap interposition')
