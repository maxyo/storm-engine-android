#!/usr/bin/env python3
"""Sanitizer tests of the production decoder/cache, linked with bimg from the renderer's bgfx pin.
Compiles through the Storm ccache launcher (tools/ccache/README.md) in the kept build directory.
Optional local TX checks never copy user assets into source. Offline: set
FETCHCONTENT_SOURCE_DIR_STORM_BGFX_UPSTREAM to a pinned, patched bgfx.cmake checkout."""
import argparse, hashlib, os, pathlib, struct, subprocess, tempfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir',type=pathlib.Path,required=True)
p.add_argument('--no-sanitize',action='store_true',help='Explicitly disable sanitizers for toolchains without runtimes')
p.add_argument('--compiler',default=os.environ.get('CXX','c++'))
p.add_argument('--local-texture',action='append',type=pathlib.Path,default=[])
a=p.parse_args(); here=pathlib.Path(__file__).resolve().parent
if pathlib.Path(a.compiler).match('tools/ccache/cxx'): a.compiler=os.environ.get('STORM_CCACHE_CXX','c++')  # CMake adds the launcher
# A configured tree keeps its compiler: use another --build-dir for another compiler.
build=a.build_dir.resolve()
configure=['cmake','-S',str(here),'-B',str(build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
           f'-DCMAKE_CXX_COMPILER={a.compiler}',f'-DSTORM_SANITIZE={"OFF" if a.no_sanitize else "ON"}']
if os.environ.get('FETCHCONTENT_SOURCE_DIR_STORM_BGFX_UPSTREAM'):
    configure.append('-DFETCHCONTENT_SOURCE_DIR_STORM_BGFX_UPSTREAM='+os.environ['FETCHCONTENT_SOURCE_DIR_STORM_BGFX_UPSTREAM'])
subprocess.run(configure,check=True,stdout=subprocess.DEVNULL)
warmth=here.parents[1]/'tools/ccache/warmth'
subprocess.run([str(warmth),'begin',str(build)],check=True)
subprocess.run(['cmake','--build',str(build),'--target','mobile_images_check'],check=True)
subprocess.run([str(warmth),'end',str(build)],check=True)
binary=build/'mobile_images_check'
subprocess.run([str(binary)],check=True)
with tempfile.TemporaryDirectory(prefix='storm-mobile-images-') as tmp:
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
                elif fmt==23: expected.extend(((((v>>11)&31)*255+15)//31,(((v>>5)&63)*255+31)//63,((v&31)*255+15)//31,255))
                else: expected.extend(((((v>>10)&31)*255+15)//31,(((v>>5)&31)*255+15)//31,((v&31)*255+15)//31,255 if v&0x8000 else 0))
        actual=output.read_bytes();assert actual==expected
        print('PASS local TX independent full-pixel oracle:',file.name,dims,'format',fmt,'RGBA sha256',hashlib.sha256(actual).hexdigest())
