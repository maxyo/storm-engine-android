#!/usr/bin/env python3
"""Bounded source adapter checks, not runtime game evidence."""
import json, pathlib, subprocess, tempfile
here=pathlib.Path(__file__).resolve().parent
root=here.parents[1]
with tempfile.TemporaryDirectory(prefix='nation-relations-') as tmp:
    exe=pathlib.Path(tmp)/'check'
    subprocess.run(['c++','-std=c++17','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(root/'src/libs/xinterface/src'),str(here/'check.cpp'),'-o',str(exe)],check=True)
    model=json.loads(subprocess.check_output([str(exe)],text=True))
    source=(root/'src/libs/xinterface/src/xinterface.cpp').read_text()
    start=source.index('void XINTERFACE::MobileCollectWidgets(')
    (pathlib.Path(tmp)/'collection.inc').write_text(source[start:source.index('\n#endif',start)])
    start=source.index('        if(action.screen=="nationrelation" && action.value.rfind("nation@",0)==0)')
    (pathlib.Path(tmp)/'dispatch.inc').write_text(source[start:source.index('        if (node->MobileApply',start)])
    bridge=pathlib.Path(tmp)/'bridge'
    subprocess.run(['c++','-std=c++17','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(root/'src/libs/xinterface/src'),'-I'+tmp,str(here/'bridge.cpp'),'-o',str(bridge)],check=True)
    subprocess.run([str(bridge)],check=True)
    rows=model['widgets'][1:]
    assert len(rows)==5 and sum(len(c['images']) for row in rows for c in row['cells'])==20
    assert model['screenRevision']==7
print('PASS source-linked 20 pairs, exact reordered axes, Unknown, diagonal absence, malformed/missing/duplicate/beta rejection; ASAN/UBSAN')
