#!/usr/bin/env python3
"""Run the production picture collector against an atlas service boundary."""
from pathlib import Path
import os, subprocess, tempfile
here=Path(__file__).resolve().parent
source=here.parents[1]/"src/libs/xinterface/src"
text=(source/"nodes/xi_picture.cpp").read_text()
collect=text[text.index("void CXI_PICTURE::MobileCollect("):text.index("bool CXI_PICTURE::MobileApply(")]
def method(signature):
    start=text.index(signature);end=text.index('{',start)+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
with tempfile.TemporaryDirectory(prefix="storm-picture-tabs-") as tmp:
    build=Path(tmp)
    (build/"collect.inc").write_text(collect)
    (build/"sources.inc").write_text('\n'.join(method(signature) for signature in [
        'void CXI_PICTURE::SetNewPicture(', 'void CXI_PICTURE::SetNewPictureByGroup(',
        'void CXI_PICTURE::ChangeUV(', 'void CXI_PICTURE::ReleasePicture(']))
    exe=build/"check"
    subprocess.run([os.environ.get("CXX","c++"),"-std=c++17","-O1","-g","-fsanitize=address,undefined","-fno-omit-frame-pointer","-I"+str(source),"-I"+tmp,str(here/"check.cpp"),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
