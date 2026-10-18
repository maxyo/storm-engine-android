#!/usr/bin/env python3
"""Contract tests for real engine storage consumers; all output stays in --build-dir.

--source-root allows the same harness to validate an unmodified baseline checkout.
Dependencies are existing read-only source directories, never configured or built.
"""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
p.add_argument('--build-dir', type=Path, required=True)
p.add_argument('--dependency-sources', type=Path, required=True)
p.add_argument('--ndk', type=Path)
p.add_argument('--glm-source', type=Path)
p.add_argument('--contract', choices=('all', 'entity_manager', 'named_event', 'postevents'), default='all')
a = p.parse_args()
r = a.source_root.resolve()
b = a.build_dir.resolve()
b.mkdir(parents=True, exist_ok=True)
deps = a.dependency_sources.resolve()
includes = [r/'src/libs/core/src', r/'src/libs/core/include', r/'src/libs/util/include', deps/'fmt/include']
flags = ['-std=c++20', '-DFMT_HEADER_ONLY'] + ['-I'+str(i) for i in includes]
test_dir = Path(__file__).resolve().parent
contracts = ('entity_manager', 'named_event', 'postevents') if a.contract == 'all' else (a.contract,)
for contract in contracts:
    sources = [test_dir/(contract+'.cpp')]
    contract_flags = list(flags)
    if contract == 'entity_manager':
        sources += [r/'src/libs/core/src/entity_manager.cpp', r/'src/libs/core/src/entity_container_cache.cpp']
    else:
        if not a.glm_source:
            p.error('event contracts require --glm-source')
        contract_flags += ['-I'+str(i) for i in (r/'src/libs/math/include', a.glm_source.resolve(),
                           deps/'dxvk/include/native/directx', deps/'dxvk/include/native/windows')]
        if contract == 'postevents':
            sources += [r/'src/libs/core/src/message.cpp']
    subprocess.run([os.environ.get('CXX', 'c++'), *contract_flags, '-g', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-fno-sanitize-recover=all', *map(str, sources), '-o', str(b/contract)], check=True)
    subprocess.run([str(b/contract)], check=True)

if a.ndk:
    if not a.glm_source:
        p.error('--ndk requires --glm-source')
    # Compile actual consumers with the Android toolchain, without a link or a device.
    includes += list((r/'src/libs').glob('*/include'))
    includes += [deps/'SDL/include', deps/'spdlog/include', deps/'mimalloc/include', deps/'fast_float/include',
                 deps/'dxvk/include/native/directx', deps/'dxvk/include/native/windows', a.glm_source.resolve(), b]
    if not (b/'SDL2').exists():
        (b/'SDL2').symlink_to(deps/'SDL/include', target_is_directory=True)
    compiler = a.ndk.resolve()/'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android24-clang++'
    flags = ['-std=c++20', '-DFMT_HEADER_ONLY', '-DSPDLOG_FMT_EXTERNAL'] + ['-I'+str(i) for i in includes]
    for source in ('entity_manager.cpp', 'entity_container_cache.cpp', 'core_impl.cpp', 'compiler.cpp', 'internal_functions.cpp'):
        subprocess.run([str(compiler), *flags, '-c', str(r/'src/libs/core/src'/source), '-o', str(b/(source+'.o'))], check=True)
        print('PASS: real Android ARM64 consumer ' + source, flush=True)
