#!/usr/bin/env python3
"""Synthetic archive integration and adversarial fixtures. No game assets."""
import argparse
import ctypes as C
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import struct
import unittest
import zipfile

p=argparse.ArgumentParser();p.add_argument('--library',required=True);p.add_argument('--inno-installer',type=Path);args,remaining=p.parse_known_args()
lib=C.CDLL(str(Path(args.library).resolve()))
class Limits(C.Structure):_fields_=[('total',C.c_uint64),('file',C.c_uint64),('entries',C.c_uint64),('seconds',C.c_uint64)]
Callback=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_char_p,C.c_uint64,C.c_uint64)
lib.storm_archive_extract.argtypes=[C.c_char_p,C.c_char_p,C.POINTER(Limits),Callback,C.c_void_p,C.c_char_p,C.c_size_t]
lib.storm_archive_inspect.argtypes=[C.c_char_p,C.c_char_p,C.c_size_t,C.c_char_p,C.c_size_t]
DATA={'app/RESOURCE/Textures/Корабль.txt':b'texture\x00fixture','app/PROGRAM/game.c':b'void game() {}','app/engine.ini':b'[engine]\n','app/crack.exe':b'MZnot executable fixture','app/RESOURCE/payload.dll':b'discard','app/README.txt':b'not game data'}
class Archives(unittest.TestCase):
 def setUp(self):self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.output=self.root/'out';self.output.mkdir()
 def tearDown(self):self.tmp.cleanup()
 def zip(self,entries=None):
  path=self.root/'input.zip'
  with zipfile.ZipFile(path,'w',compression=zipfile.ZIP_DEFLATED) as z:
   for name,data in (entries or DATA).items():z.writestr(name,data)
  return path
 def extract(self,path,limits=None,callback=None):
  error=C.create_string_buffer(2048);cb=Callback(callback) if callback else Callback()
  rc=lib.storm_archive_extract(str(path).encode(),str(self.output).encode(),C.byref(limits) if limits else None,cb,None,error,len(error))
  return rc,error.value.decode(errors='replace')
 def assert_game(self):
  self.assertEqual((self.output/'RESOURCE/Textures/Корабль.txt').read_bytes(),DATA['app/RESOURCE/Textures/Корабль.txt'])
  self.assertEqual((self.output/'PROGRAM/game.c').read_bytes(),DATA['app/PROGRAM/game.c'])
  self.assertEqual(sorted(str(p.relative_to(self.output)) for p in self.output.rglob('*') if p.is_file()),['PROGRAM/game.c','RESOURCE/Textures/Корабль.txt','engine.ini'])
 def manifest(self,path):
  lib.storm_archive_manifest.argtypes=[C.c_char_p,C.POINTER(C.c_void_p),C.c_char_p,C.c_size_t]
  lib.storm_archive_free.argtypes=[C.c_void_p]
  data=C.c_void_p();error=C.create_string_buffer(2048)
  code=lib.storm_archive_manifest(str(path).encode(),C.byref(data),error,len(error))
  manifest=json.loads(C.string_at(data).decode()) if data.value else None
  lib.storm_archive_free(data);return code,manifest,error.value.decode(errors='replace')
 def test_reentrant_manifest_ownership(self):
  path=self.zip();saved=C.c_void_p();nested_error=C.create_string_buffer(2048);state=[]
  lib.storm_archive_manifest.argtypes=[C.c_char_p,C.POINTER(C.c_void_p),C.c_char_p,C.c_size_t]
  lib.storm_archive_free.argtypes=[C.c_void_p]
  def progress(user,name,size,files):
   if files and not state:
    state.append(lib.storm_archive_manifest(str(path).encode(),C.byref(saved),nested_error,len(nested_error)))
   return 0
  self.assertEqual(self.extract(path,callback=progress),(0,''));self.assertEqual(state,[0])
  try:
   manifest=json.loads(C.string_at(saved).decode());self.assertTrue(manifest['compatible_structure'])
  finally:lib.storm_archive_free(saved)
 def test_manifest_zip(self):
  code,data,error=self.manifest(self.zip());self.assertEqual(code,0,error)
  self.assertTrue(data['compatible_structure']);self.assertEqual(data['file_count'],3)
  self.assertEqual(data['required_volumes'],[]);self.assertEqual(data['unpacked_bytes'],sum(len(DATA[n]) for n in ['app/RESOURCE/Textures/Корабль.txt','app/PROGRAM/game.c','app/engine.ini']))
 def test_manifest_incompatible(self):
  code,data,_=self.manifest(self.zip({'other.txt':b'not a game'}));self.assertEqual(code,9);self.assertFalse(data['compatible_structure'])
 def test_manifest_inno_without_volumes(self):
  if not args.inno_installer:self.skipTest('Pass --inno-installer for local real Inno metadata validation')
  setup=self.root/'setup.exe';shutil.copyfile(args.inno_installer,setup)
  code,data,error=self.manifest(setup);self.assertEqual(code,0,error)
  self.assertTrue(data['compatible_structure']);self.assertEqual(data['required_volumes'],['setup-1a.bin','setup-1b.bin','setup-1c.bin'])
  self.assertEqual(data['file_count'],46418);self.assertEqual(data['unpacked_bytes'],12734518486)
  self.assertEqual(self.extract(setup)[0],8)
 def test_inno_bad_volume_signature(self):
  if not args.inno_installer:self.skipTest('Pass --inno-installer for local Inno validation')
  setup=self.root/'setup.exe';shutil.copyfile(args.inno_installer,setup)
  for name in ['setup-1a.bin','setup-1b.bin','setup-1c.bin']:(self.root/name).write_bytes(bytes(128))
  self.assertEqual(self.extract(setup)[0],5)
 def test_inno_actual_decoder_cancel(self):
  if not args.inno_installer:self.skipTest('Pass --inno-installer for actual decoder validation')
  code,error=self.extract(args.inno_installer,callback=lambda user,path,size,count:int(size>1024*1024))
  self.assertEqual(code,7,error)
 def test_zip(self):self.assertEqual(self.extract(self.zip()),(0,''));self.assert_game()
 def test_tar_filters(self):
  for mode in ['w','w:gz','w:bz2','w:xz']:
   with self.subTest(mode=mode):
    shutil.rmtree(self.output);self.output.mkdir();path=self.root/'input.tar'
    with tarfile.open(path,mode) as tar:
     for name,data in DATA.items():
      e=tarfile.TarInfo(name);e.size=len(data);tar.addfile(e,io.BytesIO(data))
    self.assertEqual(self.extract(path),(0,''));self.assert_game()
 def test_cab_stored(self):
  entries={'app/RESOURCE/a.txt':b'resource','app/PROGRAM/a.c':b'program','app/engine.ini':b'engine'}
  table=b'';payload=b''
  for name,data in entries.items():
   table+=struct.pack('<IIHHHH',len(data),len(payload),0,0,0,32)+name.replace('/','\\').encode()+b'\0';payload+=data
  data_offset=44+len(table);size=data_offset+8+len(payload)
  header=struct.pack('<4sIIIIIBBHHHHH',b'MSCF',0,size,0,44,0,3,1,1,len(entries),0,0,0)
  folder=struct.pack('<IHH',data_offset,1,0);block=struct.pack('<IHH',0,len(payload),len(payload))+payload
  path=self.root/'input.cab';path.write_bytes(header+folder+table+block)
  self.assertEqual(self.extract(path),(0,''));self.assertEqual((self.output/'RESOURCE/a.txt').read_bytes(),b'resource')
 def test_encrypted_7z_headers(self):
  if not shutil.which('7z'):self.skipTest('7z fixture generator unavailable')
  src=self.root/'file';src.write_bytes(b'encrypted');path=self.root/'encrypted.7z'
  subprocess.run(['7z','a','-psecret','-mhe=on',str(path),str(src)],check=True,stdout=subprocess.DEVNULL)
  self.assertEqual(self.extract(path)[0],4)
 def test_7z(self):
  if not shutil.which('7z'):self.skipTest('7z fixture generator unavailable')
  source=self.root/'source';source.mkdir()
  for name,data in DATA.items():f=source/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(data)
  path=self.root/'input.7z';subprocess.run(['7z','a',str(path),'app'],cwd=source,check=True,stdout=subprocess.DEVNULL)
  self.assertEqual(self.extract(path),(0,''));self.assert_game()
 def test_traversal(self):
  for path in ['../escape','app/RESOURCE/../../escape','/absolute','C:/windows','app//RESOURCE/file','app/RESOURCE/a\nfile']:
   with self.subTest(path=path):self.assertEqual(self.extract(self.zip({path:b'evil'}))[0],5)
  self.assertFalse((self.root/'escape').exists())
 def test_symlink_and_hardlink(self):
  for typ in [tarfile.SYMTYPE,tarfile.LNKTYPE,tarfile.FIFOTYPE]:
   with self.subTest(type=typ):
    path=self.root/'input.tar'
    with tarfile.open(path,'w') as tar:
     e=tarfile.TarInfo('app/RESOURCE/link');e.type=typ;e.linkname='/tmp';tar.addfile(e)
    self.assertEqual(self.extract(path)[0],5)
 def test_case_collision(self):
  path=self.zip({'app/PROGRAM/game.c':b'a','app/program/GAME.C':b'b'})
  self.assertEqual(self.extract(path)[0],5)
 def test_two_roots(self):self.assertEqual(self.extract(self.zip({'one/PROGRAM/a.c':b'a','two/RESOURCE/a.txt':b'b'}))[0],5)
 def test_limits(self):
  for bounds in [Limits(10,1000,100,10),Limits(1000,3,100,10),Limits(1000,1000,1,10)]:
   with self.subTest(bounds=bounds):
    shutil.rmtree(self.output);self.output.mkdir();self.assertEqual(self.extract(self.zip(),bounds)[0],6)
 def test_cancel(self):self.assertEqual(self.extract(self.zip(),callback=lambda *_:1)[0],7)
 def test_nonempty_staging(self):
  (self.output/'sentinel').write_text('keep');self.assertEqual(self.extract(self.zip())[0],5);self.assertEqual((self.output/'sentinel').read_text(),'keep')
 def test_symlink_staging(self):
  real=self.root/'real';real.mkdir();self.output.rmdir();self.output.symlink_to(real);self.assertEqual(self.extract(self.zip())[0],1);self.assertEqual(list(real.iterdir()),[])
 def test_executable(self):
  path=self.root/'setup.exe';path.write_bytes(b'MZ'+bytes(1024));fmt=C.create_string_buffer(64);err=C.create_string_buffer(1024)
  self.assertEqual(lib.storm_archive_inspect(str(path).encode(),fmt,len(fmt),err,len(err)),3)
 def test_nested_is_not_recursed(self):self.assertEqual(self.extract(self.zip({'inner.zip':b'archive fixture'}))[0],9)
 def test_encrypted_zip(self):
  if not shutil.which('7z'):self.skipTest('7z fixture generator unavailable')
  src=self.root/'file';src.write_bytes(b'encrypted');path=self.root/'encrypted.zip'
  subprocess.run(['7z','a','-tzip','-psecret',str(path),str(src)],check=True,stdout=subprocess.DEVNULL)
  self.assertEqual(self.extract(path)[0],4)
 def test_truncated(self):
  path=self.zip();data=path.read_bytes();path.write_bytes(data[:len(data)//2]);self.assertNotEqual(self.extract(path)[0],0)
 def test_zip_crc(self):
  path=self.root/'corrupt.zip'
  with zipfile.ZipFile(path,'w',compression=zipfile.ZIP_STORED) as z:z.writestr('app/PROGRAM/game.c',b'known unique payload')
  path.write_bytes(path.read_bytes().replace(b'known unique payload',b'brokenunique payload'));self.assertNotEqual(self.extract(path)[0],0)
if __name__=='__main__':unittest.main(argv=['test_archives.py',*remaining])
