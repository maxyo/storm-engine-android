#!/usr/bin/env python3
"""Source-set discovery and nested extraction on synthetic fixtures. No game assets.

  python3 android/archive/tests/test_discovery.py --library /abs/libstorm_archive.so [-v]
     [--libarchive-tests /abs/libarchive-3.8.10/libarchive/test]  # RAR multi-volume fixtures
     [--inno-installer /abs/setup.exe]                            # local real installer

The library must be built with -DSTORM_ARCHIVE_TESTING=ON (simulated free space).
Fixtures are generated in temporary directories; 7z is needed for 7z/split/encryption cases.
"""
import argparse
import binascii
import ctypes as C
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest
import zipfile

p = argparse.ArgumentParser()
p.add_argument('--library', required=True)
p.add_argument('--libarchive-tests', type=Path)
p.add_argument('--inno-installer', type=Path)
args, remaining = p.parse_known_args()
lib = C.CDLL(str(Path(args.library).resolve()))


class Limits(C.Structure):
    _fields_ = [(n, C.c_uint64) for n in ('total', 'file', 'entries', 'seconds', 'scratch', 'depth', 'ratio', 'reserve')]


Progress = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_char_p, C.c_uint64, C.c_uint64, C.c_uint64)
lib.storm_archive_discover.argtypes = [C.c_char_p, C.c_char_p, C.c_char_p, C.POINTER(Limits), Progress, C.c_void_p,
                                       C.POINTER(C.c_void_p), C.c_char_p, C.c_size_t]
lib.storm_archive_extract_root.argtypes = [C.c_char_p, C.c_char_p, C.c_char_p, C.c_char_p, C.c_char_p, C.POINTER(Limits),
                                           Progress, C.c_void_p, C.POINTER(C.c_void_p), C.c_char_p, C.c_size_t]
lib.storm_archive_free.argtypes = [C.c_void_p]
lib.storm_archive_test_free_bytes.argtypes = [C.c_uint64]
HAVE_7Z = shutil.which('7z') is not None
VERSION_HEADER = ('#define VERSION_NUMBER1       "Корсары: Каждому Свое  " // title\n'
                  '#define VERSION_NUMBER2       "ver. 1.7.3 (26.11.21) x64"\n').encode('cp1251')
LANGUAGE_INI = b'[COMMON]\ndefaultLanguage\t= russian\n;defaultLanguage\t= english\n'


def game(prefix='', marker='a', extra=None):
    """A minimal complete game root under prefix (with trailing slash or empty)."""
    files = {
        prefix + 'engine.ini': b'run = seadogs.c\n',
        prefix + 'options': b'volume.music=0.1\n',
        prefix + 'PROGRAM/seadogs.c': b'void Main() {} // ' + marker.encode(),
        prefix + 'PROGRAM/_LSC_on_off.h': VERSION_HEADER,
        prefix + 'RESOURCE/INI/texts/language.ini': LANGUAGE_INI,
        prefix + 'RESOURCE/INI/texts/russian/common.ini': b'strings',
        prefix + 'RESOURCE/INI/texts/english/common.ini': b'strings',
        prefix + 'RESOURCE/Textures/Корабль.tx': marker.encode() * 64,
        prefix + 'seadogs.exe': b'MZ engine binary is never copied',
        prefix + 'unins000.exe': b'MZ uninstaller',
        prefix + 'Patch_1.3_readme.txt': b'patched',
    }
    files.update(extra or {})
    return files


GAME_FILES = 8  # engine.ini, options, two PROGRAM files and four RESOURCE files


def write_zip(path, files, compression=zipfile.ZIP_DEFLATED):
    with zipfile.ZipFile(path, 'w', compression=compression) as z:
        for name, data in files.items():
            z.writestr(name, data)
    return path


def tree(root, files):
    for name, data in files.items():
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)


def seven(path, root, *extra):
    subprocess.run(['7z', 'a', *extra, str(path), '.'], cwd=root, check=True, stdout=subprocess.DEVNULL)
    return path


class Discovery(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='storm-discovery-')
        base = Path(self.tmp.name)
        self.source, self.scratch, self.work = base / 'source', base / 'scratch', base / 'work'
        for d in (self.source, self.scratch, self.work):
            d.mkdir()
        self.outputs = 0
        lib.storm_archive_test_free_bytes(0)

    def tearDown(self):
        lib.storm_archive_test_free_bytes(0)
        self.tmp.cleanup()

    def limits(self, **values):
        defaults = dict(total=1 << 34, file=1 << 32, entries=400000, seconds=600, scratch=1 << 34, depth=3, ratio=100,
                        reserve=1 << 20)
        defaults.update(values)
        return Limits(**defaults)

    def discover(self, password=None, limits=None, progress=None, source=None):
        data, error = C.c_void_p(), C.create_string_buffer(2048)
        cb = Progress(progress) if progress else Progress()
        code = lib.storm_archive_discover(str(source or self.source).encode(), str(self.scratch).encode(),
                                          password.encode() if password else None, C.byref(limits or self.limits()),
                                          cb, None, C.byref(data), error, len(error))
        document = json.loads(C.string_at(data).decode()) if data.value else None
        lib.storm_archive_free(data)
        return code, document, error.value.decode(errors='replace')

    def found(self, **kwargs):
        code, document, error = self.discover(**kwargs)
        self.assertEqual(code, 0, error)
        return document

    def extract(self, candidate, password=None, limits=None, progress=None, source=None):
        self.outputs += 1
        out = self.work / f'stage{self.outputs}'
        out.mkdir()
        data, error = C.c_void_p(), C.create_string_buffer(2048)
        cb = Progress(progress) if progress else Progress()
        code = lib.storm_archive_extract_root(str(source or self.source).encode(), str(self.scratch).encode(),
                                              password.encode() if password else None, candidate['id'].encode(),
                                              str(out).encode(), C.byref(limits or self.limits()), cb, None,
                                              C.byref(data), error, len(error))
        document = json.loads(C.string_at(data).decode()) if data.value else None
        lib.storm_archive_free(data)
        return code, out, document, error.value.decode(errors='replace')

    def assert_installed(self, candidate, marker='a', **kwargs):
        code, out, _, error = self.extract(candidate, **kwargs)
        self.assertEqual(code, 0, error)
        files = sorted(str(p.relative_to(out)) for p in out.rglob('*') if p.is_file())
        self.assertEqual(len(files), candidate['file_count'])
        self.assertIn('engine.ini', files)
        self.assertNotIn('seadogs.exe', ' '.join(files))
        self.assertEqual((out / 'RESOURCE/Textures/Корабль.tx').read_bytes(), marker.encode() * 64)
        return out

    def test_archive_with_wrapper_and_metadata(self):
        write_zip(self.source / 'game.zip', game('Corsairs/'))
        document = self.found()
        self.assertEqual(len(document['candidates']), 1, document)
        c = document['candidates'][0]
        self.assertEqual((c['kind'], c['format'], c['root'], c['location']), ('archive', 'zip', 'Corsairs/', ['game.zip']))
        self.assertEqual(c['title'], 'Корсары: Каждому Свое')
        self.assertEqual(c['version'], 'ver. 1.7.3 (26.11.21) x64')
        self.assertEqual(c['default_language'], 'russian')
        self.assertEqual(c['languages'], ['english', 'russian'])
        self.assertEqual(c['patch_markers'], ['Patch_1.3_readme.txt'])
        self.assertEqual(c['file_count'], GAME_FILES)
        self.assertTrue(c['ready'])
        self.assertEqual(list(self.scratch.iterdir()), [], 'A plain archive needs no scratch')
        self.assert_installed(c)

    def test_two_copies_in_one_archive(self):
        files = game('Original/', 'o')
        files.update(game('Patched/', 'p'))
        write_zip(self.source / 'both.zip', files)
        document = self.found()
        roots = {c['root']: c for c in document['candidates']}
        self.assertEqual(sorted(roots), ['Original/', 'Patched/'])
        self.assert_installed(roots['Original/'], 'o')
        self.assert_installed(roots['Patched/'], 'p')

    def test_case_distinct_roots_stay_separate(self):
        files = game('Game/', 'u')
        files.update(game('game/', 'l'))
        write_zip(self.source / 'case.zip', files)
        roots = {c['root']: c for c in self.found()['candidates']}
        self.assertEqual(sorted(roots), ['Game/', 'game/'])
        self.assert_installed(roots['game/'], 'l')

    def test_loose_folder_and_archive_side_by_side(self):
        tree(self.source / 'Installed', game('', 'f'))
        write_zip(self.source / 'copy.zip', game('', 'z'))
        document = self.found()
        kinds = {c['kind']: c for c in document['candidates']}
        self.assertEqual(sorted(kinds), ['archive', 'folder'])
        self.assertEqual(kinds['folder']['root'], 'Installed/')
        self.assertEqual(kinds['folder']['version'], 'ver. 1.7.3 (26.11.21) x64')
        self.assert_installed(kinds['folder'], 'f')
        self.assert_installed(kinds['archive'], 'z')

    @unittest.skipUnless(HAVE_7Z, '7z fixture generator unavailable')
    def test_nested_three_levels(self):
        inner = self.work / 'inner'
        inner.mkdir()
        write_zip(inner / 'game.zip', game('Game/'))
        middle = self.work / 'middle'
        middle.mkdir()
        seven(middle / 'repack.7z', inner)
        write_zip(self.source / 'download.zip', {'repack.7z': (middle / 'repack.7z').read_bytes(), 'readme.txt': b'hi'})
        document = self.found()
        self.assertEqual(len(document['candidates']), 1, document)
        c = document['candidates'][0]
        self.assertEqual(c['location'], ['download.zip', 'repack.7z', 'game.zip'])
        self.assertGreater(document['scratch_bytes'], 0)
        self.assert_installed(c)

    def test_depth_limit_is_reported(self):
        payload = write_zip(self.work / 'level4.zip', game())
        for level in (3, 2, 1, 0):
            payload = write_zip(self.work / f'level{level}.zip', {payload.name: payload.read_bytes()})
        shutil.copy(payload, self.source / payload.name)
        document = self.found(limits=self.limits(depth=3))
        self.assertEqual(document['candidates'], [])
        self.assertIn(6, [i['status'] for i in document['issues']], document)
        shutil.rmtree(self.scratch)
        self.scratch.mkdir()
        os.remove(self.source / payload.name)
        shutil.copy(self.work / 'level1.zip', self.source / 'level1.zip')
        self.assertEqual(len(self.found(limits=self.limits(depth=3))['candidates']), 1)

    @unittest.skipUnless(HAVE_7Z, '7z fixture generator unavailable')
    def test_raw_split_volumes(self):
        content = self.work / 'content'
        tree(content, game('', 's', {'RESOURCE/big.dat': os.urandom(300000)}))
        subprocess.run(['7z', 'a', '-v100k', str(self.source / 'game.7z'), '.'], cwd=content, check=True, stdout=subprocess.DEVNULL)
        parts = sorted(self.source.iterdir())
        self.assertGreaterEqual(len(parts), 3)
        c = self.found()['candidates'][0]
        self.assertEqual(c['location'], ['game.7z.001'])
        self.assert_installed(c, 's')
        # A missing middle part is named.
        middle = parts[1]
        hidden = self.work / middle.name
        middle.rename(hidden)
        document = self.found()
        self.assertEqual(document['candidates'], [])
        issue = next(i for i in document['issues'] if i['status'] == 8)
        self.assertEqual(issue['missing_volumes'], [middle.name])
        hidden.rename(middle)
        # A missing last part is suspected by name.
        last = parts[-1]
        last.rename(self.work / last.name)
        issue = next(i for i in self.found()['issues'] if i['status'] == 8)
        self.assertEqual(issue['missing_volumes'], [last.name])

    def test_archive_bomb_by_declared_size(self):
        write_zip(self.source / 'bomb.zip', game('', 'b', {'RESOURCE/zeros.dat': bytes(64 << 20)}))
        code, document, error = self.discover()
        self.assertEqual(code, 6, error)
        self.assertIn('bomb', error)

    def test_bomb_inside_nested_container(self):
        inner = write_zip(self.work / 'inner.zip', {'payload.bin': bytes(64 << 20)})
        write_zip(self.source / 'outer.zip', {'inner.zip': inner.read_bytes()}, zipfile.ZIP_STORED)
        code, _, error = self.discover()
        self.assertEqual(code, 6, error)

    @unittest.skipUnless(HAVE_7Z, '7z fixture generator unavailable')
    def test_zip_password(self):
        content = self.work / 'content'
        tree(content, game('', 'e'))
        subprocess.run(['7z', 'a', '-tzip', '-psecret', str(self.source / 'locked.zip'), '.'], cwd=content, check=True,
                       stdout=subprocess.DEVNULL)
        code, document, error = self.discover()
        self.assertEqual(code, 4, error)
        self.assertEqual(document['encrypted'], {'location': ['locked.zip'], 'password_supported': True, 'password_given': False})
        code, document, error = self.discover(password='wrong')
        self.assertEqual(code, 4, error)
        self.assertTrue(document['encrypted']['password_given'])
        c = self.found(password='secret')['candidates'][0]
        self.assertEqual(c['version'], 'ver. 1.7.3 (26.11.21) x64')
        self.assertEqual(self.extract(c, password='wrong')[0], 4)
        self.assert_installed(c, 'e', password='secret')

    @unittest.skipUnless(HAVE_7Z, '7z fixture generator unavailable')
    def test_encryption_without_password_support(self):
        content = self.work / 'content'
        tree(content, game())
        seven(self.source / 'locked.7z', content, '-psecret', '-mhe=on')
        code, document, error = self.discover(password='secret')
        self.assertEqual(code, 4, error)
        self.assertFalse(document['encrypted']['password_supported'])

    def test_no_space_for_nested_and_for_install(self):
        inner = write_zip(self.work / 'inner.zip', game())
        write_zip(self.source / 'outer.zip', {'inner.zip': inner.read_bytes()})
        lib.storm_archive_test_free_bytes(1 << 20)
        code, document, error = self.discover()
        self.assertEqual(code, 10, error)
        self.assertEqual(document['no_space']['step'], 'nested')
        self.assertGreater(document['no_space']['needed_bytes'], document['no_space']['available_bytes'])
        lib.storm_archive_test_free_bytes(0)
        shutil.rmtree(self.scratch)
        self.scratch.mkdir()
        c = self.found()['candidates'][0]
        lib.storm_archive_test_free_bytes(1 << 20)
        code, out, document, error = self.extract(c)
        self.assertEqual(code, 10, error)
        self.assertEqual(document['no_space']['step'], 'install')
        self.assertEqual(list(out.iterdir()), [])

    def test_inno_volumes_without_installer(self):
        (self.source / 'setup-1a.bin').write_bytes(b'idska32\x1a' + bytes(64))
        issue = self.found()['issues'][0]
        self.assertEqual((issue['status'], issue['missing_volumes']), (8, ['setup.exe']))

    def test_unsupported_and_split_zip_are_reported(self):
        (self.source / 'game.ace').write_bytes(b'**ACE** not supported')
        write_zip(self.source / 'split.zip', game())
        (self.source / 'split.z01').write_bytes(b'PK\x07\x08')
        (self.source / 'menu.exe').write_bytes(b'MZ' + bytes(4096))
        document = self.found()
        self.assertEqual(document['candidates'], [])
        statuses = sorted((i['location'][0], i['status']) for i in document['issues'])
        self.assertEqual(statuses, [('game.ace', 2), ('menu.exe', 3), ('split.zip', 2)])

    def test_partial_root_explains_missing_game(self):
        write_zip(self.source / 'half.zip', {'Game/RESOURCE/a.txt': b'a', 'Game/PROGRAM/b.c': b'b'})
        document = self.found()
        self.assertEqual(document['candidates'], [])
        self.assertEqual(document['partial_roots'][0]['root'], 'Game/')
        self.assertFalse(document['partial_roots'][0]['has_engine_ini'])

    def test_unsafe_member_rejects_only_that_container(self):
        write_zip(self.source / 'evil.zip', {'../escape/engine.ini': b'x'})
        write_zip(self.source / 'good.zip', game())
        document = self.found()
        self.assertEqual([c['location'] for c in document['candidates']], [['good.zip']])
        self.assertEqual(document['issues'][0]['location'], ['evil.zip'])
        self.assertFalse((self.work.parent / 'escape').exists())

    def test_cancel_and_progress_stages(self):
        inner = write_zip(self.work / 'inner.zip', game())
        write_zip(self.source / 'outer.zip', {'inner.zip': inner.read_bytes()})
        stages = set()
        def record(user, stage, path, done, total, files):
            stages.add(stage)
            return 0
        c = self.found(progress=record)['candidates'][0]
        self.assert_installed(c, progress=record)
        self.assertEqual(stages, {1, 2, 3})
        shutil.rmtree(self.scratch)
        self.scratch.mkdir()
        self.assertEqual(self.discover(progress=lambda *a: 1)[0], 7)

    def test_scratch_must_be_empty_and_separate(self):
        write_zip(self.source / 'game.zip', game())
        (self.scratch / 'left-over').write_bytes(b'x')
        self.assertEqual(self.discover()[0], 5)
        data, error = C.c_void_p(), C.create_string_buffer(2048)
        code = lib.storm_archive_discover(str(self.source).encode(), str(self.source / 'inside').encode(), None,
                                          C.byref(self.limits()), Progress(), None, C.byref(data), error, len(error))
        self.assertNotEqual(code, 0)

    @unittest.skipUnless(HAVE_7Z, '7z fixture generator unavailable')
    def test_single_file_source_reads_only_that_file_and_its_volumes(self):
        content = self.work / 'content'
        tree(content, game('', 'v', {'RESOURCE/big.dat': os.urandom(200000)}))
        subprocess.run(['7z', 'a', '-v100k', str(self.source / 'game.7z'), '.'], cwd=content, check=True, stdout=subprocess.DEVNULL)
        write_zip(self.source / 'other.zip', game('', 'o'))
        code, document, error = self.discover(source=self.source / 'game.7z.001')
        self.assertEqual(code, 0, error)
        self.assertEqual([c['location'] for c in document['candidates']], [['game.7z.001']])
        code, out, _, error = self.extract(document['candidates'][0], source=self.source / 'game.7z.001')
        self.assertEqual(code, 0, error)
        self.assertEqual((out / 'RESOURCE/Textures/Корабль.tx').read_bytes(), b'v' * 64)

    def test_changed_source_is_detected(self):
        write_zip(self.source / 'game.zip', game())
        c = self.found()['candidates'][0]
        write_zip(self.source / 'game.zip', game('', 'a', {'RESOURCE/new.txt': b'new'}))
        code, _, _, error = self.extract(c)
        self.assertEqual(code, 5, error)

    def test_tar_gz_inside_zip(self):
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode='w:gz') as tar:
            for name, data in game('Game/').items():
                entry = tarfile.TarInfo(name)
                entry.size = len(data)
                tar.addfile(entry, io.BytesIO(data))
        write_zip(self.source / 'outer.zip', {'nested/game.tar.gz': buffer.getvalue()})
        c = self.found()['candidates'][0]
        self.assertEqual(c['location'], ['outer.zip', 'nested/game.tar.gz'])
        self.assertEqual(c['version'], '', 'tar members are not read for metadata')
        self.assert_installed(c)

    @unittest.skipUnless(args.libarchive_tests, 'Pass --libarchive-tests for RAR multi-volume fixtures')
    def test_rar_multivolume(self):
        for uu in sorted(args.libarchive_tests.glob('test_rar_multivolume_multiple_files.part*.rar.uu')):
            lines = uu.read_text().splitlines()
            data = b''.join(binascii.a2b_uu(line) for line in lines[1:] if line and line != 'end')
            (self.source / uu.name[:-3]).write_bytes(data)
        document = self.found()
        self.assertEqual((document['candidates'], document['issues']), ([], []), 'All six volumes are read as one archive')
        (self.source / 'test_rar_multivolume_multiple_files.part3.rar').unlink()
        issue = self.found()['issues'][0]
        self.assertEqual(issue['missing_volumes'], ['test_rar_multivolume_multiple_files.part3.rar'])

    @unittest.skipUnless(args.inno_installer, 'Pass --inno-installer for a local real installer')
    def test_inno_in_zip_without_volumes(self):
        write_zip(self.source / 'installer.zip', {'Game/setup.exe': args.inno_installer.read_bytes()}, zipfile.ZIP_STORED)
        c = self.found()['candidates'][0]
        self.assertEqual(c['location'], ['installer.zip', 'Game/setup.exe'])
        self.assertFalse(c['ready'])
        self.assertEqual(c['missing_volumes'], ['setup-1a.bin', 'setup-1b.bin', 'setup-1c.bin'])
        self.assertEqual(self.extract(c)[0], 8)


if __name__ == '__main__':
    unittest.main(argv=['test_discovery.py', *remaining])
