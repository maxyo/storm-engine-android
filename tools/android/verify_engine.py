#!/usr/bin/env python3
"""Check full engine artifact identity, architecture, entry point and real audio."""
import argparse
from pathlib import Path
import subprocess
p=argparse.ArgumentParser();p.add_argument('--library',type=Path,required=True);p.add_argument('--ndk',type=Path,required=True);p.add_argument('--allocator-library',type=Path);a=p.parse_args()
header=a.library.read_bytes()[:20]
if header[:6]!=b'\x7fELF\x02\x01' or int.from_bytes(header[18:20],'little')!=183:raise SystemExit('Not ARM64 ELF')
bin=a.ndk/'toolchains/llvm/prebuilt/linux-x86_64/bin'
symbols=subprocess.check_output([str(bin/'llvm-nm'),'-D','--defined-only','--demangle',str(a.library)],text=True)
for symbol in ['SDL_main','SoundService::Init()','storm_audio::Mixer::play(', 'ma_engine_init']:
    if symbol not in symbols:raise SystemExit('Missing real engine/audio symbol: '+symbol)
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
