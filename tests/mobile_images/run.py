#!/usr/bin/env python3
"""Sanitizer tests of the production picture registry and model JSON.
Compiles through the Storm ccache launcher (tools/ccache/README.md) in the kept build directory."""
import argparse, os, pathlib, subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir',type=pathlib.Path,required=True)
p.add_argument('--no-sanitize',action='store_true',help='Explicitly disable sanitizers for toolchains without runtimes')
p.add_argument('--compiler',default=os.environ.get('CXX','c++'))
a=p.parse_args(); here=pathlib.Path(__file__).resolve().parent
if pathlib.Path(a.compiler).match('tools/ccache/cxx'): a.compiler=os.environ.get('STORM_CCACHE_CXX','c++')  # CMake adds the launcher
# A configured tree keeps its compiler: use another --build-dir for another compiler.
build=a.build_dir.resolve()
configure=['cmake','-S',str(here),'-B',str(build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
           f'-DCMAKE_CXX_COMPILER={a.compiler}',f'-DSTORM_SANITIZE={"OFF" if a.no_sanitize else "ON"}']
subprocess.run(configure,check=True,stdout=subprocess.DEVNULL)
warmth=here.parents[1]/'tools/ccache/warmth'
subprocess.run([str(warmth),'begin',str(build)],check=True)
subprocess.run(['cmake','--build',str(build),'--target','mobile_images_check'],check=True)
subprocess.run([str(warmth),'end',str(build)],check=True)
binary=build/'mobile_images_check'
subprocess.run([str(binary)],check=True)
