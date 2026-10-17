#!/usr/bin/env python3
"""Compile actual DXVK public Reset/ResetEx bodies with injected backend failures."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--dxvk-source', type=Path, required=True)
parser.add_argument('--cxx', default='c++')
args = parser.parse_args()
source = (args.dxvk_source / 'src/d3d9/d3d9_device.cpp').read_text()
bodies = []
for name in ('Reset', 'ResetEx'):
    start = source.index('  HRESULT STDMETHODCALLTYPE D3D9DeviceEx::' + name + '(')
    end = source.index('\n  }', start) + len('\n  }')
    bodies.append(source[start:end])
code = r'''
#include <cassert>
#include <stdexcept>
#include <string>
#include <iostream>
#define STDMETHODCALLTYPE
using HRESULT = int;
constexpr HRESULT D3D_OK=0, D3DERR_DEVICEREMOVED=-1, D3DERR_INVALIDCALL=-2;
bool FAILED(HRESULT h) { return h < 0; }
struct D3DPRESENT_PARAMETERS {};
struct D3DDISPLAYMODEEX {};
struct DxvkError { std::string message() const { return "backend failed"; } };
struct Logger { static inline int errors=0; static void err(const std::string&) { ++errors; } };
struct D3D9DeviceLock {
  int& held;
  explicit D3D9DeviceLock(int& h):held(h) { ++held; }
  ~D3D9DeviceLock() { --held; }
};
struct D3D9DeviceEx {
  int held=0, phase=0, failure=0;
  HRESULT swapResult=D3D_OK, stateResult=D3D_OK;
  D3DDISPLAYMODEEX* seenMode=nullptr;
  D3D9DeviceLock LockDevice() { return D3D9DeviceLock(held); }
  void step(int n) { assert(held==1); phase=n; if(failure==n)throw DxvkError(); if(failure==-n)throw std::logic_error("unrelated"); }
  HRESULT ResetSwapChain(D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX* mode) { seenMode=mode;step(1);return swapResult; }
  HRESULT ResetState(D3DPRESENT_PARAMETERS*) { step(2);return stateResult; }
  void Flush() { step(3); }
  void SynchronizeCsThread() { step(4); }
  HRESULT Reset(D3DPRESENT_PARAMETERS*);
  HRESULT ResetEx(D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*);
};
''' + '\n'.join(bodies) + r'''
int main() {
  D3DPRESENT_PARAMETERS params;
  D3DDISPLAYMODEEX mode;
  for(bool ex:{false,true}) {
    auto invoke=[&](D3D9DeviceEx& d) { return ex?d.ResetEx(&params,&mode):d.Reset(&params); };
    for(int phase=1;phase<=(ex?1:4);++phase) {
      D3D9DeviceEx d;d.failure=phase;int before=Logger::errors;
      assert(invoke(d)==D3DERR_DEVICEREMOVED);
      assert(d.phase==phase && d.held==0 && Logger::errors==before+1);
    }
    { D3D9DeviceEx d;d.swapResult=D3DERR_INVALIDCALL;int before=Logger::errors;
      assert(invoke(d)==D3DERR_INVALIDCALL && d.phase==1 && d.held==0 && Logger::errors==before); }
    { D3D9DeviceEx d;assert(invoke(d)==D3D_OK && d.phase==(ex?1:4) && d.held==0);
      assert(d.seenMode==(ex?&mode:nullptr)); }
    { D3D9DeviceEx d;d.failure=-1;bool propagated=false;
      try { invoke(d); } catch(const std::logic_error&) { propagated=true; }
      assert(propagated && d.held==0); }
  }
  { D3D9DeviceEx d;d.stateResult=D3DERR_INVALIDCALL;
    assert(d.Reset(&params)==D3DERR_INVALIDCALL && d.phase==2 && d.held==0); }
  std::cout<<"actual public Reset/ResetEx: backend HRESULT boundary/early return/lock release PASS\n";
}
'''
with tempfile.TemporaryDirectory(prefix='storm-reset-boundary-') as directory:
    cpp = Path(directory) / 'check.cpp'
    cpp.write_text(code)
    for android in (False, True):
        binary = Path(directory) / ('android' if android else 'desktop')
        subprocess.run([args.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        *(['-D__ANDROID__'] if android else []), str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
