#!/usr/bin/env python3
"""Run actual compiler lifecycle hook bodies against interpreter/filesystem boundary doubles."""
from pathlib import Path
import os,re,subprocess,tempfile
here=Path(__file__).resolve().parent;root=here.parents[1]
source=(root/'src/libs/core/src/compiler.cpp').read_text()
def member(signature):
 start=source.index(signature);opening=source.index('{',start);depth=1;end=opening+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
methods='\n'.join(member(name) for name in ['void COMPILER::ResetAndroidUiAdapter()', 'void COMPILER::LoadAndroidUiAdapter()', 'bool COMPILER::Run()', 'bool COMPILER::OnLoad()'])
release=member('void COMPILER::Release()');restore=member('bool COMPILER::LoadState(')
assert release.index('ResetAndroidUiAdapter();')<release.index('EventTab.Release();')
assert restore.index('Release();')<restore.index('BC_LoadSegment(pSegmentName)')<restore.index('OnLoad();')
assert source.count('if (executed) LoadAndroidUiAdapter();')==2
with tempfile.TemporaryDirectory(prefix='android-ui-bootstrap-') as temp:
 cpp=Path(temp)/'check.cpp';binary=Path(temp)/'check'
 cpp.write_text((here/'check.cpp').read_text().replace('// PRODUCTION_METHODS',methods))
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-fsanitize=address,undefined','-I'+str(root/'src/libs/util/include'),str(cpp),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
