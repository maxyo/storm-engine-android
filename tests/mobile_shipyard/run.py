#!/usr/bin/env python3
"""Run production Shipyard collector/apply against bounded native service doubles."""
import pathlib,subprocess,tempfile
here=pathlib.Path(__file__).resolve().parent;root=here.parents[1]
with tempfile.TemporaryDirectory(prefix='shipyard-native-') as tmp:
 exe=pathlib.Path(tmp)/'check'
 subprocess.run(['c++','-std=c++17','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(here),'-I'+str(root/'src/libs/xinterface/src'),str(here/'check.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
source=(root/'src/libs/xinterface/src/nodes/xi_table.cpp').read_text();apply=source[source.index('bool CXI_TABLE::MobileApply('):]
assert apply.index('MSG_INTERFACE_NEW_CURRENT_NODE')<apply.index('SelectLine(index)')
print('PASS native table focus precedes selection callback (ProcessFrame ordering)')
