#!/usr/bin/env python3
"""Package the SDL/Vulkan probe or the full engine as a locally debug-signed APK.

Uses existing official SDK/NDK/SDL inputs. No downloads, license acceptance,
engine/audio substitution, store upload, or game resources.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]

def run(*args):
    subprocess.run([str(x) for x in args], check=True)

p = argparse.ArgumentParser(description=__doc__)
for name in ('android-jar', 'build-tools', 'sdl-source', 'deps-prefix', 'ndk', 'build-dir'):
    p.add_argument('--' + name, type=Path, required=True)
mode = p.add_mutually_exclusive_group(required=True)
mode.add_argument('--probe-library', type=Path)
mode.add_argument('--engine-library', type=Path)
p.add_argument('--engine-sources', type=Path)
p.add_argument('--audio-sources', type=Path)
p.add_argument('--mimalloc-library', type=Path)
a = p.parse_args()
is_game = a.engine_library is not None
if is_game and not all((a.engine_sources, a.audio_sources, a.mimalloc_library)):
    p.error('Engine APK requires --engine-sources, --audio-sources and --mimalloc-library')
b = a.build_dir.resolve()
b.mkdir(parents=True, exist_ok=True)
classes = b / 'classes'
if classes.exists():
    shutil.rmtree(classes)
classes.mkdir()
manifest = ET.parse(ROOT / 'android/app/src/main/AndroidManifest.xml')
manifest.getroot().set('package', 'org.stormengine.port.game' if is_game else 'org.stormengine.port.probe')
if is_game:
    app=manifest.getroot().find('application')
    app.set('{http://schemas.android.com/apk/res/android}label','Storm Android experimental engine')
    launcher=app.find('activity')
    launcher.set('{http://schemas.android.com/apk/res/android}name','org.stormengine.port.GameLauncherActivity')
    game=ET.SubElement(app,'activity',dict(launcher.attrib))
    game.set('{http://schemas.android.com/apk/res/android}name','org.stormengine.port.GameActivity')
    game.set('{http://schemas.android.com/apk/res/android}exported','false')
manifest.getroot().find('application').set('{http://schemas.android.com/apk/res/android}debuggable', 'true')
manifest.write(b / 'AndroidManifest.xml', encoding='utf-8', xml_declaration=True)
unsigned = b / 'probe-unsigned.apk'
run(a.build_tools / 'aapt2', 'link', '-I', a.android_jar, '--manifest', b / 'AndroidManifest.xml',
    '--min-sdk-version', '24', '--target-sdk-version', '35', '--version-code', '1',
    '--version-name', '0.2-engine-test' if is_game else '0.1-probe', '-o', unsigned)
sources = sorted((a.sdl_source / 'android-project/app/src/main/java').rglob('*.java'))
sources += sorted((ROOT / ('android/game/src' if is_game else 'android/app/src/main/java')).rglob('*.java'))
if not sources:
    raise RuntimeError('No Activity/SDL Java sources')
run('javac', '-source', '8', '-target', '8', '-classpath', a.android_jar, '-d', classes, *sources)
jar = b / 'classes.jar'
with zipfile.ZipFile(jar, 'w') as z:
    for file in classes.rglob('*.class'):
        z.write(file, file.relative_to(classes))
dex = b / 'dex'
dex.mkdir(exist_ok=True)
run(a.build_tools / 'd8', '--lib', a.android_jar, '--min-api', '24', '--output', dex, jar)
libraries = [a.engine_library if is_game else a.probe_library, a.deps_prefix / 'lib/libSDL2.so',
             a.ndk / 'toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so']
if is_game:
    libraries += [a.deps_prefix/'lib/libfmt.so', a.deps_prefix/'lib/libdxvk_d3d9.so', a.mimalloc_library]
with zipfile.ZipFile(unsigned, 'a', compression=zipfile.ZIP_DEFLATED) as z:
    for file in dex.glob('*.dex'):
        z.write(file, file.name)
    for file in libraries:
        header = file.read_bytes()[:20]
        if header[:6] != b'\x7fELF\x02\x01' or int.from_bytes(header[18:20], 'little') != 183:
            raise RuntimeError(f'Not ARM64 ELF: {file}')
        name = 'libmimalloc.so' if is_game and file == a.mimalloc_library else file.name
        z.write(file, 'lib/arm64-v8a/' + name)
    if is_game:
        for project, filename in [('fmt','LICENSE.rst'),('dxvk','LICENSE'),('spdlog','LICENSE'),('fast_float','LICENSE-MIT'),('fast_float','LICENSE-APACHE'),('mimalloc','LICENSE')]:
            z.write(a.engine_sources/project/filename, 'assets/licenses/'+project+'-'+filename)
        for filename in ('miniaudio.h','stb_vorbis.c'):
            z.write(a.audio_sources/filename,'assets/licenses/'+filename)
        z.write(ROOT/'tools/android/patches/miniaudio-0.11.23-error-cleanup.patch','assets/licenses/miniaudio-storm-patch.txt')
    z.write(ROOT / 'LICENSE.txt', 'assets/licenses/storm-GPLv3.txt')
    z.write(a.sdl_source / 'LICENSE.txt', 'assets/licenses/SDL.txt')
    z.write(a.ndk / 'NOTICE.toolchain', 'assets/licenses/NDK-toolchain-NOTICE.txt')
aligned = b / 'probe-aligned.apk'
run(a.build_tools / 'zipalign', '-f', '-P', '16', '4', unsigned, aligned)
key = b / 'probe-debug.keystore'
if not key.exists():
    run('keytool', '-genkeypair', '-keystore', key, '-storepass', 'android', '-keypass', 'android',
        '-alias', 'androiddebugkey', '-dname', 'CN=Android Debug,O=Android,C=US',
        '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000')
output = b / ('storm-engine-arm64-debug.apk' if is_game else 'storm-integration-probe-debug.apk')
run(a.build_tools / 'apksigner', 'sign', '--ks', key, '--ks-key-alias', 'androiddebugkey',
    '--ks-pass', 'pass:android', '--key-pass', 'pass:android', '--out', output, aligned)
run(a.build_tools / 'apksigner', 'verify', '--verbose', output)
run(a.build_tools / 'zipalign', '-c', '-P', '16', '4', output)
print(f'Created {output}; ' + ('full engine build, device/gameplay unverified.' if is_game else 'integration probe only, not the game engine.'))
