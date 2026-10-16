#!/usr/bin/env python3
"""Read an existing Ninja compile command; compile changed ARM64 sources into a temporary directory.
Never runs Ninja builds or writes the reference build/cache/worktree.
"""
import argparse, pathlib, shlex, subprocess, tempfile
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reference-build',type=pathlib.Path,required=True);p.add_argument('--source',action='append',help='Check only named xinterface source(s)');a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[2]
target='src/libs/xinterface/CMakeFiles/xinterface.dir/src/xinterface.cpp.o'
line=subprocess.check_output(['ninja','-C',str(a.reference_build.resolve()),'-t','commands',target],text=True).splitlines()[-1]
args=shlex.split(line);original=pathlib.Path(args[args.index('-c')+1]);old=str(original).split('/src/libs/')[0]
filtered=[];i=0
while i<len(args):
    if args[i] in ('-o','-c','-MF','-MT'): i+=2;continue
    if args[i]=='-MD': i+=1;continue
    filtered.append(args[i].replace(old+'/src/',str(root)+'/src/'));i+=1
sources=['mobile_image_decode.cpp','mobile_image_cache.cpp','mobile_image_bridge.cpp','xservice.cpp','xinterface.cpp','nodes/xi_picture.cpp','nodes/xi_table.cpp']
if a.source:
    assert all(source in sources for source in a.source), 'source must belong to the known changed-source list'
    sources=a.source
with tempfile.TemporaryDirectory(prefix='storm-mobile-arm64-') as tmp:
    for n,source in enumerate(sources):
        subprocess.run(filtered+['-c',str(root/'src/libs/xinterface/src'/source),'-o',str(pathlib.Path(tmp)/f'{n}.o')],check=True,cwd=root)
        print('PASS ARM64',source,flush=True)
