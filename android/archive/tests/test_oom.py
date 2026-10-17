#!/usr/bin/env python3
"""Isolated allocation-failure sweep for the private C archive allocator.

Build the host reader with -DSTORM_ARCHIVE_TESTING=ON. Every operation/allocation
position runs in a subprocess; a signal, exit(), hang or leaked quota fails the
suite. Single and persistent failures are repeated within each child, followed
by a successful operation after disarming fault injection. No game data needed.
"""
import argparse
import ctypes as C
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

def load(path):
    lib=C.CDLL(str(Path(path).resolve()))
    lib.storm_archive_test_fault.argtypes=[C.c_size_t,C.c_int,C.c_size_t]
    lib.storm_archive_test_used.restype=C.c_size_t
    lib.storm_archive_test_attempts.restype=C.c_size_t
    lib.storm_archive_inspect.argtypes=[C.c_char_p,C.c_char_p,C.c_size_t,C.c_char_p,C.c_size_t]
    lib.storm_archive_manifest.argtypes=[C.c_char_p,C.POINTER(C.c_void_p),C.c_char_p,C.c_size_t]
    lib.storm_archive_free.argtypes=[C.c_void_p]
    callback=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_char_p,C.c_uint64,C.c_uint64)
    lib.storm_archive_extract.argtypes=[C.c_char_p,C.c_char_p,C.c_void_p,callback,C.c_void_p,C.c_char_p,C.c_size_t]
    lib.storm_archive_test_fault(0,0,0)  # Initialize loader/TLS before arming.
    return lib,callback

def child(args):
    lib,callback=load(args.library)
    initial=lib.storm_archive_test_used()
    def operation():
        error=C.create_string_buffer(2048);format=C.create_string_buffer(64)
        if args.operation=='inspect':
            code=lib.storm_archive_inspect(os.fsencode(args.input),format,len(format),error,len(error))
        elif args.operation=='manifest':
            data=C.c_void_p()
            code=lib.storm_archive_manifest(os.fsencode(args.input),C.byref(data),error,len(error))
            lib.storm_archive_free(data)
        else:
            with tempfile.TemporaryDirectory(prefix='storm-oom-output-') as out:
                cb=callback(lambda user,path,size,files:int(size>1024*1024) if args.input.suffix.lower()=='.exe' else 1) if args.operation=='cancel' else callback()
                code=lib.storm_archive_extract(os.fsencode(args.input),os.fsencode(out),None,cb,None,error,len(error))
        return code,error.value.decode(errors='replace')
    outcomes=[]
    for _ in range(args.repeat):
        lib.storm_archive_test_fault(args.position,args.persistent,args.quota)
        code,error=operation();attempts=lib.storm_archive_test_attempts()
        lib.storm_archive_test_fault(0,0,0)
        after=lib.storm_archive_test_used()
        assert after==initial,(args.operation,args.position,'quota leak',initial,after,code,error)
        expected=6 if args.position and attempts>=args.position or args.quota and attempts else 7 if args.operation=='cancel' else 0
        assert code==expected,(args.operation,args.position,'status',code,expected,error)
        outcomes.append({'status':code,'allocations':attempts})
    # Same process and same library must remain usable after every error.
    code,error=operation()
    assert code==(7 if args.operation=='cancel' else 0),('recovery',code,error)
    assert lib.storm_archive_test_used()==initial,('recovery quota leak',lib.storm_archive_test_used())
    print(json.dumps({'outcomes':outcomes,'live_bytes':initial}))

def fixtures(root):
    files={'app/RESOURCE/Корабль.txt':b'fixture '*4096,'app/PROGRAM/game.c':b'void game() {}','app/engine.ini':b'[engine]'}
    path=root/'game.zip'
    with zipfile.ZipFile(path,'w',compression=zipfile.ZIP_DEFLATED) as archive:
        for name,data in files.items():archive.writestr(name,data)
    result=[path]
    path=root/'game.tar.gz'
    with tarfile.open(path,'w:gz') as archive:
        for name,data in files.items():
            entry=tarfile.TarInfo(name);entry.size=len(data);archive.addfile(entry,io.BytesIO(data))
    result.append(path)
    if shutil.which('7z'):
        source=root/'source'
        for name,data in files.items():
            path=source/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
        path=root/'game.7z'
        subprocess.run(['7z','a',str(path),'app'],cwd=source,stdout=subprocess.DEVNULL,check=True)
        result.append(path)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--library',required=True,type=Path)
    p.add_argument('--report',type=Path)
    p.add_argument('--inno-installer',type=Path)
    p.add_argument('--inno-only',action='store_true')
    p.add_argument('--child',action='store_true')
    p.add_argument('--input',type=Path)
    p.add_argument('--operation',choices=['inspect','manifest','extract','cancel'])
    p.add_argument('--position',type=int,default=0)
    p.add_argument('--persistent',type=int,default=0)
    p.add_argument('--quota',type=int,default=0)
    p.add_argument('--repeat',type=int,default=3)
    args=p.parse_args()
    if args.child:return child(args)
    results=[]
    def run(path,op,position=0,persistent=0,quota=0):
        cmd=[sys.executable,__file__,'--child','--library',str(args.library.resolve()),'--input',str(path),'--operation',op,'--position',str(position),'--persistent',str(persistent),'--quota',str(quota),'--repeat',str(args.repeat)]
        result=subprocess.run(cmd,text=True,capture_output=True,timeout=15)
        if result.returncode:
            raise RuntimeError(f'{path.name} {op} allocation={position} persistent={persistent} quota={quota}: process exit {result.returncode}\n{result.stdout}\n{result.stderr}')
        data=json.loads(next(line for line in result.stdout.splitlines() if line.startswith('{"outcomes":')))
        results.append({'format':path.suffix,'operation':op,'position':position,'persistent':bool(persistent),'quota':quota,**data})
        return data
    with tempfile.TemporaryDirectory(prefix='storm-oom-fixtures-') as directory:
        inputs=[] if args.inno_only else fixtures(Path(directory))
        if args.inno_installer:inputs.append(args.inno_installer.resolve())
        for path in inputs:
            operations=['inspect','manifest','cancel'] if path.suffix.lower()=='.exe' else ['inspect','manifest','extract','cancel']
            for op in operations:
                baseline=run(path,op);count=baseline['outcomes'][0]['allocations']
                for persistent in [0,1]:
                    for position in range(1,count+1):run(path,op,position,persistent)
                for quota in [1,256,2048]:run(path,op,quota=quota)
                print(f'{path.name} {op}: {count} allocation positions, single+persistent failures, quota and recovery passed',flush=True)
    report={'subprocess_cases':len(results),'repetitions_per_case':args.repeat,'results':results}
    if args.report:args.report.write_text(json.dumps(report,indent=2))
    print(f'Passed {len(results)} isolated subprocess cases; no crashes, exits, or quota leaks')
if __name__=='__main__':main()
