#!/usr/bin/env python3
from pathlib import Path
import os,subprocess,tempfile
here=Path(__file__).resolve().parent;root=here.parents[1]
def member(source,signature):
 start=source.index(signature);opening=source.index('{',start);depth=1;end=opening+1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
data=(root/'src/libs/core/src/data.cpp').read_text()
commands=(root/'src/libs/battle_interface/src/land/battle_man_command.cpp').read_text()
compiler=(root/'src/libs/core/src/compiler.cpp').read_text()
errors=member(compiler,'void COMPILER::SetError(')
assert errors.index('++storm::android::scriptErrorEpoch;')<errors.index('logError_->error(')
assert '"SendMessage", VAR_PTR' in (root/'src/libs/core/src/internal_functions.cpp').read_text()
methods=member(data,'bool DATA::Convert(')+'\n'+member(commands,'storm::android::LandCommandSource BIManCommandList::RefreshAndroidSource()')
with tempfile.TemporaryDirectory(prefix='android-land-refresh-') as temp:
 cpp=Path(temp)/'check.cpp';binary=Path(temp)/'check';cpp.write_text((here/'check.cpp').read_text().replace('// PRODUCTION_METHODS',methods))
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-fsanitize=address,undefined','-I'+str(root/'src/libs/util/include'),str(cpp),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
