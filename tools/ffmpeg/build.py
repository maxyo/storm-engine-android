#!/usr/bin/env python3
"""Build the engine's minimal LGPL FFmpeg once per (source, flags, compiler) and print its prefix.

The result is cached under --cache/<target>-<key>/ (include/, lib/, configure.txt); a finished
prefix is reused by every build directory and worktree that points at the same cache. The key
covers the source revision, this script, the configure flags and the compiler identity, so a
change to any of them builds a new prefix. Flags and their reasons: tools/ffmpeg/README.md.

  python3 tools/ffmpeg/build.py --source <ffmpeg checkout> --cache <dir> --target host
  python3 tools/ffmpeg/build.py --source <ffmpeg checkout> --cache <dir> --target android-arm64 --ndk <ndk>
  (android-x86_64: the same for emulator debug builds)
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

# Bump when the way the prefix is produced changes (the flags are hashed separately).
BUILD_FORMAT = 1
LIBRARIES = ('avutil', 'swresample', 'swscale', 'avcodec', 'avformat')

# Only what the game videos need. LGPL-2.1+: never --enable-gpl, --enable-version3 or --enable-nonfree.
COMMON = [
    '--disable-everything', '--disable-autodetect', '--disable-programs', '--disable-doc',
    '--disable-network', '--disable-avdevice', '--disable-avfilter', '--disable-debug',
    '--enable-shared', '--disable-static', '--enable-pic',
    '--enable-avcodec', '--enable-avformat', '--enable-swscale', '--enable-swresample',
    '--enable-pthreads',
    # TEHO: ASF + WMV3 (VC-1 Simple/Main) + WMA2. Caribbean Legend: WebM (matroska) + VP9 + Vorbis.
    '--enable-demuxer=asf,matroska',
    '--enable-decoder=wmv3,vc1,wmav2,vp8,vp9,vorbis,opus',
]
TARGETS = {
    # No nasm on the development hosts: x86 SIMD is off; the host build only serves tests.
    'host': ['--disable-x86asm'],
    'android-arm64': [
        '--enable-cross-compile', '--target-os=android', '--arch=aarch64', '--cpu=armv8-a',
        # Android MediaCodec for VP8/VP9 (hardware decoders; see src/libs/video/README.md).
        '--enable-jni', '--enable-mediacodec', '--enable-decoder=vp8_mediacodec,vp9_mediacodec',
        # Same 16 KiB page alignment the APK requires of every library.
        '--extra-ldflags=-Wl,-z,max-page-size=16384',
    ],
    # Emulator debug builds only (release APKs are arm64). Same features as android-arm64; no nasm
    # on the development hosts, so the x86 SIMD paths are off (plain C decoders).
    'android-x86_64': [
        '--enable-cross-compile', '--target-os=android', '--arch=x86_64', '--disable-x86asm',
        '--enable-jni', '--enable-mediacodec', '--enable-decoder=vp8_mediacodec,vp9_mediacodec',
        '--extra-ldflags=-Wl,-z,max-page-size=16384',
    ],
}
# NDK compiler triple of each Android target.
TRIPLES = {'android-arm64': 'aarch64-linux-android24', 'android-x86_64': 'x86_64-linux-android24'}


def launcher():
    # The Storm ccache launcher in front of the compiler (tools/ccache/README.md). It does not
    # change the output, so it is not part of the cache key.
    reason = os.environ.get('STORM_ALLOW_NO_CCACHE', '').strip()
    if reason:
        print(f'STORM ALARM: FFmpeg compiles WITHOUT CCACHE, STORM_ALLOW_NO_CCACHE="{reason}"', file=sys.stderr, flush=True)
        return ''
    if not shutil.which('ccache'):
        sys.exit('STORM ALARM: ccache is not installed; FFmpeg compiles through tools/ccache/launcher '
                 '(exception: STORM_ALLOW_NO_CCACHE="<reason>", tools/ccache/README.md)')
    return f'{Path(__file__).resolve().parents[2] / "tools/ccache/launcher"} '


def compiler(args):
    if args.target == 'host':
        cc = os.environ.get('CC', 'cc')
        return [f'--cc={launcher()}{cc}'], cc
    tools = args.ndk.resolve() / 'toolchains/llvm/prebuilt/linux-x86_64/bin'
    triple = TRIPLES[args.target]
    cc = tools / f'{triple}-clang'
    if not cc.exists():
        sys.exit(f'NDK compiler not found: {cc}')
    prefix = launcher()
    return [f'--cc={prefix}{cc}', f'--cxx={prefix}{tools / (triple + "-clang++")}', f'--ar={tools / "llvm-ar"}',
            f'--nm={tools / "llvm-nm"}', f'--ranlib={tools / "llvm-ranlib"}', f'--strip={tools / "llvm-strip"}'], str(cc)


def revision(source):
    head = subprocess.run(['git', '-C', str(source), 'rev-parse', 'HEAD'], capture_output=True, text=True)
    if head.returncode == 0:
        return head.stdout.strip()
    return (source / 'RELEASE').read_text().strip()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--cache', type=Path, required=True)
    p.add_argument('--target', choices=sorted(TARGETS), required=True)
    p.add_argument('--ndk', type=Path)
    p.add_argument('--jobs', type=int, default=os.cpu_count())
    args = p.parse_args()
    source = args.source.resolve()
    if not (source / 'configure').is_file():
        sys.exit(f'Not an FFmpeg source tree: {source}')
    if args.target != 'host' and not args.ndk:
        sys.exit('--ndk is required for Android targets')
    tool_flags, cc = compiler(args)
    flags = COMMON + TARGETS[args.target]
    version = subprocess.run([cc, '--version'], capture_output=True, text=True, check=True).stdout
    key = hashlib.sha256(json.dumps([revision(source), flags, version, BUILD_FORMAT]).encode()).hexdigest()[:16]
    cache = args.cache.resolve()
    prefix = cache / f'{args.target}-{key}'
    cache.mkdir(parents=True, exist_ok=True)
    with open(cache / f'{args.target}.lock', 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)  # one build per target at a time; others wait and reuse it
        if not (prefix / '.complete').exists():
            build(source, prefix, flags + tool_flags, flags, args.jobs)
    print(prefix)


def build(source, prefix, configure_flags, recorded_flags, jobs):
    work = prefix.with_name(prefix.name + '.build')
    for path in (work, prefix):
        if path.exists():
            shutil.rmtree(path)
    work.mkdir(parents=True)
    print(f'FFmpeg: building {prefix.name} (cached afterwards)', file=sys.stderr, flush=True)
    log = work / 'build.log'
    with open(log, 'w') as out:
        for command in ([str(source / 'configure'), f'--prefix={prefix}', *configure_flags],
                        ['make', f'-j{jobs}'], ['make', 'install']):
            if subprocess.run(command, cwd=work, stdout=out, stderr=subprocess.STDOUT).returncode:
                sys.exit(f'FFmpeg build failed: {" ".join(command[:1])}; see {log}')
    for name in LIBRARIES:
        if not (prefix / 'lib' / f'lib{name}.so').exists():
            sys.exit(f'FFmpeg build produced no lib{name}.so; see {log}')
    # The configured licence is what ends up in the binaries: refuse anything but LGPL 2.1+.
    if '#define FFMPEG_LICENSE "LGPL version 2.1 or later"' not in (work / 'config.h').read_text():
        sys.exit(f'FFmpeg was not configured as LGPL-2.1-or-later; see {work / "config.h"}')
    (prefix / 'configure.txt').write_text(
        f'FFmpeg {(source / "RELEASE").read_text().strip()} ({revision(source)}), LGPL-2.1-or-later\n'
        'configure ' + ' '.join(recorded_flags) + '\n')
    shutil.copyfile(source / 'COPYING.LGPLv2.1', prefix / 'COPYING.LGPLv2.1')
    shutil.rmtree(work)
    (prefix / '.complete').touch()


if __name__ == '__main__':
    main()
