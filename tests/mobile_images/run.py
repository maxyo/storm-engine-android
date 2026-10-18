#!/usr/bin/env python3
"""Standalone sanitizer tests. Optional local TX checks never copy user assets into source."""
import argparse, hashlib, os, pathlib, struct, subprocess, tempfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--no-sanitize',action='store_true',help='Explicitly disable sanitizers for toolchains without runtimes')
p.add_argument('--compiler',default=os.environ.get('CXX','c++'))
p.add_argument('--local-texture',action='append',type=pathlib.Path,default=[])
a=p.parse_args(); root=pathlib.Path(__file__).resolve().parents[2]; src=root/'src/libs/xinterface/src'
with tempfile.TemporaryDirectory(prefix='storm-mobile-images-') as tmp:
    binary=pathlib.Path(tmp)/'check'
    command=[a.compiler,'-std=c++17','-O1','-g','-fno-omit-frame-pointer','-I'+str(src),str(pathlib.Path(__file__).with_name('check.cpp')),str(src/'mobile_image_decode.cpp'),str(src/'mobile_image_cache.cpp'),'-o',str(binary)]
    if not a.no_sanitize: command.insert(1,'-fsanitize=address,undefined')
    subprocess.run(command,check=True);subprocess.run([str(binary)],check=True)
    for file in a.local_texture:
        data=file.read_bytes();flags,w,h,mips,fmt,size=struct.unpack_from('<6I',data)
        assert flags==0 and fmt in (21,22,23,25,26,50), 'independent local oracle covers raw TX formats'
        output=pathlib.Path(tmp)/'rgba'
        dims=subprocess.check_output([str(binary),'--decode',str(file.resolve()),str(output)],text=True).strip()
        expected=bytearray();pos=24
        for _ in range(w*h):
            if fmt in (21,22):
                b,g,r,alpha=data[pos:pos+4];pos+=4;expected.extend((r,g,b,alpha if fmt==21 else 255))
            elif fmt==50:
                c=data[pos];pos+=1;expected.extend((c,c,c,255))
            else:
                v=struct.unpack_from('<H',data,pos)[0];pos+=2
                if fmt==26: expected.extend((((v>>8)&15)*17,((v>>4)&15)*17,(v&15)*17,((v>>12)&15)*17))
                elif fmt==23: expected.extend((((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31,255))
                else: expected.extend((((v>>10)&31)*255//31,((v>>5)&31)*255//31,(v&31)*255//31,255 if v&0x8000 else 0))
        actual=output.read_bytes();assert actual==expected
        print('PASS local TX independent full-pixel oracle:',file.name,dims,'format',fmt,'RGBA sha256',hashlib.sha256(actual).hexdigest())
