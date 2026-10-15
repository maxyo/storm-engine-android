#!/usr/bin/env python3
"""Package the real SDL/Vulkan integration probe as a locally debug-signed APK.

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
for name in ('android-jar', 'build-tools', 'sdl-source', 'deps-prefix', 'probe-library', 'ndk', 'build-dir'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
b = a.build_dir.resolve()
b.mkdir(parents=True, exist_ok=True)
classes = b / 'classes'
if classes.exists():
    shutil.rmtree(classes)
classes.mkdir()
manifest = ET.parse(ROOT / 'android/app/src/main/AndroidManifest.xml')
manifest.getroot().set('package', 'org.stormengine.port.probe')
manifest.getroot().find('application').set('{http://schemas.android.com/apk/res/android}debuggable', 'true')
manifest.write(b / 'AndroidManifest.xml', encoding='utf-8', xml_declaration=True)
unsigned = b / 'probe-unsigned.apk'
run(a.build_tools / 'aapt2', 'link', '-I', a.android_jar, '--manifest', b / 'AndroidManifest.xml',
    '--min-sdk-version', '24', '--target-sdk-version', '35', '--version-code', '1',
    '--version-name', '0.1-probe', '-o', unsigned)
sources = sorted((a.sdl_source / 'android-project/app/src/main/java').rglob('*.java'))
sources += sorted((ROOT / 'android/app/src/main/java').rglob('*.java'))
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
libraries = [a.probe_library, a.deps_prefix / 'lib/libSDL2.so',
             a.ndk / 'toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so']
with zipfile.ZipFile(unsigned, 'a', compression=zipfile.ZIP_DEFLATED) as z:
    for file in dex.glob('*.dex'):
        z.write(file, file.name)
    for file in libraries:
        header = file.read_bytes()[:20]
        if header[:6] != b'\x7fELF\x02\x01' or int.from_bytes(header[18:20], 'little') != 183:
            raise RuntimeError(f'Not ARM64 ELF: {file}')
        z.write(file, 'lib/arm64-v8a/' + file.name)
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
output = b / 'storm-integration-probe-debug.apk'
run(a.build_tools / 'apksigner', 'sign', '--ks', key, '--ks-key-alias', 'androiddebugkey',
    '--ks-pass', 'pass:android', '--key-pass', 'pass:android', '--out', output, aligned)
run(a.build_tools / 'apksigner', 'verify', '--verbose', output)
run(a.build_tools / 'zipalign', '-c', '-P', '16', '4', output)
print(f'Created {output}; integration probe only, not the game engine.')
